#include "core/platform/session_repository_factory.hpp"

#include "core/runtime/android/secure_storage_bridge.hpp"
#include "core/runtime/session/android_session_repository.hpp"
#include "core/runtime/session/memory_session_repository.hpp"
#include "core/runtime/windows/windows_session_repository.hpp"

namespace cauth::core::platform {

std::unique_ptr<session::SessionRepository> make_platform_session_repository() {
#ifdef _WIN32
    return std::make_unique<cauth::core::runtime::WindowsSessionRepository>();
#elif defined(__ANDROID__)
    if (auto* bridge = cauth::core::runtime::get_android_secure_storage_bridge();
        bridge != nullptr) {
        return std::make_unique<cauth::core::runtime::AndroidSessionRepository>(*bridge);
    }
    return std::make_unique<cauth::core::runtime::MemorySessionRepository>();
#else
    return std::make_unique<cauth::core::runtime::MemorySessionRepository>();
#endif
}

} // namespace cauth::core::platform
