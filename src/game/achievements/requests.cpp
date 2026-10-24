#include "game/achievements/requests.h"

namespace disruptor::achievements {

Requests::Requests(std::unique_ptr<Transport> transport)
    : transport_(std::move(transport)), worker_([this] { run(); }) {}

void Requests::close() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
}

Requests::~Requests() { close(); }

void Requests::send(Request request, Done done) {
    {
        const std::lock_guard lock(mutex_);
        if (stopping_) return;
        queue_.emplace_back(std::move(request), std::move(done));
    }
    wake_.notify_one();
}

void Requests::run() {
    for (;;) {
        std::pair<Request, Done> next;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;
            next = std::move(queue_.front());
            queue_.pop_front();
        }
        const Response response = transport_->perform(next.first);
        if (next.second) next.second(response);
    }
}

} // namespace disruptor::achievements
