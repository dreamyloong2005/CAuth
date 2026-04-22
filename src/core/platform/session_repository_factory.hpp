#ifndef CAUTH_CORE_PLATFORM_SESSION_REPOSITORY_FACTORY_HPP
#define CAUTH_CORE_PLATFORM_SESSION_REPOSITORY_FACTORY_HPP

#include "core/session/session_repository.hpp"

#include <memory>
#include <string>

namespace cauth::core::platform {

enum class SessionRepositoryBackend {
    Default = 0,
    Memory = 1,
    File = 2,
    SecureStorage = 3,
};

struct SessionRepositoryOptions {
    SessionRepositoryBackend backend = SessionRepositoryBackend::Default;
    std::string storage_path;
    std::string storage_namespace;
    std::string storage_key;
};

void set_current_thread_session_repository_options(SessionRepositoryOptions options);
void clear_current_thread_session_repository_options();
const SessionRepositoryOptions* current_thread_session_repository_options();

std::unique_ptr<session::SessionRepository> make_platform_session_repository();
std::unique_ptr<session::SessionRepository> make_platform_session_repository(
    const SessionRepositoryOptions& options);

} // namespace cauth::core::platform

#endif
