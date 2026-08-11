#include <unistd.h>

#include <cerrno>
#include <coroutine>
#include <stop_token>

#include <gtest/gtest.h>
#include <sys/socket.h>

#include "ant_server/awaiter/socket_awaiter.hpp"
#include "ant_server/context/context.hpp"
#include "ant_server/type.hpp"

// Coroutine task type with initial_suspend = suspend_always to allow setting token into promise
struct TestCancelTask {
  struct promise_type {
    std::stop_token token;

    TestCancelTask get_return_object() {
      return TestCancelTask {std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }

    void set_stop_token(std::stop_token t) { token = std::move(t); }
    std::stop_token get_stop_token() const { return token; }
  };

  std::coroutine_handle<promise_type> handle {nullptr};
};

static TestCancelTask async_read_coro(Context& ctx, int fd, int& out_res, bool& finished) {
  char buf[1024];
  out_res = co_await ReadAwaiter(ctx, fd, buf, sizeof(buf), /*is_fixed=*/false);
  finished = true;
}

TEST(ReadAwaiterTest, ManualStopRequestTriggersECanceledAndSafelyExits) {
  // 1. Create Context and socket pair
  Context ctx(256);
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  int read_result = 0;
  bool coro_finished = false;

  // 2. Create std::stop_source and coroutine instance
  std::stop_source stop_source;
  TestCancelTask task = async_read_coro(ctx, fds[0], read_result, coro_finished);

  // 3. Pass token to promise
  task.handle.promise().set_stop_token(stop_source.get_token());

  // 4. Resume coroutine so it enters ReadAwaiter::await_suspend
  task.handle.resume();
  EXPECT_FALSE(coro_finished);

  // 5. Manually call source.request_stop()
  stop_source.request_stop();

  // 6. Process events in io_uring
  int processed = ctx.ProcessEvents(1);
  EXPECT_GT(processed, 0);

  // 7. Verify ReadAwaiter returned -ECANCELED and coroutine finished safely
  EXPECT_TRUE(coro_finished);
  EXPECT_EQ(read_result, -ECANCELED);

  close(fds[0]);
  close(fds[1]);
}

TEST(ReadAwaiterTest, PreRequestedStopTriggersECanceled) {
  Context ctx(256);
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  int read_result = 0;
  bool coro_finished = false;

  std::stop_source stop_source;
  stop_source.request_stop();

  TestCancelTask task = async_read_coro(ctx, fds[0], read_result, coro_finished);
  task.handle.promise().set_stop_token(stop_source.get_token());
  task.handle.resume();

  ctx.ProcessEvents(1);

  EXPECT_TRUE(coro_finished);
  EXPECT_EQ(read_result, -ECANCELED);

  close(fds[0]);
  close(fds[1]);
}
