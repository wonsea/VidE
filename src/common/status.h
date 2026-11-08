// Error handling primitives used across module boundaries.
//
// VidE does not throw across adapter boundaries; failures are reported through
// Status / Result<T>.
#pragma once

#include <string>
#include <utility>

namespace vide {

enum class StatusCode {
    Ok = 0,
    InvalidArgument,
    IoError,
    Unsupported,
    ParseError,
    DecodeError,
    Internal,
};

class Status {
public:
    Status() = default;
    Status(StatusCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    static Status success() { return Status{}; }
    static Status error(StatusCode code, std::string message) {
        return Status{code, std::move(message)};
    }

    bool ok() const { return code_ == StatusCode::Ok; }
    explicit operator bool() const { return ok(); }

    StatusCode code() const { return code_; }
    const std::string& message() const { return message_; }

private:
    StatusCode code_ = StatusCode::Ok;
    std::string message_;
};

template <typename T>
class Result {
public:
    Result(T value) : status_(Status::success()), value_(std::move(value)) {}
    Result(Status status) : status_(std::move(status)) {}

    bool ok() const { return status_.ok(); }
    explicit operator bool() const { return ok(); }

    const Status& status() const { return status_; }

    T& value() { return value_; }
    const T& value() const { return value_; }

    // Returns the contained value, or `fallback` when the result is an error.
    T valueOr(T fallback) const { return status_.ok() ? value_ : std::move(fallback); }

private:
    Status status_;
    T value_{};
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Status status) : status_(std::move(status)) {}

    bool ok() const { return status_.ok(); }
    explicit operator bool() const { return ok(); }

    const Status& status() const { return status_; }

private:
    Status status_;
};

inline Result<void> Ok() { return Result<void>{}; }
inline Result<void> Fail(StatusCode code, std::string message) {
    return Result<void>{Status::error(code, std::move(message))};
}

}  // namespace vide
