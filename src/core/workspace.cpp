#include "core/workspace.h"

#include <algorithm>

namespace vide {

Status Workspace::open(const std::string& path, const OpenOptions& options) {
    auto session = std::make_unique<Session>();
    Status status = session->open(path, options);
    if (!status.ok()) {
        return status;
    }
    sessions_.push_back(std::move(session));
    active_ = static_cast<int>(sessions_.size()) - 1;
    return Status::success();
}

void Workspace::closeAll() {
    sessions_.clear();
    active_ = -1;
}

Session& Workspace::active() {
    if (active_ >= 0 && active_ < count()) {
        return *sessions_[static_cast<std::size_t>(active_)];
    }
    return scratch_;
}

const Session& Workspace::active() const {
    if (active_ >= 0 && active_ < count()) {
        return *sessions_[static_cast<std::size_t>(active_)];
    }
    return scratch_;
}

Session* Workspace::at(int index) {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return sessions_[static_cast<std::size_t>(index)].get();
}

void Workspace::setActive(int index) {
    if (index >= 0 && index < count()) {
        active_ = index;
    }
}

void Workspace::removeActive() {
    removeAt(active_);
}

void Workspace::removeAt(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    sessions_.erase(sessions_.begin() + index);
    if (sessions_.empty()) {
        active_ = -1;
    } else if (active_ > index) {
        --active_;
    } else if (active_ >= count()) {
        active_ = count() - 1;
    }
}

void Workspace::poll() {
    for (auto& session : sessions_) {
        session->poll();
    }
}

}  // namespace vide
