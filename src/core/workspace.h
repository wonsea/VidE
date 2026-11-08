// Workspace: owns the list of open streams (R6 multi-stream support) and which
// one is active. UI panels draw against Workspace::active(), so a workspace with
// no open stream behaves like an empty Session.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/status.h"
#include "core/session.h"

namespace vide {

class Workspace {
public:
    Workspace() = default;

    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    // Opens `path` as a new stream and makes it active. The stream is only
    // added when the open succeeds.
    Status open(const std::string& path, const OpenOptions& options = {});

    // Closes and removes every stream.
    void closeAll();

    int count() const { return static_cast<int>(sessions_.size()); }
    bool empty() const { return sessions_.empty(); }

    // Active stream, or a persistent empty Session when nothing is open.
    Session& active();
    const Session& active() const;

    Session* at(int index);
    int activeIndex() const { return active_; }
    void setActive(int index);
    void removeActive();
    void removeAt(int index);

    // Advances every open stream (helpers / thumbnails keep progressing in the
    // background even while another stream is active).
    void poll();

private:
    std::vector<std::unique_ptr<Session>> sessions_;
    int active_ = -1;
    Session scratch_;  // returned by active() when no stream is open
};

}  // namespace vide
