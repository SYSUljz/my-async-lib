#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include "ant_server/constants.hpp"
#include "ant_server/context/context.hpp"
#include "ant_server/scheduler/executor.hpp"
#include "ant_server/type.hpp"

// ============================================================================
// Scheduler: Orchestrates Dedicated IO Reactors (io_uring) & Worker Executor
// Clean separation of concerns (Mode A: Strict Reactor + Worker Separation).
// ============================================================================
class Scheduler : public Executor {
 public:
  Scheduler(std::size_t n_workers, std::size_t n_io_threads = 2,
            std::size_t uring_size = ant_server::constants::kDefaultServerUringSize)
      : n_workers_(n_workers), n_io_threads_(n_io_threads), uring_size_(uring_size) {
    worker_executor_ = std::make_unique<WorkStealingExecutor>(n_workers_);

    io_contexts_.reserve(n_io_threads_);
    for (std::size_t i = 0; i < n_io_threads_; ++i) {
      auto ctx = std::make_unique<Context>(uring_size_, this, worker_executor_.get());
      io_contexts_.push_back(std::move(ctx));
    }
  }

  // Smart default constructor matching machine topology
  Scheduler()
      : Scheduler(std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2 : 1,
                  std::thread::hardware_concurrency() > 2 ? 2 : 1) {}

  ~Scheduler() override { Stop(); }

  // Executor interface delegation
  void schedule(TaskNode* task) override { worker_executor_->schedule(task); }
  void schedule(TaskNode* task, std::size_t thread_id) override { worker_executor_->schedule(task, thread_id); }
  void schedule(TaskNode* task, int thread_id) { schedule(task, static_cast<std::size_t>(thread_id)); }

  // Accessors
  Executor& GetExecutor() noexcept { return *worker_executor_; }
  WorkStealingExecutor& GetWorkerExecutor() noexcept { return *worker_executor_; }

  Context& GetIOContext(std::size_t index) {
    if (index >= io_contexts_.size()) {
      return *io_contexts_[0];
    }
    return *io_contexts_[index];
  }

  Context& GetNextIOContext() {
    std::size_t idx = next_io_idx_.fetch_add(1, std::memory_order_relaxed) % io_contexts_.size();
    return *io_contexts_[idx];
  }

  std::size_t NumWorkers() const noexcept { return n_workers_; }
  std::size_t NumIOThreads() const noexcept { return n_io_threads_; }

  void IOLoop(int io_id) {
    g_thread_id = static_cast<std::size_t>(io_id);
    g_local_context = io_contexts_[io_id].get();
    g_executor = worker_executor_.get();
    g_scheduler = this;

    auto& ctx = *io_contexts_[io_id];

    while (running_.load(std::memory_order_relaxed)) {
      int ret = ctx.ProcessEvents(1);
      if (ret < 0) {
        if (ret == -EINTR) continue;
        break;
      }
    }
  }

  void Start() {
    running_.store(true, std::memory_order_release);

    // 1. Start pure worker threads
    worker_executor_->Start();

    // 2. Start dedicated IO reactor threads
    io_threads_.reserve(n_io_threads_);
    for (std::size_t i = 0; i < n_io_threads_; ++i) {
      io_threads_.emplace_back([this, i]() { this->IOLoop(static_cast<int>(i)); });
    }

    // 3. Join all IO threads on exit
    for (auto& t : io_threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

  void Stop() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
      // Stop all IO contexts (wake them up from io_uring_submit_and_wait)
      for (auto& ctx : io_contexts_) {
        ctx->Stop();
      }

      // Stop worker executor
      worker_executor_->Stop();

      // Join IO threads
      for (auto& t : io_threads_) {
        if (t.joinable()) {
          t.join();
        }
      }
      io_threads_.clear();
    }
  }

 private:
  std::unique_ptr<WorkStealingExecutor> worker_executor_;
  std::vector<std::unique_ptr<Context>> io_contexts_;
  std::vector<std::thread> io_threads_;
  std::atomic<bool> running_ {false};
  std::atomic<std::size_t> next_io_idx_ {0};
  std::size_t n_workers_;
  std::size_t n_io_threads_;
  std::size_t uring_size_;
};