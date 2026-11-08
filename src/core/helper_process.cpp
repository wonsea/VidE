#include "core/helper_process.h"

#include <cstring>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vide {
namespace {

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&v),
               reinterpret_cast<const uint8_t*>(&v) + 4);
}

uint32_t readU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

}  // namespace

std::vector<uint8_t> encodeHelperMessage(const HelperMessage& message) {
    std::vector<uint8_t> out;
    appendU32(out, static_cast<uint32_t>(message.type));
    appendU32(out, static_cast<uint32_t>(message.codec));
    appendU32(out, static_cast<uint32_t>(message.width));
    appendU32(out, static_cast<uint32_t>(message.height));
    appendU32(out, static_cast<uint32_t>(message.bit_depth));
    appendU32(out, message.frame);
    appendU32(out, static_cast<uint32_t>(message.text.size()));
    out.insert(out.end(), message.text.begin(), message.text.end());
    return out;
}

bool decodeHelperMessage(const uint8_t* data, std::size_t size, HelperMessage& out) {
    if (size < 28) {
        return false;
    }
    out.type = static_cast<HelperMessageType>(readU32(data));
    out.codec = static_cast<int>(readU32(data + 4));
    out.width = static_cast<int>(readU32(data + 8));
    out.height = static_cast<int>(readU32(data + 12));
    out.bit_depth = static_cast<int>(readU32(data + 16));
    out.frame = readU32(data + 20);
    const uint32_t text_len = readU32(data + 24);
    if (28 + text_len > size) {
        return false;
    }
    out.text.assign(reinterpret_cast<const char*>(data + 28), text_len);
    return true;
}

#ifdef _WIN32

struct HelperProcess::Impl {
    PROCESS_INFORMATION pi{};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    HANDLE job = nullptr;
    std::vector<uint8_t> buffer;
};

HelperProcess::~HelperProcess() {
    terminate();
    delete impl_;
}

void HelperProcess::terminate() {
    if (impl_ == nullptr) {
        return;
    }
    if (running_ && impl_->pi.hProcess != nullptr) {
        TerminateProcess(impl_->pi.hProcess, 1);
    }
    if (impl_->read_pipe != nullptr) {
        CloseHandle(impl_->read_pipe);
        impl_->read_pipe = nullptr;
    }
    if (impl_->write_pipe != nullptr) {
        CloseHandle(impl_->write_pipe);
        impl_->write_pipe = nullptr;
    }
    if (impl_->pi.hProcess != nullptr) {
        CloseHandle(impl_->pi.hProcess);
        impl_->pi.hProcess = nullptr;
    }
    if (impl_->pi.hThread != nullptr) {
        CloseHandle(impl_->pi.hThread);
        impl_->pi.hThread = nullptr;
    }
    running_ = false;
}

Status HelperProcess::start(const std::string& executable,
                            const std::vector<std::string>& args) {
    impl_ = new Impl();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&impl_->read_pipe, &impl_->write_pipe, &sa, 0)) {
        return Status::error(StatusCode::Internal, "CreatePipe failed");
    }
    SetHandleInformation(impl_->read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = impl_->write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::string cmdline = "\"" + executable + "\"";
    for (const auto& arg : args) {
        cmdline += " \"" + arg + "\"";
    }
    std::vector<char> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back('\0');

    if (!CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
                        nullptr, &si, &impl_->pi)) {
        CloseHandle(impl_->read_pipe);
        CloseHandle(impl_->write_pipe);
        impl_->read_pipe = nullptr;
        impl_->write_pipe = nullptr;
        return Status::error(StatusCode::Internal, "CreateProcess failed for " + executable);
    }
    CloseHandle(impl_->write_pipe);
    impl_->write_pipe = nullptr;
    running_ = true;
    return Status::success();
}

