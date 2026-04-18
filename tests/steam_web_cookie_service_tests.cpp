#include "steam/auth/steam_web_cookie_service.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <iostream>

namespace {

class FakeRequester final : public cauth::steam::auth::SteamHttpRequester {
  public:
    cauth::core::platform::HttpResponse
    request(const cauth::core::platform::HttpRequest& request) override {
        requests.push_back(request);
        if (requests.size() == 1) {
            return {
                true,
                "",
                200,
                std::vector<std::uint8_t>{
                    '{', '"', 't', 'r', 'a', 'n', 's', 'f', 'e', 'r', '_', 'i', 'n', 'f', 'o',
                    '"', ':', '[', '{', '"', 'u', 'r', 'l', '"', ':', '"',
                    'h', 't', 't', 'p', 's', ':', '/', '/', 's', 't', 'e', 'a', 'm', 'c',
                    'o', 'm', 'm', 'u', 'n', 'i', 't', 'y', '.', 'c', 'o', 'm', '/', 'l',
                    'o', 'g', 'i', 'n', '/', 's', 'e', 't', 't', 'o', 'k', 'e', 'n', '"',
                    ',', '"', 'p', 'a', 'r', 'a', 'm', 's', '"', ':', '{',
                    '"', 'n', 'o', 'n', 'c', 'e', '"', ':', '"', 'n', 'o', 'n', 'c', 'e',
                    '-', 'v', 'a', 'l', 'u', 'e', '"', ',', '"', 'a', 'u', 't', 'h', '"',
                    ':', '"', 'a', 'u', 't', 'h', '-', 'v', 'a', 'l', 'u', 'e', '"', '}',
                    '}', ']',
                    '}',
                },
                {{"Set-Cookie", "steamRefresh_steam=refresh-cookie; Path=/; Secure"}},
            };
        }

        return {
            true,
            "",
            200,
            std::vector<std::uint8_t>{'{', '}', '\n'},
            {{"Set-Cookie", "steamLoginSecure=login-cookie; Path=/; Secure"}},
        };
    }

    std::vector<cauth::core::platform::HttpRequest> requests;
};

class EscapedUrlRequester final : public cauth::steam::auth::SteamHttpRequester {
  public:
    cauth::core::platform::HttpResponse
    request(const cauth::core::platform::HttpRequest& request) override {
        requests.push_back(request);
        if (requests.size() == 1) {
            static constexpr char kBody[] =
                "{\"transfer_info\":[{\"url\":\"https:\\/\\/steamcommunity.com\\/login\\/settoken\","
                "\"params\":{\"nonce\":\"nonce-value\",\"auth\":\"auth-value\"}}]}";
            return {
                true,
                "",
                200,
                std::vector<std::uint8_t>{kBody, kBody + sizeof(kBody) - 1},
                {{"Set-Cookie", "steamRefresh_steam=refresh-cookie; Path=/; Secure"}},
            };
        }

        return {
            true,
            "",
            200,
            std::vector<std::uint8_t>{'{', '}', '\n'},
            {{"Set-Cookie", "steamLoginSecure=login-cookie; Path=/; Secure"}},
        };
    }

    std::vector<cauth::core::platform::HttpRequest> requests;
};

class LegacyShapeRequester final : public cauth::steam::auth::SteamHttpRequester {
  public:
    cauth::core::platform::HttpResponse
    request(const cauth::core::platform::HttpRequest& request) override {
        requests.push_back(request);
        if (requests.size() == 1) {
            static constexpr char kBody[] =
                "{\"transfer_urls\":[\"https://steamcommunity.com/login/settoken\"],"
                "\"transfer_parameters\":{\"nonce\":\"nonce-value\",\"auth\":\"auth-value\","
                "\"steamID\":\"76561198000000000\",\"remember_login\":true}}";
            return {
                true,
                "",
                200,
                std::vector<std::uint8_t>{kBody, kBody + sizeof(kBody) - 1},
                {{"Set-Cookie", "steamRefresh_steam=refresh-cookie; Path=/; Secure"}},
            };
        }

        return {
            true,
            "",
            200,
            std::vector<std::uint8_t>{'{', '}', '\n'},
            {{"Set-Cookie", "steamLoginSecure=login-cookie; Path=/; Secure"}},
        };
    }

