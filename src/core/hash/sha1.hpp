#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cauth::core::hash {

class Sha1Hasher {
public:
    void update(const std::uint8_t* data, std::size_t size);
    std::array<std::uint8_t, 20> finish();

private:
    void process_block(const std::uint8_t* block);

    std::array<std::uint32_t, 5> state_{
        0x67452301U,
        0xefcdab89U,
        0x98badcfeU,
        0x10325476U,
        0xc3d2e1f0U,
    };
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_ = 0;
    std::uint64_t total_size_ = 0;
    bool finished_ = false;
};

std::array<std::uint8_t, 20> sha1_digest(const std::vector<std::uint8_t>& bytes);
std::string hash_bytes_to_hex(const std::vector<std::uint8_t>& bytes);
std::string sha1_to_hex(const std::array<std::uint8_t, 20>& digest);
bool sha1_matches(const std::vector<std::uint8_t>& bytes,
                  const std::vector<std::uint8_t>& expected_sha1);

} // namespace cauth::core::hash
