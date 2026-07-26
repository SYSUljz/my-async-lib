template <typename CoroTask>
struct TimeoutAwaiter {
  bool awaite_ready() { return false; }
  void awaite_suspend(std::coroutine_handle<> handle) {
    parent_handle_ = handle;
    timer_.AddTimer(delay_, []() {

    });
    co_await task_();
  }

 private:
  Context& ctx_;
  std::chrono::milliseconds delay_;
  std::size_t timer_id_ {0};
  CoroTask task_;
  std::coroutine_handle<>& parent_handle_;
  // Todo: impl a type erase Service
  Timer& timer_;
}