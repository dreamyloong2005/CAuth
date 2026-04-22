#ifndef CAUTH_CORE_RUNTIME_ANDROID_SECURE_STORAGE_BRIDGE_HPP
#define CAUTH_CORE_RUNTIME_ANDROID_SECURE_STORAGE_BRIDGE_HPP

#include "core/runtime/session/android_session_repository.hpp"

#include <memory>
#include <string>

namespace cauth::core::runtime {

struct AndroidSecureStorageConfig {
    std::string preferences_name = "cauth_secure_store";
    std::string session_key = "auth_session_v1";
};

bool is_android_secure_storage_bridge_available();
AndroidSecureStorageBridge* get_android_secure_storage_bridge();
std::unique_ptr<AndroidSecureStorageBridge> create_android_secure_storage_bridge(
    AndroidSecureStorageConfig config = {});

} // namespace cauth::core::runtime

#endif
