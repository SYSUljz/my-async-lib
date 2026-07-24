#include <liburing.h>

#include <coroutine>
#include <exception>

#include "context.hpp"
#include "context/service.hpp"
#include "type.hpp"
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
  explicit BaseAwaiter(Context& ctx) : service_(ctx.UseService<IOuringSocketService>()) {}
  std::coroutine_handle<> handle;
  int res;
  size_t timeout;
  std::chrono::milliseconds timeout_delay;
  IOuringSocketService& socket_service;
  IOuringTimeService& time_service;
  void on_complete(int res, uint32_t flags) override {
    this->res = res;
    if (handle) {
      handle.resume();
    }
  }
};

struct ReadAwaiter : public BaseAwaiter {
  ReadAwaiter(Context& ctx, int client_index, char* buffer, int buffer_size)
      : ctx_(ctx), BaseAwaiter(ctx), fd_(client_index), buffer_(buffer), len_(buffer_size) {};
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    socket_service_.SubmitRead(fd, buffer, len, static_cast<IOHandler*>(this));
    // add a lazy init closeawaiter here
    // time_service_.AddTask(timeout_delay, )
  }

  int await_resume() {
    // time_service_.CancelTask(timer_id_);
    return this->res;
  }

 private:
  int fd_;
  Context& ctx_;
  char* buffer_;
  std::size_t len_;
  std::size_t timer_id_ {0};
};
struct WriteAwaiter : public BaseAwaiter {
  WriteAwaiter(io_uring& ring, int client_index, char* buffer, int buffer_size)
      : ring(ring), fd(client_index), buffer(buffer), len(buffer_size) {};
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    socket_service_.SubmitWrite(fd, buffer, len, static_cast<IOHandler*>(this));
    // add a lazy init closeawaiter here
    // time_service_.AddTask(timeout_delay, )
  }

  int await_resume() { return this->res; }

 private:
  int fd_;
  Context& ctx_;
  char* buffer_;
  std::size_t len_;
  std::size_t timer_id_ {0};
};
struct CloseAwaiter : public BaseAwaiter {
  int fd;
  io_uring& ring;
  CloseAwaiter(io_uring& ring, int client_index) : ring(ring), fd(client_index) {};
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    socket_service_.SubmitClose(fd, static_cast<IOHandler*>(this));
  }

  int await_resume() { return this->res; }

 private:
  int fd_;
  Context& ctx_;
  std::size_t timer_id_ {0};
};