bool HelperProcess::poll(std::vector<HelperMessage>& out) {
    if (impl_ == nullptr) {
        return false;
    }
    DWORD available = 0;
    while (PeekNamedPipe(impl_->read_pipe, nullptr, 0, nullptr, &available, nullptr) &&
           available > 0) {
        uint8_t chunk[4096];
        DWORD read = 0;
        if (!ReadFile(impl_->read_pipe, chunk, sizeof(chunk), &read, nullptr) || read == 0) {
            break;
        }
        impl_->buffer.insert(impl_->buffer.end(), chunk, chunk + read);
    }

    // Parse length-prefixed messages: [u32 len][payload]
    std::size_t pos = 0;
    while (impl_->buffer.size() - pos >= 4) {
        const uint32_t len = readU32(impl_->buffer.data() + pos);
        if (impl_->buffer.size() - pos - 4 < len) {
            break;
        }
        HelperMessage message;
        if (decodeHelperMessage(impl_->buffer.data() + pos + 4, len, message)) {
            out.push_back(message);
        }
        pos += 4 + len;
    }
    impl_->buffer.erase(impl_->buffer.begin(),
                        impl_->buffer.begin() + static_cast<std::ptrdiff_t>(pos));

    DWORD code = 0;
    if (GetExitCodeProcess(impl_->pi.hProcess, &code) && code != STILL_ACTIVE) {
        exit_code_ = static_cast<int>(code);
        running_ = false;
    }
    return running_ || !out.empty();
}

#else  // POSIX

struct HelperProcess::Impl {
    int read_fd = -1;
    int write_fd = -1;
    pid_t pid = -1;
    std::vector<uint8_t> buffer;
};

HelperProcess::~HelperProcess() {
    terminate();
    delete impl_;
}

void HelperProcess::terminate() {
    if (impl_ == nullptr) {
        return;
    }
    if (running_ && impl_->pid > 0) {
        kill(impl_->pid, SIGTERM);
        int status = 0;
        waitpid(impl_->pid, &status, 0);
    }
    if (impl_->read_fd >= 0) {
        close(impl_->read_fd);
        impl_->read_fd = -1;
    }
    running_ = false;
}

Status HelperProcess::start(const std::string& executable,
                            const std::vector<std::string>& args) {
    impl_ = new Impl();

    int fds[2];
    if (pipe(fds) != 0) {
        return Status::error(StatusCode::Internal, "pipe failed");
    }
    impl_->read_fd = fds[0];
    impl_->write_fd = fds[1];
    fcntl(impl_->read_fd, F_SETFL, O_NONBLOCK);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        return Status::error(StatusCode::Internal, "fork failed");
    }
    if (pid == 0) {
        close(impl_->read_fd);
        dup2(impl_->write_fd, STDERR_FILENO);
        close(impl_->write_fd);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    close(impl_->write_fd);
    impl_->write_fd = -1;
    impl_->pid = pid;
    running_ = true;
    return Status::success();
}

bool HelperProcess::poll(std::vector<HelperMessage>& out) {
    if (impl_ == nullptr) {
        return false;
    }
    uint8_t chunk[4096];
    for (;;) {
        const ssize_t n = read(impl_->read_fd, chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        impl_->buffer.insert(impl_->buffer.end(), chunk, chunk + n);
    }

    std::size_t pos = 0;
    while (impl_->buffer.size() - pos >= 4) {
        const uint32_t len = readU32(impl_->buffer.data() + pos);
        if (impl_->buffer.size() - pos - 4 < len) {
            break;
        }
        HelperMessage message;
        if (decodeHelperMessage(impl_->buffer.data() + pos + 4, len, message)) {
            out.push_back(message);
        }
        pos += 4 + len;
    }
    impl_->buffer.erase(impl_->buffer.begin(),
                        impl_->buffer.begin() + static_cast<std::ptrdiff_t>(pos));

    if (impl_->pid > 0) {
        int status = 0;
        const pid_t r = waitpid(impl_->pid, &status, WNOHANG);
        if (r == impl_->pid) {
            exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            running_ = false;
        }
    }
    return running_ || !out.empty();
}

#endif

}  // namespace vide
