#ifndef CAUTH_CORE_AUTH_STEAM_PASSWORD_ENCRYPTOR_HPP
#define CAUTH_CORE_AUTH_STEAM_PASSWORD_ENCRYPTOR_HPP

#include "steam/auth/steam_auth_transport.hpp"

#include <optional>
#include <string>

namespace cauth::steam::auth {

struct SteamEncryptedPassword {
    std::string bytes;
    std::uint64_t timestamp = 0;
};

class SteamPasswordEncryptor {
  public:
    virtual ~SteamPasswordEncryptor() = default;

    virtual std::optional<SteamEncryptedPassword> encrypt_password(
        const std::string& password, const SteamRsaPublicKey& key) = 0;
};

class UnsupportedSteamPasswordEncryptor final : public SteamPasswordEncryptor {
  public:
    std::optional<SteamEncryptedPassword> encrypt_password(
        const std::string& password, const SteamRsaPublicKey& key) override;
};

class PlatformSteamPasswordEncryptor final : public SteamPasswordEncryptor {
  public:
    std::optional<SteamEncryptedPassword> encrypt_password(
        const std::string& password, const SteamRsaPublicKey& key) override;
};

} // namespace cauth::steam::auth

#endif
