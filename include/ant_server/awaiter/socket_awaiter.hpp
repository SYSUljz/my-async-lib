#ifndef ANT_AWAITER_HPP
#define ANT_AWAITER_HPP

#include <liburing.h>

#include <chrono>
#include <coroutine>
#include <exception>

#include "ant_server/context/context.hpp"
#include "ant_server/context/service.hpp"
#include "ant_server/type.hpp"

struct BaseAwaiter : public IOHandler {
  explicit BaseAwaiter(Context& ctx) : socket_service_(ctx.UseService<IOuringSocketService>()) {}
  std::coroutine_handle<> handle;
  int res {0};
  IOuringSocketService& socket_service_;
  std::stop_token token_;
  std::atomic<CancelState> state_;
  std::optional<std::stop_callback<std::function<void()>>> cb_;

  void bind_stop_callback() {
    if (!token_.stop_possible()) return;
    cb_.emplace(token_, [this] {
      CancelState expected = CancelState::pending;
      if (state_.compare_exchange_strong(expected, CancelState::canceled, std::memory_order_acq_rel)) {
        on_cancel();
      }
    });
  }
  virtual void on_cancel() {};
  void on_complete(int res, uint32_t flags) override {
    this->res = res;
    if (handle) {
      handle.resume();
    }
  }
};

struct ReadAwaiter : public BaseAwaiter {
  ReadAwaiter(Context& ctx, int client_fd, char* buffer, int buffer_size, bool is_fixed = true)
      : BaseAwaiter(ctx), fd_(client_fd), buffer_(buffer), len_(buffer_size), is_fixed_(is_fixed) {}
  bool await_ready() { return false; }

  template <typename PromiseType>
  void await_suspend(std::coroutine_handle<PromiseType> handle) {
    this->handle = handle;
    if constexpr (requires(PromiseType& p) { p.get_stop_token(); }) {
      this->token_ = handle.promise().get_stop_token();
    }
    socket_service_.SubmitRead(fd_, buffer_, len_, static_cast<IOHandler*>(this), is_fixed_);
    bind_stop_callback();
  }
  int await_resume() { return this->res; }
  void on_cancel() override { socket_service_.SubmitCancel(this); }

 private:
  int fd_;
  char* buffer_;
  std::size_t len_;
  bool is_fixed_;
};

struct WriteAwaiter : public BaseAwaiter {
  WriteAwaiter(Context& ctx, int client_fd, char* buffer, int buffer_size, bool is_fixed = true)
      : BaseAwaiter(ctx), fd_(client_fd), buffer_(buffer), len_(buffer_size), is_fixed_(is_fixed) {}
  bool await_ready() { return false; }

  template <typename PromiseType>
  void await_suspend(std::coroutine_handle<PromiseType> handle) {
    this->handle = handle;
    if constexpr (requires(PromiseType& p) { p.get_stop_token(); }) {
      this->token_ = handle.promise().get_stop_token();
    }
    socket_service_.SubmitWrite(fd_, buffer_, len_, static_cast<IOHandler*>(this), is_fixed_);
    bind_stop_callback();
  }
  int await_resume() { return this->res; }
  void on_cancel() override { socket_service_.SubmitCancel(this); }

 private:
  int fd_;
  char* buffer_;
  std::size_t len_;
  bool is_fixed_;
};

struct CloseAwaiter : public BaseAwaiter {
  CloseAwaiter(Context& ctx, int client_fd, bool is_fixed = true)
      : BaseAwaiter(ctx), fd_(client_fd), is_fixed_(is_fixed) {}
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    socket_service_.SubmitClose(fd_, static_cast<IOHandler*>(this), is_fixed_);
  }
  int await_resume() { return this->res; }

 private:
  int fd_;
  bool is_fixed_;
};

#endif