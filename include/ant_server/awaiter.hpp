#include <liburing.h>

#include <coroutine>
#include <exception>

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
struct BaseAwaiter {
  std::coroutine_handle<> handle;
  int res;
};

struct ReadAwaiter : public BaseAwaiter {
  int fd;
  io_uring& ring;
  char* buffer;
  int len;
  ReadAwaiter(io_uring& ring, int client_index, char* buffer, int buffer_size)
      : ring(ring), fd(client_index), buffer(buffer), len(buffer_size) {};
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_recv(sqe, fd, buffer, len, 0);
    sqe->flags |= IOSQE_FIXED_FILE;
    io_uring_sqe_set_data(sqe, this);
    io_uring_submit(&ring);
  }

  int await_resume() { return this->res; }
};
struct WriteAwaiter : public BaseAwaiter {
  int fd;
  io_uring& ring;
  char* buffer;
  size_t len;
  WriteAwaiter(io_uring& ring, int client_index, char* buffer, int buffer_size)
      : ring(ring), fd(client_index), buffer(buffer), len(buffer_size) {};
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_send(sqe, fd, buffer, len, 0);
    sqe->flags |= IOSQE_FIXED_FILE;
    io_uring_sqe_set_data(sqe, this);
    io_uring_submit(&ring);
  }

  int await_resume() { return this->res; }
};
struct CloseAwaiter : public BaseAwaiter {
  int fd;
  io_uring& ring;
  CloseAwaiter(io_uring& ring, int client_index) : ring(ring), fd(client_index) {};
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    this->handle = handle;
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_close_direct(sqe, fd);
    io_uring_sqe_set_data(sqe, this);
    io_uring_submit(&ring);
  }

  int await_resume() { return this->res; }
};