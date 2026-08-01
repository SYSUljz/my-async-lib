#ifndef ANT_TIMEOUT_AWAITER_HPP
#define ANT_TIMEOUT_AWAITER_HPP

#include <chrono>
#include <coroutine>
#include <memory>
#include <stop_token>
#include <utility>

#include "ant_server/context/context.hpp"
#include "ant_server/type.hpp"
#include "ant_server/utils/timer.hpp"

template <typename CoroTaskFunc>
struct TimeoutAwaiter {
  TimeoutAwaiter(Context& ctx, std::chrono::milliseconds delay, CoroTaskFunc&& task_func)
      : ctx_(ctx), delay_(delay), task_func_(std::forward<CoroTaskFunc>(task_func)) {}

  bool await_ready() { return false; }

  void await_suspend(std::coroutine_handle<> handle) {
    parent_handle_ = handle;
    source_ = std::make_shared<std::stop_source>();

    // Launch child coroutine task via lambda
    child_task_ = task_func_();
    if (child_task_.handle) {
      child_task_.handle.promise().set_stop_token(source_->get_token());
      child_task_.handle.promise().set_continuation(parent_handle_);
    } else {
      parent_handle_.resume();
      return;
    }

    auto& timer = ctx_.UseService<AntTimer>();
    timer_id_ = timer.AddTimer(delay_, [source = source_]() {
      if (!source->stop_requested()) {
        source->request_stop();
      }
    });
  }

  void await_resume() {
    if (timer_id_ != 0) {
      ctx_.UseService<AntTimer>().CancelTimer(timer_id_);
    }
  }

 private:
  Context& ctx_;
  std::chrono::milliseconds delay_;
  std::size_t timer_id_ {0};
  CoroTaskFunc task_func_;
  HttpTask child_task_;
  std::shared_ptr<std::stop_source> source_;
  std::coroutine_handle<> parent_handle_;
};

template <typename CoroTaskFunc>
auto with_timeout(Context& ctx, std::chrono::milliseconds delay, CoroTaskFunc&& task_func) {
  return TimeoutAwaiter<CoroTaskFunc> {ctx, delay, std::forward<CoroTaskFunc>(task_func)};
}

#endif