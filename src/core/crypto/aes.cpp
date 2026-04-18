#include "core/crypto/aes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace cauth::core::crypto {
namespace {

constexpr std::size_t kAesBlockSize = 16;
constexpr std::size_t kAes256KeySize = 32;
constexpr std::size_t kAes256RoundKeySize = 240;
constexpr int kAes256Rounds = 14;

using AesState = std::array<std::array<std::uint8_t, 4>, 4>;

constexpr std::array<std::uint8_t, 256> kInverseSBox = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81,
    0xf3, 0xd7, 0xfb, 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e,
    0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23,
    0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e, 0x08, 0x2e, 0xa1, 0x66,
    0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25, 0x72,
    0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65,
    0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46,
    0x57, 0xa7, 0x8d, 0x9d, 0x84, 0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
    0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06, 0xd0, 0x2c, 0x1e, 0x8f, 0xca,
    0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b, 0x3a, 0x91,
    0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6,
    0x73, 0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8,
    0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f,
    0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2,
    0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4, 0x1f, 0xdd, 0xa8,
    0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93,
    0xc9, 0x9c, 0xef, 0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb,
    0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61, 0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6,
    0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

constexpr std::array<std::uint8_t, 256> kSBox = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe,
    0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4,
    0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7,
    0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3,
    0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09,
    0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3,
    0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe,
    0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92,
    0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c,
    0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19,
    0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2,
    0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5,
    0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25,
    0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86,
    0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e,
    0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42,
    0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

constexpr std::array<std::uint8_t, 15> kRcon = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8,
    0xab, 0x4d
};

std::uint8_t xtime(std::uint8_t value) {
    return static_cast<std::uint8_t>((value << 1U) ^
                                     ((value & 0x80U) != 0 ? 0x1bU : 0x00U));
}

std::uint8_t multiply(std::uint8_t value, std::uint8_t factor) {
    std::uint8_t result = 0;
    std::uint8_t current = value;
    std::uint8_t mask = factor;
    while (mask != 0) {
        if ((mask & 1U) != 0) {
            result ^= current;
        }
        current = xtime(current);
        mask >>= 1U;
    }
    return result;
}

void add_round_key(AesState& state,
                   const std::array<std::uint8_t, kAes256RoundKeySize>& round_keys,
                   int round) {
    const std::size_t round_offset = static_cast<std::size_t>(round) * kAesBlockSize;
    for (std::size_t col = 0; col < 4; ++col) {
        for (std::size_t row = 0; row < 4; ++row) {
            state[row][col] ^= round_keys[round_offset + col * 4 + row];
        }
    }
}

void inv_sub_bytes(AesState& state) {
    for (auto& row : state) {
        for (auto& value : row) {
            value = kInverseSBox[value];
        }
    }
}

void inv_shift_rows(AesState& state) {
    std::uint8_t temp = 0;

    temp = state[1][3];
    state[1][3] = state[1][2];
    state[1][2] = state[1][1];
    state[1][1] = state[1][0];
    state[1][0] = temp;

    temp = state[2][0];
    state[2][0] = state[2][2];
    state[2][2] = temp;
    temp = state[2][1];
    state[2][1] = state[2][3];
    state[2][3] = temp;

    temp = state[3][0];
    state[3][0] = state[3][1];
    state[3][1] = state[3][2];
    state[3][2] = state[3][3];
    state[3][3] = temp;
}

void inv_mix_columns(AesState& state) {
    for (std::size_t col = 0; col < 4; ++col) {
        const auto a = state[0][col];
        const auto b = state[1][col];
        const auto c = state[2][col];
        const auto d = state[3][col];
        state[0][col] = static_cast<std::uint8_t>(
            multiply(a, 0x0eU) ^ multiply(b, 0x0bU) ^ multiply(c, 0x0dU) ^ multiply(d, 0x09U));
        state[1][col] = static_cast<std::uint8_t>(
            multiply(a, 0x09U) ^ multiply(b, 0x0eU) ^ multiply(c, 0x0bU) ^ multiply(d, 0x0dU));
        state[2][col] = static_cast<std::uint8_t>(
            multiply(a, 0x0dU) ^ multiply(b, 0x09U) ^ multiply(c, 0x0eU) ^ multiply(d, 0x0bU));
        state[3][col] = static_cast<std::uint8_t>(
            multiply(a, 0x0bU) ^ multiply(b, 0x0dU) ^ multiply(c, 0x09U) ^ multiply(d, 0x0eU));
    }
}

void rot_word(std::array<std::uint8_t, 4>& word) {
    const auto first = word[0];
    word[0] = word[1];
    word[1] = word[2];
    word[2] = word[3];
    word[3] = first;
}

void sub_word(std::array<std::uint8_t, 4>& word) {
    for (auto& value : word) {
        value = kSBox[value];
    }
}

