#ifndef CAUTH_CORE_PLATFORM_SESSION_REPOSITORY_FACTORY_HPP
#define CAUTH_CORE_PLATFORM_SESSION_REPOSITORY_FACTORY_HPP

#include "core/session/session_repository.hpp"

#include <memory>

namespace cauth::core::platform {

std::unique_ptr<session::SessionRepository> make_platform_session_repository();

} // namespace cauth::core::platform

#endif
