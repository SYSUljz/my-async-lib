#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ant_server/context/context.hpp"
#include "ant_server/scheduler/spmc_queue.hpp"
#include "ant_server/type.hpp"

class Scheduler {
 public:
  struct WorkerState {
    int thread_id;
    SPMCQueue<TaskNode*> queue;
    std::unique_ptr<Context> ctx;
    TaskNode* lifo_slot {nullptr};
    uint64_t tick {0};
  };

  explicit Scheduler(std::size_t nthreads = std::thread::hardware_concurrency()) : nthreads_(nthreads) {
    workers_.reserve(nthreads_);
    for (size_t i = 0; i < nthreads_; ++i) {
      auto w = std::make_unique<WorkerState>();
      w->thread_id = static_cast<int>(i);
      w->ctx = std::make_unique<Context>(256, this);
      workers_.push_back(std::move(w));
    }
  }

  void schedule(TaskNode* task, std::size_t thread_id) {
    if (thread_id >= workers_.size()) {
      push_global(task);
      return;
    }
    auto& w = *workers_[thread_id];
    if (!w.lifo_slot) {
      w.lifo_slot = task;
    } else {
      if (!w.queue.Push(task)) {
        push_global(task);
      }
    }
  }

  void schedule(TaskNode* task, int thread_id) { schedule(task, static_cast<std::size_t>(thread_id)); }

  void WorkerLoop(int thread_id) {
    g_thread_id = thread_id;
    g_scheduler = this;
    if (thread_id < static_cast<int>(workers_.size())) {
      g_local_context = workers_[thread_id]->ctx.get();
    }

    auto& w = *workers_[thread_id];

    while (running_.load(std::memory_order_relaxed)) {
      w.tick++;

      // Non-blocking check for I/O events on current context
      w.ctx->ProcessEvents(0);

      TaskNode* task = nullptr;

      // 1. Every 61 ticks, check global queue to prevent starvation
      if (w.tick % 61 == 0) {
        task = pop_global();
      }

      // 2. Check LIFO slot
      if (!task && w.lifo_slot) {
        task = w.lifo_slot;
        w.lifo_slot = nullptr;
      }

      // 3. Pop from local Chase-Lev queue (LIFO)
      if (!task) {
        task = w.queue.TryPop();
      }

      // 4. Steal from other workers (FIFO)
      if (!task) {
        task = steal_task(thread_id);
      }

      // 5. Check global queue again
      if (!task) {
        task = pop_global();
      }

      // 6. Execute task or block for CQE events
      if (task) {
        task->run();
      } else {
        w.ctx->ProcessEvents(1);
      }
    }
  }

  void Start() {
    running_.store(true);
    std::vector<std::thread> threads;
    threads.reserve(nthreads_);
    for (std::size_t i = 0; i < nthreads_; i++) {
      threads.emplace_back([this, i]() { this->WorkerLoop(static_cast<int>(i)); });
    }
    for (auto& t : threads) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

  void Stop() {
    running_.store(false);
    cv_.notify_all();
  }

 private:
  void push_global(TaskNode* task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      global_queue_.push_back(task);
    }
    cv_.notify_one();
  }

  TaskNode* pop_global() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (global_queue_.empty()) {
      return nullptr;
    }
    auto* task = global_queue_.front();
    global_queue_.pop_front();
    return task;
  }

  TaskNode* steal_task(int thief_id) {
    for (std::size_t i = 0; i < nthreads_; ++i) {
      if (static_cast<int>(i) == thief_id) continue;
      if (auto* task = workers_[i]->queue.Steal()) {
        return task;
      }
    }
    return nullptr;
  }

  std::vector<std::unique_ptr<WorkerState>> workers_;
  alignas(kCacheLineSize) std::atomic<bool> running_ {false};
  std::deque<TaskNode*> global_queue_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::size_t nthreads_;
};