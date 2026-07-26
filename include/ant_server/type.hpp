#ifndef ANT_TYPE
#define ANT_TYPE

#include <coroutine>
#include <exception>
#include <stop_token>

enum EventType { EVENT_ACCEPT, EVENT_READ, EVENT_WRITE, EVENT_CLOSE, EVENT_TIMER };

struct IOHandler {
  virtual void on_complete(int res, uint32_t flags) = 0;
  virtual ~IOHandler() = default;
};

enum class CancelState { pending, canceled };
struct DetachedTask {
  struct promise_type {
    DetachedTask get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
};
struct HttpTask {
  struct promise_type {
    std::stop_token token;

    HttpTask get_return_object() { return HttpTask {std::coroutine_handle<promise_type>::from_promise(*this)}; }

    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }

    void set_stop_token(std::stop_token t) { token = std::move(t); }

    std::stop_token get_stop_token() const { return token; }
  };
  std::coroutine_handle<promise_type> handle {nullptr};
};

#endif
// Todo: use Clang to check Google C++ Style Guide