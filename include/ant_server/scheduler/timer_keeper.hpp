#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "ant_server/constants.hpp"
#include "ant_server/type.hpp"

// ============================================================================
// TimerKeeper: High-Performance Background Timer Keeper & Timing Wheel
//
// 1. Decoupled from IO Context: dedicated timer thread, zero io_uring interference.
// 2. Intrusive Linked List & Hashed Timing Wheel:
//    - O(1) timer insertion and O(1) bucket draining.
//    - Uses intrusive TaskNode chaining for zero-allocation coroutine sleep_for.
// 3. 100% Unified Task Model:
//    - Both coroutines and lambda callbacks are represented as TaskNode*.
//    - Lambda callbacks use SBO-backed TypeErasedTask with auto-delete.
// 4. Cache Line Alignment: atomic ID generation is aligned to prevent False Sharing.
// 5. Directly schedules expired tasks onto the Worker Executor via P2C.
// ============================================================================
class TimerKeeper {
 public:
  static constexpr size_t kWheelSlots = ant_server::constants::kDefaultTimingWheelSlots;
  static constexpr size_t kSlotMask = ant_server::constants::kDefaultTimingWheelSlotMask;
  static constexpr int64_t kTickMs = ant_server::constants::kDefaultTimingWheelTickMs;

  struct TimerEntry {
    uint64_t id {0};
    std::chrono::steady_clock::time_point expire_at;
    TaskNode* task_node {nullptr};
    bool owns_task_node {false};
    bool canceled {false};
    TimerEntry* next {nullptr};
  };

  explicit TimerKeeper(Executor& executor) : executor_(executor) {
    buckets_.resize(kWheelSlots, nullptr);
    start_time_ = std::chrono::steady_clock::now();
    last_tick_ = 0;
  }

  ~TimerKeeper() { Stop(); }

  void Start() {
    if (!running_.exchange(true, std::memory_order_acq_rel)) {
      thread_ = std::thread([this]() { this->Run(); });
    }
  }

  void Stop() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
      {
        absl::MutexLock lock(&mu_);
        cv_.Signal();
      }
      if (thread_.joinable()) {
        thread_.join();
      }
    }
  }

  // Zero-allocation registration for CoroTask / TaskNode
  uint64_t AddTimer(std::chrono::milliseconds delay, TaskNode* task_node) {
    if (!task_node) return 0;
    return add_internal(delay, task_node, /*owns_task_node=*/false);
  }

  // Lambda / callable callback registration using SBO TypeErasedTask
  template <typename F>
    requires(!std::is_convertible_v<std::decay_t<F>, TaskNode*>)
  uint64_t AddTimer(std::chrono::milliseconds delay, F&& callback) {
    auto* task = new TypeErasedTask(std::forward<F>(callback), /*auto_delete=*/true);
    return add_internal(delay, task, /*owns_task_node=*/true);
  }

  void CancelTimer(uint64_t id) {
    if (id == 0) return;
    absl::MutexLock lock(&mu_);
    auto it = task_map_.find(id);
    if (it != task_map_.end()) {
      it->second->canceled = true;
      task_map_.erase(it);
    }
  }

 private:
  uint64_t add_internal(std::chrono::milliseconds delay, TaskNode* task_node, bool owns_task_node) {
    auto* entry = new TimerEntry();
    entry->id = next_id_.fetch_add(1, std::memory_order_relaxed);
    entry->expire_at = std::chrono::steady_clock::now() + delay;
    entry->task_node = task_node;
    entry->owns_task_node = owns_task_node;
    entry->canceled = false;

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(entry->expire_at - start_time_).count();
    int64_t target_tick = elapsed_ms > 0 ? elapsed_ms / kTickMs : 0;
    size_t slot = static_cast<size_t>(target_tick) & kSlotMask;

    {
      absl::MutexLock lock(&mu_);
      entry->next = buckets_[slot];
      buckets_[slot] = entry;
      task_map_[entry->id] = entry;

      cv_.Signal();
    }
    return entry->id;
  }

  void Run() {
    while (running_.load(std::memory_order_relaxed)) {
      TaskNode* expired_head = nullptr;

      {
        absl::MutexLock lock(&mu_);
        if (!running_.load(std::memory_order_relaxed)) break;

        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
        int64_t current_tick = elapsed_ms > 0 ? elapsed_ms / kTickMs : 0;

        // Process all ticks from last_tick_ up to current_tick
        while (last_tick_ <= current_tick) {
          size_t slot = static_cast<size_t>(last_tick_) & kSlotMask;
          TimerEntry* curr = buckets_[slot];
          buckets_[slot] = nullptr;

          TimerEntry* remaining_head = nullptr;

          while (curr) {
            TimerEntry* next = curr->next;
            curr->next = nullptr;

            if (curr->canceled) {
              if (curr->owns_task_node && curr->task_node) {
                delete curr->task_node;
              }
              delete curr;
            } else if (curr->expire_at <= now) {
              task_map_.erase(curr->id);
              if (curr->task_node) {
                // Intrusively prepend expired TaskNode into single-linked list
                curr->task_node->next = expired_head;
                expired_head = curr->task_node;
              }
              delete curr;
            } else {
              // Multi-round timer: re-chain to remaining list for this slot
              curr->next = remaining_head;
              remaining_head = curr;
            }
            curr = next;
          }

          buckets_[slot] = remaining_head;
          last_tick_++;
        }

        // Sleep until the next tick boundary if no timers expired
        if (expired_head == nullptr) {
          cv_.WaitWithTimeout(&mu_, absl::Milliseconds(kTickMs));
          continue;
        }
      }

      // Dispatch all expired tasks to Worker Executor outside the lock
      while (expired_head) {
        TaskNode* next = expired_head->next;
        expired_head->next = nullptr;
        executor_.schedule(expired_head);
        expired_head = next;
      }
    }
  }

  Executor& executor_;
  absl::Mutex mu_;
  absl::CondVar cv_;
  std::vector<TimerEntry*> buckets_;
  absl::flat_hash_map<uint64_t, TimerEntry*> task_map_;
  std::chrono::steady_clock::time_point start_time_;
  int64_t last_tick_ {0};

  // Align high-frequency atomic variables to distinct cache lines to prevent False Sharing
  alignas(ant_server::constants::kCacheLineSize) std::atomic<uint64_t> next_id_ {1};
  alignas(ant_server::constants::kCacheLineSize) std::atomic<bool> running_ {false};
  std::thread thread_;
};
