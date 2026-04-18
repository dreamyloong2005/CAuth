#ifndef CAUTH_CORE_CRYPTO_AES_HPP
#define CAUTH_CORE_CRYPTO_AES_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace cauth::core::crypto {

struct AesDecryptResult {
    bool ok = false;
    std::string error_message;
    std::vector<std::uint8_t> bytes;
};

AesDecryptResult aes256_ecb_then_cbc_decrypt_pkcs7(
    const std::vector<std::uint8_t>& ciphertext,
    const std::vector<std::uint8_t>& key);

} // namespace cauth::core::crypto

#endif
