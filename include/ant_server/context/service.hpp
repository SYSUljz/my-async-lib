#ifndef ANT_SERVICE_HPP
#define ANT_SERVICE_HPP

#include <liburing.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

#include "ant_server/context/context.hpp"
#include "ant_server/type.hpp"

class BaseService {
 public:
  virtual ~BaseService() = default;
};

struct IOuringSocketService : public BaseService {
  explicit IOuringSocketService(Context& ctx) : ctx_(ctx) {}

  void submit_multishot_accept(int server_socket, void* handler) {
    io_uring_sqe* sqe = ctx_.GetSqe();
    io_uring_prep_multishot_accept_direct(sqe, server_socket, nullptr, nullptr, 0);
    sqe->file_index = IORING_FILE_INDEX_ALLOC;
    io_uring_sqe_set_data(sqe, handler);
    ctx_.Submit();
  }

  void SubmitRead(int fd, char* buffer, std::size_t len, void* handler, bool is_fixed = true) {
    struct io_uring_sqe* sqe = ctx_.GetSqe();
    io_uring_prep_recv(sqe, fd, buffer, len, 0);
    if (is_fixed) {
      sqe->flags |= IOSQE_FIXED_FILE;
    }
    io_uring_sqe_set_data(sqe, handler);
    ctx_.Submit();
  }

  void SubmitWrite(int fd, char* buffer, std::size_t len, void* handler, bool is_fixed = true) {
    struct io_uring_sqe* sqe = ctx_.GetSqe();
    io_uring_prep_send(sqe, fd, buffer, len, 0);
    if (is_fixed) {
      sqe->flags |= IOSQE_FIXED_FILE;
    }
    io_uring_sqe_set_data(sqe, handler);
    ctx_.Submit();
  }

  void SubmitClose(int fd, void* handler, bool is_fixed = true) {
    struct io_uring_sqe* sqe = ctx_.GetSqe();
    if (is_fixed) {
      io_uring_prep_close_direct(sqe, fd);
    } else {
      io_uring_prep_close(sqe, fd);
    }
    io_uring_sqe_set_data(sqe, handler);
    ctx_.Submit();
  }

  void SubmitCancel(void* handler) {
    struct io_uring_sqe* sqe = ctx_.GetSqe();
    io_uring_prep_cancel(sqe, handler, 0);
    // no callback for cancel SQE itself
    io_uring_sqe_set_data(sqe, nullptr);
    ctx_.Submit();
  }

 private:
  Context& ctx_;
};

struct IOuringTimeService : public BaseService {
  explicit IOuringTimeService(Context& ctx, std::size_t period_ms = 1000) : ctx_(ctx), period_ms_(period_ms) {}

  void StartTick(IOHandler* handler) {
    io_uring_sqe* sqe = ctx_.GetSqe();
    ts_.tv_sec = period_ms_ / 1000;
    ts_.tv_nsec = (period_ms_ % 1000) * 1000000;
    io_uring_prep_timeout(sqe, &ts_, 0, 0);
    io_uring_sqe_set_data(sqe, handler);
    ctx_.Submit();
  }

 private:
  __kernel_timespec ts_ {};
  Context& ctx_;
  std::size_t period_ms_;
};

#endif