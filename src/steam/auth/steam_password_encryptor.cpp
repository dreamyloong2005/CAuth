#include "steam/auth/steam_password_encryptor.hpp"

#include "core/platform/password_crypto.hpp"

namespace cauth::steam::auth {

std::optional<SteamEncryptedPassword> UnsupportedSteamPasswordEncryptor::encrypt_password(
    const std::string&, const SteamRsaPublicKey&) {
    return std::nullopt;
}

std::optional<SteamEncryptedPassword> PlatformSteamPasswordEncryptor::encrypt_password(
    const std::string& password, const SteamRsaPublicKey& key) {
    const auto encrypted = cauth::core::platform::encrypt_password_rsa_pkcs1_base64(
        password, {key.modulus_hex, key.exponent_hex});
    if (!encrypted.ok || encrypted.base64_ciphertext.empty()) {
        return std::nullopt;
    }

    return SteamEncryptedPassword{
        encrypted.base64_ciphertext,
        key.timestamp,
    };
}

} // namespace cauth::steam::auth
