#ifndef ANT_AWAITER_HPP
#define ANT_AWAITER_HPP

#include <liburing.h>

#include <chrono>
#include <coroutine>
#include <exception>

#include "ant_server/context/context.hpp"
#include "ant_server/context/service.hpp"
#include "ant_server/type.hpp"

struct DetachedTask {
  struct promise_type {
    DetachedTask get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
};

struct BaseAwaiter : public IOHandler {
  explicit BaseAwaiter(Context& ctx)
      : socket_service_(ctx.UseService<IOuringSocketService>()) {}
  std::coroutine_handle<> handle;
  int res{0};
  IOuringSocketService& socket_service_;

  void on_complete(int res, uint32_t flags) override {
    this->res = res;
    if (handle) {
      handle.resume();
    }
  }
};

struct ReadAwaiter : public BaseAwaiter {
  ReadAwaiter(Context& ctx, int client_fd, char* buffer, int buffer_size)
      : BaseAwaiter(ctx), fd_(client_fd), buffer_(buffer), len_(buffer_size) {}
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    socket_service_.SubmitRead(fd_, buffer_, len_, static_cast<IOHandler*>(this));
  }
  int await_resume() { return this->res; }

 private:
  int fd_;
  char* buffer_;
  std::size_t len_;
};

struct WriteAwaiter : public BaseAwaiter {
  WriteAwaiter(Context& ctx, int client_fd, char* buffer, int buffer_size)
      : BaseAwaiter(ctx), fd_(client_fd), buffer_(buffer), len_(buffer_size) {}
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    socket_service_.SubmitWrite(fd_, buffer_, len_, static_cast<IOHandler*>(this));
  }
  int await_resume() { return this->res; }

 private:
  int fd_;
  char* buffer_;
  std::size_t len_;
};

struct CloseAwaiter : public BaseAwaiter {
  CloseAwaiter(Context& ctx, int client_fd)
      : BaseAwaiter(ctx), fd_(client_fd) {}
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    socket_service_.SubmitClose(fd_, static_cast<IOHandler*>(this));
  }
  int await_resume() { return this->res; }

 private:
  int fd_;
};

#endif