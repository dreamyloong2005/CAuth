#ifndef CAUTH_STEAM_AUTH_STEAM_WEB_COOKIE_SERVICE_HPP
#define CAUTH_STEAM_AUTH_STEAM_WEB_COOKIE_SERVICE_HPP

#include "core/platform/http_client.hpp"
#include "core/session/auth_session.hpp"

#include <optional>
#include <string>
#include <vector>

namespace cauth::steam::auth {

struct SteamWebCookieResult {
    bool ok = false;
    std::string error_message;
    std::vector<std::string> cookies;
    std::string login_cookie_header;
    std::string community_cookie_header;
    std::string store_cookie_header;
    std::string store_session_id;
    std::string store_webapi_token;
};

std::string build_cookie_request_header(const std::vector<std::string>& cookies,
                                        std::string_view preferred_domain = {});
std::optional<std::string> extract_steam_refresh_token_from_web_cookies(
    const std::vector<std::string>& cookies,
    std::uint64_t expected_steam_id = 0);

class SteamHttpRequester {
  public:
    virtual ~SteamHttpRequester() = default;

    virtual cauth::core::platform::HttpResponse
    request(const cauth::core::platform::HttpRequest& request) = 0;
};

class PlatformSteamHttpRequester final : public SteamHttpRequester {
  public:
    cauth::core::platform::HttpResponse
    request(const cauth::core::platform::HttpRequest& request) override;
};

class SteamWebCookieService {
  public:
    explicit SteamWebCookieService(SteamHttpRequester& requester);

    SteamWebCookieResult get_web_cookies(const cauth::core::session::AuthSession& session,
                                         std::string_view store_path = "/") const;

  private:
    SteamHttpRequester* requester_;
};

} // namespace cauth::steam::auth

#endif
