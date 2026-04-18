#ifndef CAUTH_CORE_PLATFORM_PASSWORD_CRYPTO_HPP
#define CAUTH_CORE_PLATFORM_PASSWORD_CRYPTO_HPP

#include "core/platform/rsa_public_key.hpp"

#include <optional>
#include <string>

namespace cauth::core::platform {

struct PlatformRsaEncryptResult {
    bool ok = false;
    std::string error_message;
    std::string base64_ciphertext;
};

PlatformRsaEncryptResult encrypt_password_rsa_pkcs1_base64(
    const std::string& password,
    const RsaPublicKey& key);

bool is_platform_password_crypto_available();

} // namespace cauth::core::platform

#endif