std::array<std::uint8_t, kAes256RoundKeySize> expand_key(
    const std::vector<std::uint8_t>& key) {
    std::array<std::uint8_t, kAes256RoundKeySize> round_keys{};
    std::copy(key.begin(), key.end(), round_keys.begin());

    std::size_t bytes_generated = kAes256KeySize;
    std::size_t rcon_index = 1;
    std::array<std::uint8_t, 4> temp{};

    while (bytes_generated < round_keys.size()) {
        for (std::size_t index = 0; index < temp.size(); ++index) {
            temp[index] = round_keys[bytes_generated - temp.size() + index];
        }

        if ((bytes_generated % kAes256KeySize) == 0) {
            rot_word(temp);
            sub_word(temp);
            temp[0] ^= kRcon[rcon_index++];
        } else if ((bytes_generated % kAes256KeySize) == kAesBlockSize) {
            sub_word(temp);
        }

        for (std::size_t index = 0; index < temp.size(); ++index) {
            round_keys[bytes_generated] =
                static_cast<std::uint8_t>(round_keys[bytes_generated - kAes256KeySize] ^
                                          temp[index]);
            ++bytes_generated;
        }
    }

    return round_keys;
}

std::array<std::uint8_t, kAesBlockSize> decrypt_block(
    const std::array<std::uint8_t, kAesBlockSize>& input,
    const std::array<std::uint8_t, kAes256RoundKeySize>& round_keys) {
    AesState state{};
    for (std::size_t col = 0; col < 4; ++col) {
        for (std::size_t row = 0; row < 4; ++row) {
            state[row][col] = input[col * 4 + row];
        }
    }

    add_round_key(state, round_keys, kAes256Rounds);
    for (int round = kAes256Rounds - 1; round >= 1; --round) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, round_keys, round);
        inv_mix_columns(state);
    }
    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, round_keys, 0);

    std::array<std::uint8_t, kAesBlockSize> output{};
    for (std::size_t col = 0; col < 4; ++col) {
        for (std::size_t row = 0; row < 4; ++row) {
            output[col * 4 + row] = state[row][col];
        }
    }
    return output;
}

std::array<std::uint8_t, kAesBlockSize> decrypt_block_from_bytes(
    const std::vector<std::uint8_t>& ciphertext,
    std::size_t offset,
    const std::array<std::uint8_t, kAes256RoundKeySize>& round_keys) {
    std::array<std::uint8_t, kAesBlockSize> block{};
    std::copy_n(ciphertext.begin() + static_cast<std::ptrdiff_t>(offset), kAesBlockSize,
                block.begin());
    return decrypt_block(block, round_keys);
}

bool remove_pkcs7_padding(std::vector<std::uint8_t>& bytes, std::string& error_message) {
    if (bytes.empty()) {
        error_message = "AES plaintext is empty after decrypt";
        return false;
    }
    const auto padding = bytes.back();
    if (padding == 0 || padding > kAesBlockSize || padding > bytes.size()) {
        error_message = "AES plaintext has invalid PKCS7 padding";
        return false;
    }
    const auto padding_size = static_cast<std::size_t>(padding);
    if (!std::all_of(bytes.end() - static_cast<std::ptrdiff_t>(padding_size), bytes.end(),
                     [padding](std::uint8_t value) { return value == padding; })) {
        error_message = "AES plaintext PKCS7 padding bytes do not match";
        return false;
    }
    bytes.resize(bytes.size() - padding_size);
    return true;
}

} // namespace

AesDecryptResult aes256_ecb_then_cbc_decrypt_pkcs7(const std::vector<std::uint8_t>& ciphertext,
                                                   const std::vector<std::uint8_t>& key) {
    if (key.size() != kAes256KeySize) {
        return {false, "AES-256 key must be 32 bytes", {}};
    }
    if (ciphertext.size() <= kAesBlockSize || (ciphertext.size() % kAesBlockSize) != 0) {
        return {false, "AES ciphertext must be more than one block and block-aligned", {}};
    }

    const auto round_keys = expand_key(key);
    const auto iv = decrypt_block_from_bytes(ciphertext, 0, round_keys);

    std::vector<std::uint8_t> plain(ciphertext.size() - kAesBlockSize);
    std::array<std::uint8_t, kAesBlockSize> previous = iv;

    for (std::size_t offset = kAesBlockSize; offset < ciphertext.size();
         offset += kAesBlockSize) {
        const auto decrypted = decrypt_block_from_bytes(ciphertext, offset, round_keys);
        for (std::size_t index = 0; index < kAesBlockSize; ++index) {
            plain[offset - kAesBlockSize + index] =
                static_cast<std::uint8_t>(decrypted[index] ^ previous[index]);
        }
        std::copy_n(ciphertext.begin() + static_cast<std::ptrdiff_t>(offset), kAesBlockSize,
                    previous.begin());
    }

    std::string error_message;
    if (!remove_pkcs7_padding(plain, error_message)) {
        return {false, error_message, {}};
    }
    return {true, {}, std::move(plain)};
}

} // namespace cauth::core::crypto
