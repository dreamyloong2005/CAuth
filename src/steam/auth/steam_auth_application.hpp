#pragma once

#include "core/session/auth_session.hpp"
#include "core/session/session_repository.hpp"
#include "steam/auth/platform_auth_runtime.hpp"
#include "steam/auth/steam_login_service.hpp"

#include <iosfwd>
#include <string_view>

namespace cauth::steam::auth {

int run_login(cauth::core::session::SessionRepository& store,
              const SteamLoginRequest& request,
              const SteamPlatformLoginOptions& options,
              std::ostream& out,
              std::ostream& err);
int print_status(cauth::core::session::SessionRepository& store, std::ostream& out);
int print_whoami(cauth::core::session::SessionRepository& store,
                 std::ostream& out,
                 std::ostream& err);
int print_saved_accounts(cauth::core::session::SessionRepository& store,
                         std::ostream& out);
int use_saved_account(cauth::core::session::SessionRepository& store,
                      std::string_view steam_id,
                      std::ostream& out,
                      std::ostream& err);
SteamLoginPlatformType steam_login_platform_type_for_refresh_token(
    const cauth::core::session::AuthSession& session);
int refresh_saved_access_token_from_store(cauth::core::session::SessionRepository& store,
                                          std::ostream& out,
                                          std::ostream& err);
bool refresh_saved_access_token(cauth::core::session::SessionRepository& store,
                                cauth::core::session::AuthSession& session,
                                std::ostream& out,
                                std::ostream& err);
int print_saved_web_cookies(cauth::core::session::SessionRepository& store,
                            std::ostream& out,
                            std::ostream& err);
int print_saved_web_cookies(const cauth::core::session::AuthSession& session,
                            std::ostream& out,
                            std::ostream& err);
int print_token_info(cauth::core::session::SessionRepository& store,
                     std::ostream& out,
                     std::ostream& err);
int clear_saved_session(cauth::core::session::SessionRepository& store, std::ostream& out);
int clear_saved_account(cauth::core::session::SessionRepository& store,
                        std::string_view steam_id,
                        std::ostream& out);
int clear_all_saved_accounts(cauth::core::session::SessionRepository& store,
                             std::ostream& out);

} // namespace cauth::steam::auth
