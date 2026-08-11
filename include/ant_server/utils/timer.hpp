#ifndef ANT_TIMER_HPP
#define ANT_TIMER_HPP

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>

#include "ant_server/context/context.hpp"
#include "ant_server/context/service.hpp"
#include "ant_server/type.hpp"

// Todo: impl a iterable version of priority_queue so we can store Task in queue rather than shared_ptr<Task> that's CPU
// cache affinity
class AntTimer : public BaseService, public IOHandler {
 public:
  explicit AntTimer(Context& ctx, std::size_t period_ms = 100)
      : ctx_(ctx), period_ms_(period_ms), service_(ctx_.UseService<IOuringTimeService>()) {
    start();
  }

  std::size_t AddTimer(std::chrono::milliseconds delay, std::function<void()> callback) {
    auto task = std::make_shared<Task>();
    task->id = next_id_++;
    task->time = std::chrono::steady_clock::now() + delay;
    task->callback = std::move(callback);
    task->canceled = false;

    timer_queue_.push(task);
    task_map_[task->id] = task;
    return task->id;
  }

  void on_complete() override {
    tick();
    start();
  }

  void CancelTimer(std::size_t id) {
    auto it = task_map_.find(id);
    if (it != task_map_.end()) {
      it->second->canceled = true;
      task_map_.erase(it);
    }
  }

  void tick() {
    auto now = std::chrono::steady_clock::now();
    while (!timer_queue_.empty()) {
      auto task = timer_queue_.top();

      if (task->time > now) {
        break;
      }

      timer_queue_.pop();
      task_map_.erase(task->id);

      if (!task->canceled && task->callback) {
        task->callback();
      }
    }
  }

 private:
  void start() { service_.StartTick(static_cast<IOHandler*>(this)); }

  Context& ctx_;
  std::size_t next_id_ {0};
  std::size_t period_ms_;
  std::priority_queue<std::shared_ptr<Task>, std::vector<std::shared_ptr<Task>>, TaskComparator> timer_queue_;
  std::unordered_map<std::size_t, std::shared_ptr<Task>> task_map_;
  IOuringTimeService& service_;
};

#endif
