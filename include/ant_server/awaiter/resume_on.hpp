#pragma once

#include <coroutine>
#include <cstddef>

#include "ant_server/type.hpp"

inline constexpr std::size_t kAnyWorkerThread = static_cast<std::size_t>(-1);

// ============================================================================
// ResumeOnAwaiter: Coroutine awaiter to hop back onto a specified Executor
// (e.g., after DB async query, external thread callbacks, or timer expiration).
// Supports P2C load-balanced dispatch by default or targeted core pinning.
// ============================================================================
struct ResumeOnAwaiter {
  Executor& executor_;
  std::size_t target_thread_id_ {kAnyWorkerThread};

  explicit ResumeOnAwaiter(Executor& exec, std::size_t target_thread_id = kAnyWorkerThread)
      : executor_(exec), target_thread_id_(target_thread_id) {}

  bool await_ready() const noexcept { return false; }

  template <typename PromiseType>
  void await_suspend(std::coroutine_handle<PromiseType> h) noexcept {
    task_.init(h);
    if (target_thread_id_ == kAnyWorkerThread) {
      executor_.schedule(&task_);
    } else {
      executor_.schedule(&task_, target_thread_id_);
    }
  }

  void await_resume() const noexcept {}

 private:
  CoroTask task_;
};

// Helper function to create ResumeOnAwaiter
inline auto resume_on(Executor& exec, std::size_t thread_id = kAnyWorkerThread) {
  return ResumeOnAwaiter {exec, thread_id};
}
