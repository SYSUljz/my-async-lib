#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#include "absl/synchronization/mutex.h"
#include "ant_server/constants.hpp"
#include "ant_server/scheduler/mpmc_queue.hpp"
#include "ant_server/scheduler/spmc_queue.hpp"
#include "ant_server/type.hpp"
#include "ant_server/utils/random.hpp"

// ============================================================================
// WorkStealingExecutor: Pure Compute / Coroutine Task Executor (Worker Pool)
// Implements the Executor interface for decoupled, non-IO task scheduling.
// Uses 3-tier Work-Stealing, LIFO hot slot, Chase-Lev SPMC queues,
// Per-Worker MPSC Inbox sharding, and P2C (Power of Two Choices) dispatching.
// Uses Abseil Mutex & CondVar for adaptive, low-overhead worker parking.
// ============================================================================
class WorkStealingExecutor : public Executor {
 public:
  struct WorkerState {
    int thread_id {0};
    alignas(kCacheLineSize) TaskNode* lifo_slot {nullptr};
    alignas(kCacheLineSize) SPMCQueue<TaskNode*> queue;
    alignas(kCacheLineSize) std::atomic<TaskNode*> inbox {nullptr};
    alignas(kCacheLineSize) uint64_t tick {0};
    alignas(kCacheLineSize) std::atomic<bool> is_parked {false};

    absl::Mutex park_mu;
    absl::CondVar park_cv;

    // Multi-producer push into worker's private inbox (lock-free Treiber stack)
    void push_inbox(TaskNode* task) {
      if (!task) {
        return;
      }
      TaskNode* old_head = inbox.load(std::memory_order_relaxed);
      do {
        task->next = old_head;
      } while (!inbox.compare_exchange_weak(old_head, task, std::memory_order_release, std::memory_order_relaxed));
    }

    // Single-consumer bulk pop from inbox (1 atomic exchange takes all)
    TaskNode* pop_all_inbox() {
      if (!inbox.load(std::memory_order_relaxed)) {
        return nullptr;
      }
      return inbox.exchange(nullptr, std::memory_order_acquire);
    }

    // Estimate current worker load (local queue + LIFO slot + inbox)
    size_t approximate_load() const {
      size_t sz = static_cast<size_t>(queue.Size());
      if (lifo_slot) {
        sz += 1;
      }
      if (inbox.load(std::memory_order_relaxed)) {
        sz += 2;
      }
      return sz;
    }

    void unpark() {
      if (is_parked.load(std::memory_order_relaxed)) {
        absl::MutexLock lock(&park_mu);
        is_parked.store(false, std::memory_order_relaxed);
        park_cv.Signal();
      }
    }

    void park_and_wait(const std::atomic<bool>& running) {
      absl::MutexLock lock(&park_mu);
      if (!running.load(std::memory_order_relaxed)) {
        return;
      }
      is_parked.store(true, std::memory_order_relaxed);
      while (running.load(std::memory_order_relaxed) && is_parked.load(std::memory_order_relaxed) &&
             inbox.load(std::memory_order_relaxed) == nullptr) {
        if (park_cv.WaitWithTimeout(&park_mu, absl::Microseconds(100))) {
          break;  // Timed out
        }
      }
      is_parked.store(false, std::memory_order_relaxed);
    }
  };

  explicit WorkStealingExecutor(std::size_t nthreads = std::max<std::size_t>(1, std::thread::hardware_concurrency()))
      : nthreads_(nthreads) {
    workers_.reserve(nthreads_);
    for (std::size_t i = 0; i < nthreads_; ++i) {
      auto w = std::make_unique<WorkerState>();
      w->thread_id = static_cast<int>(i);
      workers_.push_back(std::move(w));
    }
  }

  ~WorkStealingExecutor() override { Stop(); }

  // General task schedule (P2C load-balanced dispatch from external / IO / DB threads)
  void schedule(TaskNode* task) override {
    if (!task) {
      return;
    }

    // 1. If called from inside a worker thread, prioritize its own local slot/queue
    if (g_executor == this && g_thread_id < workers_.size()) {
      schedule(task, g_thread_id);
      return;
    }

    // 2. If called from external (IO thread, DB pool, or other executors), use P2C
    if (nthreads_ == 1) {
      workers_[0]->push_inbox(task);
      workers_[0]->unpark();
      return;
    }

    std::size_t w1 = ant_server::fast_random() % nthreads_;
    std::size_t w2 = (w1 + 1 + (ant_server::fast_random() % (nthreads_ - 1))) % nthreads_;

    std::size_t load1 = workers_[w1]->approximate_load();
    std::size_t load2 = workers_[w2]->approximate_load();
    std::size_t target = (load1 <= load2) ? w1 : w2;

    workers_[target]->push_inbox(task);
    workers_[target]->unpark();
  }

