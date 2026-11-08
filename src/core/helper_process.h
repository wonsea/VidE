// Spawns a reference-decoder helper process and communicates with it over a
// pipe. The helper writes progress messages (length-prefixed binary) while it
// fills the cache; bulk data never travels through the pipe.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/status.h"

namespace vide {

// Messages pushed by a helper.
enum class HelperMessageType : uint32_t {
    Hello = 1,     // {codec, width, height, bit_depth}
    Progress = 2,  // {frames_ready}
    Done = 3,
    Error = 4,     // {utf8 message}
    Ready = 5,     // {frame_index} a single frame became available
};

struct HelperMessage {
    HelperMessageType type = HelperMessageType::Error;
    int codec = 0;
    int width = 0;
    int height = 0;
    int bit_depth = 0;
    uint32_t frame = 0;
    std::string text;
};

// Serialisation used by helper executables to emit messages.
std::vector<uint8_t> encodeHelperMessage(const HelperMessage& message);
bool decodeHelperMessage(const uint8_t* data, std::size_t size, HelperMessage& out);

// A running helper process.
class HelperProcess {
public:
    HelperProcess() = default;
    ~HelperProcess();

    HelperProcess(const HelperProcess&) = delete;
    HelperProcess& operator=(const HelperProcess&) = delete;

    // Launches `executable` with the given arguments, connected via a pipe.
    Status start(const std::string& executable, const std::vector<std::string>& args);

    // Non-blocking drain of available messages. Returns false when the helper
    // has exited and no messages remain.
    bool poll(std::vector<HelperMessage>& out);

    bool running() const { return running_; }
    int exitCode() const { return exit_code_; }
    void terminate();

private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool running_ = false;
    int exit_code_ = 0;
};

}  // namespace vide
