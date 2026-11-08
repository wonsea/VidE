// MSB-first bit reader, as required by H.26x / MPEG bitstream syntax.
#pragma once

#include <cstddef>
#include <cstdint>

namespace vide {

class BitReader {
public:
    BitReader(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool good() const { return !error_; }
    std::size_t bitPosition() const { return bit_pos_; }
    std::size_t bitsRemaining() const { return size_ * 8 > bit_pos_ ? size_ * 8 - bit_pos_ : 0; }

    uint32_t readBit() {
        if (bit_pos_ >= size_ * 8) {
            error_ = true;
            return 0;
        }
        const std::size_t byte = bit_pos_ >> 3;
        const int shift = 7 - static_cast<int>(bit_pos_ & 7);
        ++bit_pos_;
        return (data_[byte] >> shift) & 1u;
    }

    uint32_t readBits(int count) {
        uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            value = (value << 1) | readBit();
        }
        return value;
    }

    bool readFlag() { return readBit() != 0; }

    // Unsigned Exp-Golomb (ue(v)).
    uint32_t readUE() {
        int leading_zeros = 0;
        while (good() && readBit() == 0) {
            ++leading_zeros;
            if (leading_zeros > 31) {
                error_ = true;
                return 0;
            }
        }
        if (leading_zeros == 0) {
            return 0;
        }
        const uint32_t suffix = readBits(leading_zeros);
        return ((1u << leading_zeros) - 1u) + suffix;
    }

    // Signed Exp-Golomb (se(v)).
    int32_t readSE() {
        const uint32_t code = readUE();
        const int32_t sign = (code & 1u) ? 1 : -1;
        return sign * static_cast<int32_t>((code + 1) / 2);
    }

    bool skipBits(std::size_t count) {
        if (bit_pos_ + count > size_ * 8) {
            error_ = true;
            return false;
        }
        bit_pos_ += count;
        return true;
    }

    // True when the unread bits contain more than the rbsp_trailing_bits (a stop
    // '1' bit followed only by alignment zeros). Lets a parser detect optional
    // trailing syntax elements such as the H.264 PPS extension.
    bool moreRbspData() const {
        const std::size_t total = size_ * 8;
        std::size_t p = bit_pos_;
        while (p < total) {
            const std::size_t byte = p >> 3;
            const int shift = 7 - static_cast<int>(p & 7);
            if (((data_[byte] >> shift) & 1u) != 0) {
                break;
            }
            ++p;
        }
        if (p >= total) {
            return false;
        }
        for (++p; p < total; ++p) {
            const std::size_t byte = p >> 3;
            const int shift = 7 - static_cast<int>(p & 7);
            if (((data_[byte] >> shift) & 1u) != 0) {
                return true;
            }
        }
        return false;
    }

private:
    const uint8_t* data_;
    std::size_t size_;
    std::size_t bit_pos_ = 0;
    bool error_ = false;
};

}  // namespace vide
