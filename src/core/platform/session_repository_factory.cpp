#include "core/platform/session_repository_factory.hpp"

#include "core/runtime/android/secure_storage_bridge.hpp"
#include "core/runtime/session/android_session_repository.hpp"
#include "core/runtime/session/memory_session_repository.hpp"
#include "core/runtime/windows/windows_session_repository.hpp"

#include <filesystem>
#include <utility>

namespace cauth::core::platform {

namespace {

thread_local bool g_thread_repository_options_set = false;
thread_local SessionRepositoryOptions g_thread_repository_options;

} // namespace

void set_current_thread_session_repository_options(SessionRepositoryOptions options) {
    g_thread_repository_options = std::move(options);
    g_thread_repository_options_set = true;
}

void clear_current_thread_session_repository_options() {
    g_thread_repository_options = {};
    g_thread_repository_options_set = false;
}

const SessionRepositoryOptions* current_thread_session_repository_options() {
    return g_thread_repository_options_set ? &g_thread_repository_options : nullptr;
}

std::unique_ptr<session::SessionRepository> make_platform_session_repository() {
    if (const auto* options = current_thread_session_repository_options();
        options != nullptr) {
        return make_platform_session_repository(*options);
    }
    return make_platform_session_repository(SessionRepositoryOptions{});
}

std::unique_ptr<session::SessionRepository> make_platform_session_repository(
    const SessionRepositoryOptions& options) {
#ifdef _WIN32
    if (options.backend == SessionRepositoryBackend::Memory) {
        return std::make_unique<cauth::core::runtime::MemorySessionRepository>();
    }
    if (!options.storage_path.empty() ||
        options.backend == SessionRepositoryBackend::File) {
        return std::make_unique<cauth::core::runtime::WindowsSessionRepository>(
            std::filesystem::path{options.storage_path});
    }
    return std::make_unique<cauth::core::runtime::WindowsSessionRepository>();
#elif defined(__ANDROID__)
    if (options.backend == SessionRepositoryBackend::Memory) {
        return std::make_unique<cauth::core::runtime::MemorySessionRepository>();
    }
    if (auto bridge = cauth::core::runtime::create_android_secure_storage_bridge(
            cauth::core::runtime::AndroidSecureStorageConfig{
                options.storage_namespace.empty() ? "cauth_secure_store" : options.storage_namespace,
                options.storage_key.empty() ? "auth_session_v1" : options.storage_key,
            });
        bridge != nullptr) {
        return std::make_unique<cauth::core::runtime::AndroidSessionRepository>(
            std::move(bridge));
    }
    return std::make_unique<cauth::core::runtime::MemorySessionRepository>();
#else
    return std::make_unique<cauth::core::runtime::MemorySessionRepository>();
#endif
}

} // namespace cauth::core::platform
