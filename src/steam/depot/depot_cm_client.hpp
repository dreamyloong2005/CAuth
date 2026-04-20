#ifndef CAUTH_STEAM_DEPOT_DEPOT_CM_CLIENT_HPP
#define CAUTH_STEAM_DEPOT_DEPOT_CM_CLIENT_HPP

#include "steam/auth/steam_auth_provider.hpp"
#include "steam/depot/app_info.hpp"
#include "steam/depot/depot_key.hpp"
#include "steam/depot/manifest_request_code.hpp"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace cauth::core::depot {

class DepotCmClient {
  public:
    DepotCmClient(cauth::steam::auth::SteamAuthProvider& auth_provider,
                  std::string_view subject_id,
                  std::ostream* out = nullptr,
                  std::ostream* err = nullptr);

    std::optional<AppInfo> fetch_app_info(std::uint32_t app_id, std::uint32_t max_count) const;
    std::optional<DepotDecryptionKeyResponse> fetch_depot_key(std::uint32_t app_id,
                                                              std::uint32_t depot_id,
                                                              std::uint32_t max_count) const;
    std::optional<ManifestRequestCodeResponse> fetch_manifest_request_code(
        const ManifestRequestCodeRequest& request,
        std::uint32_t max_count) const;

  private:
    cauth::steam::auth::SteamAuthProvider* auth_provider_;
    std::string subject_id_;
    std::ostream* out_;
    std::ostream* err_;
};

} // namespace cauth::core::depot

#endif
