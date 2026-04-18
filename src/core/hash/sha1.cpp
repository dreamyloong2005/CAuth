#include "core/hash/sha1.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace cauth::core::hash {
namespace {

std::uint32_t rotate_left(std::uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32U - bits));
}

std::uint32_t read_u32_be(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

void write_u64_be(std::uint64_t value, std::uint8_t* out) {
    for (int index = 7; index >= 0; --index) {
        out[index] = static_cast<std::uint8_t>(value & 0xffU);
        value >>= 8U;
    }
}

} // namespace

void Sha1Hasher::update(const std::uint8_t* data, std::size_t size) {
    if (finished_ || data == nullptr || size == 0) {
        return;
    }

    total_size_ += size;
    while (size > 0) {
        const auto copy_size = std::min<std::size_t>(size, buffer_.size() - buffer_size_);
        std::copy_n(data, copy_size, buffer_.data() + buffer_size_);
        buffer_size_ += copy_size;
        data += copy_size;
        size -= copy_size;

        if (buffer_size_ == buffer_.size()) {
            process_block(buffer_.data());
            buffer_size_ = 0;
        }
    }
}

std::array<std::uint8_t, 20> Sha1Hasher::finish() {
    if (!finished_) {
        const auto total_bits = total_size_ * 8U;
        buffer_[buffer_size_++] = 0x80U;
        if (buffer_size_ > 56) {
            std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
                      buffer_.end(), std::uint8_t{0});
            process_block(buffer_.data());
            buffer_size_ = 0;
        }
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
                  buffer_.begin() + 56, std::uint8_t{0});
        write_u64_be(total_bits, buffer_.data() + 56);
        process_block(buffer_.data());
        finished_ = true;
    }

    std::array<std::uint8_t, 20> digest{};
    for (std::size_t index = 0; index < state_.size(); ++index) {
        digest[index * 4] = static_cast<std::uint8_t>((state_[index] >> 24U) & 0xffU);
        digest[index * 4 + 1] = static_cast<std::uint8_t>((state_[index] >> 16U) & 0xffU);
        digest[index * 4 + 2] = static_cast<std::uint8_t>((state_[index] >> 8U) & 0xffU);
        digest[index * 4 + 3] = static_cast<std::uint8_t>(state_[index] & 0xffU);
    }
    return digest;
}

void Sha1Hasher::process_block(const std::uint8_t* block) {
    std::array<std::uint32_t, 80> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = read_u32_be(block + index * 4);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        words[index] = rotate_left(words[index - 3] ^ words[index - 8] ^
                                       words[index - 14] ^ words[index - 16],
                                   1);
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];

    for (std::size_t index = 0; index < words.size(); ++index) {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (index < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999U;
        } else if (index < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1U;
        } else if (index < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcU;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6U;
        }

        const auto temp = rotate_left(a, 5) + f + e + k + words[index];
        e = d;
        d = c;
        c = rotate_left(b, 30);
        b = a;
        a = temp;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
}

std::array<std::uint8_t, 20> sha1_digest(const std::vector<std::uint8_t>& bytes) {
    Sha1Hasher hasher;
    hasher.update(bytes.data(), bytes.size());
    return hasher.finish();
}

std::string hash_bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        out << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return out.str();
}

std::string sha1_to_hex(const std::array<std::uint8_t, 20>& digest) {
    return hash_bytes_to_hex(std::vector<std::uint8_t>{digest.begin(), digest.end()});
}

bool sha1_matches(const std::vector<std::uint8_t>& bytes,
                  const std::vector<std::uint8_t>& expected_sha1) {
    if (expected_sha1.size() != 20) {
        return false;
    }
    const auto digest = sha1_digest(bytes);
    return std::equal(digest.begin(), digest.end(), expected_sha1.begin());
}

} // namespace cauth::core::hash
