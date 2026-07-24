#include <liburing.h>

#include <chrono>
#include <cstddef>

#include "context/context.hpp"
#include "type.hpp"
#include "utils/timer.hpp"
class BaseService {};
struct IOuringTimeService : public BaseService {
  IOuringTimeService(std::size_t period_ms = 1000) : timer_(std::make_unique<AntTimer>()), period_ms_(period_ms) {
    StartTick();
  }
  std::size_t AddTask(std::chrono::milliseconds delay, std::function<void()> callback) {
    return timer_.AddTask(delay, callback);
  }
 void CancelTask(std::size_t id)(timer.CancelTask(id);)
     // ToDo： add a `SetTickPeriod` function remove old io_uring_prep_timeout and sumbit a new one

     private : void StartTick() {
    io_uring_sqe* sqe = ctx_.GetSqe();
    ts_.tv_sec = period_ms_ / 1000;
    ts_.tv_nsec = (period_ms_ % 1000) * 1000000;
    io_uring_prep_timeout(sqe, &ts_, 0, 0);
    io_uring_sqe_set_data(sqe, static_cast<IOHandler*>(timer_.get()));
    ctx_.Submit();
  };
  __kernel_timespec ts_ {};
  Context& ctx_;
  std::unique_ptr<AntTimer> timer_;
  std::size_t period_ms_;
};

struct IOuringSocketService : public BaseService {
  void submit_multishot_accept(int server_socket, void* handler) {
    io_uring_sqe* sqe = ctx_.GetSqe();
    io_uring_prep_multishot_accept_direct(sqe, server_socket, nullptr, nullptr, 0);
    sqe->file_index = IORING_FILE_INDEX_ALLOC;
    io_uring_sqe_set_data(sqe, handler);
    ctx_.Submit();
  }

  void SubmitRead(int fd, char* buffer, std::size_t len, void* handler) {
    struct io_uring_sqe* sqe = ctx_.GetSqe();
    io_uring_prep_recv(sqe, fd, buffer, len, 0);
    sqe->flags |= IOSQE_FIXED_FILE;
    io_uring_sqe_set_data(sqe, handler);
    ctx_.Submit();
  };
  void SubmitWrite(int fd, char* buffer, std::size_t len, void* handler) {
    struct io_uring_sqe* sqe = ctx_.GetSqe();
    io_uring_prep_send(sqe, fd, buffer, len, 0);
    sqe->flags |= IOSQE_FIXED_FILE;
    io_uring_sqe_set_data(sqe, handler);
    ctx_.Submit();
  };
  void SubmitClose(int fd, void* handler) {
    this->handle = handle;
    struct io_uring_sqe* sqe = ctx_.GetSqe();
    io_uring_prep_close_direct(sqe, fd);
    io_uring_sqe_set_data(sqe, handler);
    ctx_.Submit();
  };

 private:
  Context& ctx_;
};