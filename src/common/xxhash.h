// xxHash64 (pure C++, no dependencies) used for content-addressed caching.
#pragma once

#include <cstddef>
#include <cstdint>

namespace vide {

// Streaming xxHash64. Seed defaults to 0.
class XxHash64 {
public:
    explicit XxHash64(uint64_t seed = 0);

    void update(const void* data, std::size_t size);
    uint64_t digest() const;

private:
    uint64_t seed_;
    uint64_t state_[4];
    uint64_t total_ = 0;
    uint8_t buffer_[32] = {};
    std::size_t buffer_size_ = 0;
};

}  // namespace vide
