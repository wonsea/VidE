#include "common/xxhash.h"

#include <cstring>

namespace vide {
namespace {

constexpr uint64_t kP1 = 11400714785074694791ULL;
constexpr uint64_t kP2 = 14029467366897019727ULL;
constexpr uint64_t kP3 = 1609587929392839161ULL;
constexpr uint64_t kP4 = 9650029242287828579ULL;
constexpr uint64_t kP5 = 2870177450012600261ULL;

inline uint64_t rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

inline uint64_t read64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline uint32_t read32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline uint64_t round(uint64_t acc, uint64_t input) {
    acc += input * kP2;
    acc = rotl(acc, 31);
    acc *= kP1;
    return acc;
}

inline uint64_t mergeRound(uint64_t acc, uint64_t val) {
    val = round(0, val);
    acc ^= val;
    acc = acc * kP1 + kP4;
    return acc;
}

}  // namespace

XxHash64::XxHash64(uint64_t seed) : seed_(seed) {
    state_[0] = seed + kP1 + kP2;
    state_[1] = seed + kP2;
    state_[2] = seed;
    state_[3] = seed - kP1;
}

void XxHash64::update(const void* data, std::size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    total_ += size;

    if (buffer_size_ + size < 32) {
        std::memcpy(buffer_ + buffer_size_, p, size);
        buffer_size_ += size;
        return;
    }

    if (buffer_size_ > 0) {
        const std::size_t fill = 32 - buffer_size_;
        std::memcpy(buffer_ + buffer_size_, p, fill);
        state_[0] = round(state_[0], read64(buffer_));
        state_[1] = round(state_[1], read64(buffer_ + 8));
        state_[2] = round(state_[2], read64(buffer_ + 16));
        state_[3] = round(state_[3], read64(buffer_ + 24));
        p += fill;
        size -= fill;
        buffer_size_ = 0;
    }

    while (size >= 32) {
        state_[0] = round(state_[0], read64(p));
        state_[1] = round(state_[1], read64(p + 8));
        state_[2] = round(state_[2], read64(p + 16));
        state_[3] = round(state_[3], read64(p + 24));
        p += 32;
        size -= 32;
    }

    if (size > 0) {
        std::memcpy(buffer_, p, size);
        buffer_size_ = size;
    }
}

uint64_t XxHash64::digest() const {
    uint64_t h;
    if (total_ >= 32) {
        h = rotl(state_[0], 1) + rotl(state_[1], 7) + rotl(state_[2], 12) + rotl(state_[3], 18);
        h = mergeRound(h, state_[0]);
        h = mergeRound(h, state_[1]);
        h = mergeRound(h, state_[2]);
        h = mergeRound(h, state_[3]);
    } else {
        h = seed_ + kP5;
    }

    h += total_;

    const uint8_t* p = buffer_;
    std::size_t remaining = buffer_size_;
    while (remaining >= 8) {
        h ^= round(0, read64(p));
        h = rotl(h, 27) * kP1 + kP4;
        p += 8;
        remaining -= 8;
    }
    if (remaining >= 4) {
        h ^= static_cast<uint64_t>(read32(p)) * kP1;
        h = rotl(h, 23) * kP2 + kP3;
        p += 4;
        remaining -= 4;
    }
    while (remaining > 0) {
        h ^= static_cast<uint64_t>(*p) * kP5;
        h = rotl(h, 11) * kP1;
        ++p;
        --remaining;
    }

    h ^= h >> 33;
    h *= kP2;
    h ^= h >> 29;
    h *= kP3;
    h ^= h >> 32;
    return h;
}

}  // namespace vide
