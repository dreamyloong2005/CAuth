#ifndef CAUTH_CORE_RUNTIME_ANDROID_SECURE_STORAGE_BRIDGE_HPP
#define CAUTH_CORE_RUNTIME_ANDROID_SECURE_STORAGE_BRIDGE_HPP

#include "core/runtime/session/android_session_repository.hpp"

namespace cauth::core::runtime {

bool is_android_secure_storage_bridge_available();
AndroidSecureStorageBridge* get_android_secure_storage_bridge();

} // namespace cauth::core::runtime

#endif