  // Targeted schedule with worker thread_id hint
  void schedule(TaskNode* task, std::size_t thread_id) override {
    if (!task) {
      return;
    }

    if (thread_id >= workers_.size()) {
      global_queue_.Push(task);
      wake_any_worker();
      return;
    }

    auto& w = *workers_[thread_id];

    // If current thread is the owner worker
    if (g_executor == this && g_thread_id == thread_id) {
      if (!w.lifo_slot) {
        w.lifo_slot = task;
      } else {
        if (!w.queue.Push(task)) {
          // Local queue is full: offload half to global queue
          std::array<TaskNode*, ant_server::constants::kDefaultSpmcCapacity> batch {};
          std::size_t count = w.queue.TakeHalf(batch);
          if (count > 0) {
            for (std::size_t i = 0; i < count - 1; ++i) {
              batch[i]->next = batch[i + 1];
            }
            batch[count - 1]->next = task;
            task->next = nullptr;
            global_queue_.PushBatch(batch[0], task);
          } else {
            global_queue_.Push(task);
          }
          wake_any_worker();
        }
      }
    } else {
      // From another thread targeting this specific worker
      w.push_inbox(task);
      w.unpark();
    }
  }

  void SetScheduler(Scheduler* scheduler) noexcept { scheduler_ = scheduler; }

  void WorkerLoop(int thread_id) {
    g_thread_id = static_cast<std::size_t>(thread_id);
    g_executor = this;
    g_scheduler = scheduler_;
    g_local_context = nullptr;  // Worker threads do NOT own io_uring context

    auto& w = *workers_[thread_id];
    int empty_spins = 0;

    while (running_.load(std::memory_order_relaxed)) {
      w.tick++;
      TaskNode* task = nullptr;

      // 1. Every 61 ticks, check global queue to prevent starvation
      if (w.tick % ant_server::constants::kGlobalCheckInterval == 0) {
        task = global_queue_.Pop();
      }

      // 2. Check LIFO slot (hot cache path)
      if (!task && w.lifo_slot) {
        task = w.lifo_slot;
        w.lifo_slot = nullptr;
      }

      // 3. Drain Inbox (external tasks from IO / DB / P2C)
      if (!task) {
        TaskNode* inbox_list = w.pop_all_inbox();
        if (inbox_list) {
          task = inbox_list;
          TaskNode* curr = inbox_list->next;
          task->next = nullptr;

          // Push remaining tasks from inbox into local queue
          while (curr) {
            TaskNode* next = curr->next;
            curr->next = nullptr;
            if (!w.queue.Push(curr)) {
              global_queue_.Push(curr);
            }
            curr = next;
          }
        }
      }

      // 4. Pop from local Chase-Lev queue (LIFO)
      if (!task) {
        task = w.queue.TryPop();
      }

      // 5. Steal from other workers (Half-stealing / FIFO)
      if (!task) {
        task = steal_task(thread_id);
      }

      // 6. Check global queue again
      if (!task) {
        task = global_queue_.Pop();
      }

      // 7. Execute task or backoff & park
      if (task) {
        empty_spins = 0;
        task->run();
      } else {
        empty_spins++;
        if (empty_spins < 16) {
#if defined(__x86_64__) || defined(_M_X64)
          _mm_pause();
#elif defined(__aarch64__)
          asm volatile("yield" ::: "memory");
#else
          std::this_thread::yield();
#endif
        } else if (empty_spins < 32) {
          std::this_thread::yield();
        } else {
          w.park_and_wait(running_);
          empty_spins = 0;
        }
      }
    }
  }

  void Start() {
    running_.store(true, std::memory_order_release);
    threads_.reserve(nthreads_);
    for (std::size_t i = 0; i < nthreads_; ++i) {
      threads_.emplace_back([this, i]() { this->WorkerLoop(static_cast<int>(i)); });
    }
  }

  void Stop() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
      for (auto& w : workers_) {
        w->unpark();
      }
      for (auto& t : threads_) {
        if (t.joinable()) {
          t.join();
        }
      }
      threads_.clear();
    }
  }

  std::size_t NumWorkers() const noexcept { return nthreads_; }

 private:
  TaskNode* steal_task(int thief_id) {
    if (nthreads_ <= 1) {
      return nullptr;
    }

    std::size_t start = ant_server::fast_random() % nthreads_;
    for (std::size_t i = 0; i < nthreads_; ++i) {
      std::size_t victim_id = (start + i) % nthreads_;
      if (static_cast<int>(victim_id) == thief_id) {
        continue;
      }

      auto& victim = *workers_[victim_id];

      // Try batch stealing half of victim's queue
      std::array<TaskNode*, ant_server::constants::kDefaultSpmcCapacity> batch {};
      std::size_t count = victim.queue.TakeHalf(batch);
      if (count > 0) {
        auto& thief = *workers_[thief_id];
        // Keep the first task for immediate execution, push the rest to thief's local queue
        for (std::size_t k = 1; k < count; ++k) {
          if (!thief.queue.Push(batch[k])) {
            global_queue_.Push(batch[k]);
          }
        }
        return batch[0];
      }

      // Single item steal fallback
      if (auto* task = victim.queue.Steal()) {
        return task;
      }
    }
    return nullptr;
  }

  void wake_any_worker() {
    for (auto& w : workers_) {
      if (w->is_parked.load(std::memory_order_relaxed)) {
        w->unpark();
        break;
      }
    }
  }

  std::vector<std::unique_ptr<WorkerState>> workers_;
  std::vector<std::thread> threads_;
  IntrusiveSpinLockQueue global_queue_;
  std::atomic<bool> running_ {false};
  std::size_t nthreads_;
  Scheduler* scheduler_ {nullptr};
};
