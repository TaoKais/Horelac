#include "horelac/services/RenderQueue.hpp"
#include <algorithm>
#include <ranges>
namespace horelac::services {
RenderQueue::RenderQueue(Callback callback,std::chrono::milliseconds debounce):callback_(std::move(callback)),debounce_(debounce),worker_([this](std::stop_token t){run(t);}){}
RenderQueue::~RenderQueue(){stop();}
void RenderQueue::mark_dirty(domain::CalendarId id,std::int64_t revision){std::scoped_lock lock(mutex_);pending_[id]={revision,std::chrono::steady_clock::now()+debounce_};wake_.notify_all();}
void RenderQueue::stop(){if(worker_.joinable()){worker_.request_stop();wake_.notify_all();worker_.join();}}
void RenderQueue::run(std::stop_token token){std::unique_lock lock(mutex_);while(!token.stop_requested()){if(pending_.empty()){wake_.wait(lock,token,[this]{return !pending_.empty();});continue;}auto selected=std::ranges::min_element(pending_,{},[](const auto& item){return item.second.due;});const auto due=selected->second.due;if(wake_.wait_until(lock,token,due,[&]{return std::chrono::steady_clock::now()>=due;})){if(token.stop_requested())break;const auto id=selected->first,revision=selected->second.revision;pending_.erase(selected);lock.unlock();try{callback_(id,revision);}catch(...){/* The caller logs and may enqueue a retry. */}lock.lock();}}}
} // namespace horelac::services
