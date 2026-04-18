#include "core/platform/password_crypto.hpp"

#ifdef __ANDROID__
#include "core/runtime/android/bridge.hpp"
#endif

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace cauth::core::platform {
namespace {

#ifdef _WIN32
std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
    constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const auto remaining = bytes.size() - index;
        const auto a = bytes[index];
        const auto b = remaining > 1 ? bytes[index + 1] : 0;
        const auto c = remaining > 2 ? bytes[index + 2] : 0;

        encoded.push_back(kAlphabet[(a >> 2) & 0x3f]);
        encoded.push_back(kAlphabet[((a & 0x03) << 4) | ((b >> 4) & 0x0f)]);
        encoded.push_back(remaining > 1 ? kAlphabet[((b & 0x0f) << 2) | ((c >> 6) & 0x03)] : '=');
        encoded.push_back(remaining > 2 ? kAlphabet[c & 0x3f] : '=');
    }

    return encoded;
}

std::optional<std::vector<std::uint8_t>> hex_to_bytes(const std::string& hex) {
    if (hex.empty() || hex.size() % 2 != 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t index = 0; index < hex.size(); index += 2) {
        const auto high = static_cast<unsigned char>(hex[index]);
        const auto low = static_cast<unsigned char>(hex[index + 1]);
        if (!std::isxdigit(high) || !std::isxdigit(low)) {
            return std::nullopt;
        }
        const auto value = static_cast<unsigned long>(std::stoul(hex.substr(index, 2), nullptr, 16));
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

struct BCryptAlgorithmDeleter {
    void operator()(BCRYPT_ALG_HANDLE handle) const noexcept {
        if (handle != nullptr) {
            BCryptCloseAlgorithmProvider(handle, 0);
        }
    }
};

struct BCryptKeyDeleter {
    void operator()(BCRYPT_KEY_HANDLE handle) const noexcept {
        if (handle != nullptr) {
            BCryptDestroyKey(handle);
        }
    }
};

using BCryptAlgorithmHandle =
    std::unique_ptr<std::remove_pointer_t<BCRYPT_ALG_HANDLE>, BCryptAlgorithmDeleter>;
using BCryptKeyHandle = std::unique_ptr<std::remove_pointer_t<BCRYPT_KEY_HANDLE>, BCryptKeyDeleter>;

std::optional<std::vector<std::uint8_t>> build_rsa_public_blob(
    const RsaPublicKey& key) {
    auto modulus = hex_to_bytes(key.modulus_hex);
    auto exponent = hex_to_bytes(key.exponent_hex);
    if (!modulus.has_value() || !exponent.has_value() || modulus->empty() || exponent->empty()) {
        return std::nullopt;
    }

    if (modulus->size() > (std::numeric_limits<ULONG>::max)() ||
        exponent->size() > (std::numeric_limits<ULONG>::max)()) {
        return std::nullopt;
    }

    BCRYPT_RSAKEY_BLOB header{};
    header.Magic = BCRYPT_RSAPUBLIC_MAGIC;
    header.BitLength = static_cast<ULONG>(modulus->size() * 8);
    header.cbPublicExp = static_cast<ULONG>(exponent->size());
    header.cbModulus = static_cast<ULONG>(modulus->size());
    header.cbPrime1 = 0;
    header.cbPrime2 = 0;

    std::vector<std::uint8_t> blob(sizeof(header) + exponent->size() + modulus->size());
    std::memcpy(blob.data(), &header, sizeof(header));
    std::memcpy(blob.data() + sizeof(header), exponent->data(), exponent->size());
    std::memcpy(blob.data() + sizeof(header) + exponent->size(), modulus->data(), modulus->size());
    return blob;
}

PlatformRsaEncryptResult windows_encrypt_password_rsa_pkcs1_base64(
    const std::string& password,
    const RsaPublicKey& key) {
    auto blob = build_rsa_public_blob(key);
    if (!blob.has_value()) {
        return {false, "failed to build RSA public key blob", ""};
    }

    BCRYPT_ALG_HANDLE raw_algorithm = nullptr;
    if (!BCRYPT_SUCCESS(
            BCryptOpenAlgorithmProvider(&raw_algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0))) {
        return {false, "BCryptOpenAlgorithmProvider failed", ""};
    }
    BCryptAlgorithmHandle algorithm{raw_algorithm};

    BCRYPT_KEY_HANDLE raw_key = nullptr;
    if (!BCRYPT_SUCCESS(BCryptImportKeyPair(algorithm.get(), nullptr, BCRYPT_RSAPUBLIC_BLOB,
                                            &raw_key, blob->data(),
                                            static_cast<ULONG>(blob->size()), 0))) {
        return {false, "BCryptImportKeyPair failed", ""};
    }
    BCryptKeyHandle imported_key{raw_key};

    ULONG encrypted_size = 0;
    auto* plain = reinterpret_cast<PUCHAR>(const_cast<char*>(password.data()));
    const auto plain_size = static_cast<ULONG>(password.size());
    if (!BCRYPT_SUCCESS(BCryptEncrypt(imported_key.get(), plain, plain_size, nullptr, nullptr, 0,
                                      nullptr, 0, &encrypted_size, BCRYPT_PAD_PKCS1))) {
        return {false, "BCryptEncrypt sizing failed", ""};
    }

    std::vector<std::uint8_t> encrypted(encrypted_size);
    if (!BCRYPT_SUCCESS(BCryptEncrypt(imported_key.get(), plain, plain_size, nullptr, nullptr, 0,
                                      encrypted.data(), encrypted_size, &encrypted_size,
                                      BCRYPT_PAD_PKCS1))) {
        return {false, "BCryptEncrypt failed", ""};
    }

    encrypted.resize(encrypted_size);
    return {true, "", base64_encode(encrypted)};
}
#endif

} // namespace

PlatformRsaEncryptResult encrypt_password_rsa_pkcs1_base64(
    const std::string& password,
    const RsaPublicKey& key) {
#ifdef _WIN32
    return windows_encrypt_password_rsa_pkcs1_base64(password, key);
#elif defined(__ANDROID__)
    const auto result = cauth::core::runtime::android_bridge_encrypt_password_pkcs1(
        key.modulus_hex, key.exponent_hex, password);
    return {result.ok, result.error_message, result.base64_ciphertext};
#else
    (void)password;
    (void)key;
    return {false, "Platform RSA password encryption is not implemented on this platform yet", ""};
#endif
}

bool is_platform_password_crypto_available() {
#ifdef _WIN32
    return true;
#elif defined(__ANDROID__)
    return cauth::core::runtime::is_android_platform_bridge_available();
#else
    return false;
#endif
}

} // namespace cauth::core::platform