    std::vector<cauth::core::platform::HttpRequest> requests;
};

} // namespace

int main() {
    auto make_session = []() {
        cauth::core::session::AuthSession session;
        session.provider = std::string{cauth::steam::auth::kSteamAuthProvider};
        session.subject_id = "76561198000000000";
        session.account_name = "test_account";
        session.refresh_token = "web-refresh-token";
        return session;
    };

    {
        FakeRequester requester;
        cauth::steam::auth::SteamWebCookieService service{requester};
        const auto result = service.get_web_cookies(make_session());
        if (!result.ok || result.cookies.size() < 3 || requester.requests.size() != 2) {
            std::cerr << "web cookie service should finalize login and perform transfer requests\n";
            return 1;
        }

        if (requester.requests[0].url != "https://login.steampowered.com/jwt/finalizelogin" ||
            requester.requests[1].url != "https://steamcommunity.com/login/settoken") {
            std::cerr << "web cookie service should hit finalizelogin then transfer endpoints\n";
            return 1;
        }

        bool saw_refresh_cookie = false;
        bool saw_login_cookie = false;
        bool saw_session_cookie = false;
        for (const auto& cookie : result.cookies) {
            if (cookie.find("steamRefresh_steam=refresh-cookie") != std::string::npos &&
                cookie.find("Domain=login.steampowered.com") != std::string::npos) {
                saw_refresh_cookie = true;
            }
            if (cookie.find("steamLoginSecure=login-cookie") != std::string::npos &&
                cookie.find("Domain=steamcommunity.com") != std::string::npos) {
                saw_login_cookie = true;
            }
            if (cookie.find("sessionid=") == 0 &&
                cookie.find("Domain=steamcommunity.com") != std::string::npos) {
                saw_session_cookie = true;
            }
        }

        if (!saw_refresh_cookie || !saw_login_cookie || !saw_session_cookie) {
            std::cerr << "web cookie service should surface transfer cookies and synthesized sessionid\n";
            return 1;
        }
    }

    {
        LegacyShapeRequester requester;
        cauth::steam::auth::SteamWebCookieService service{requester};
        const auto result = service.get_web_cookies(make_session());
        if (!result.ok || requester.requests.size() != 2) {
            std::cerr << "web cookie service should accept transfer_urls/transfer_parameters\n";
            return 1;
        }
    }

    {
        EscapedUrlRequester requester;
        cauth::steam::auth::SteamWebCookieService service{requester};
        const auto result = service.get_web_cookies(make_session());
        if (!result.ok || requester.requests.size() != 2 ||
            requester.requests[1].url != "https://steamcommunity.com/login/settoken") {
            std::cerr << "web cookie service should unescape transfer URLs\n";
            return 1;
        }
    }

    {
        const std::vector<std::string> cookies = {
            "steamRefresh_steam=76561198000000000%7C%7Crefresh-jwt; Domain=login.steampowered.com",
            "steamLoginSecure=login-cookie; Domain=steamcommunity.com",
            "steamLoginSecure=tv-cookie; Domain=steam.tv",
            "sessionid=abc123; Domain=steamcommunity.com",
        };
        const auto refresh_token =
            cauth::steam::auth::extract_steam_refresh_token_from_web_cookies(
                cookies, 76561198000000000ULL);
        const auto cookie_header =
            cauth::steam::auth::build_cookie_request_header(cookies, "steamcommunity.com");
        if (!refresh_token.has_value() || *refresh_token != "refresh-jwt" ||
            cookie_header.find("steamLoginSecure=login-cookie") == std::string::npos ||
            cookie_header.find("tv-cookie") != std::string::npos ||
            cookie_header.find("sessionid=abc123") == std::string::npos) {
            std::cerr << "web cookie helpers should extract the refresh token and build a domain-aware Cookie header\n";
            return 1;
        }
    }

    return 0;
}
