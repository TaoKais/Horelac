#pragma once
#include "horelac/domain/Models.hpp"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <thread>
#include <map>
#include <mutex>
namespace horelac::services {
class RenderQueue {
  public:
    using Callback=std::function<void(domain::CalendarId,std::int64_t)>;
    explicit RenderQueue(Callback callback,std::chrono::milliseconds debounce=std::chrono::seconds(3));
    ~RenderQueue();
    void mark_dirty(domain::CalendarId id,std::int64_t revision);
    void stop();
  private:
    struct Pending{std::int64_t revision{};std::chrono::steady_clock::time_point due;};
    Callback callback_;std::chrono::milliseconds debounce_;std::mutex mutex_;std::condition_variable_any wake_;std::map<domain::CalendarId,Pending> pending_;std::jthread worker_;
    void run(std::stop_token token);
};
} // namespace horelac::services

