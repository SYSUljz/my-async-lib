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

// Task used in AntTimer service
struct Task {
  std::size_t id;
  std::chrono::steady_clock::time_point time;
  bool canceled = false;
  std::function<void()> callback;
  bool operator>(const Task& other) { return time > other.time; }
};

struct TaskComparator {
  bool operator()(const std::shared_ptr<Task>& lhs, const std::shared_ptr<Task>& rhs) const { return *lhs > *rhs; }
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
    std::coroutine_handle<> continuation {nullptr};
    void set_continuation(std::coroutine_handle<> h) { continuation = h; }

    struct FinalAwaiter {
      bool await_ready() noexcept { return false; }
      template <typename Promise>
      std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
        if (auto parent = h.promise().continuation) {
          return parent;
        }
        return std::noop_coroutine();
      }
      void await_resume() noexcept {};
    };

    std::suspend_never initial_suspend() { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }
    HttpTask get_return_object() { return HttpTask {std::coroutine_handle<promise_type>::from_promise(*this)}; }

    void return_void() {}
    void unhandled_exception() { std::terminate(); }

    void set_stop_token(std::stop_token t) { token = std::move(t); }

    std::stop_token get_stop_token() const { return token; }
  };
  std::coroutine_handle<promise_type> handle {nullptr};
};

#endif
// Todo: use Clang to check Google C++ Style Guide