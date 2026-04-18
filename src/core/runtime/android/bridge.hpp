#ifndef CAUTH_CORE_RUNTIME_ANDROID_BRIDGE_HPP
#define CAUTH_CORE_RUNTIME_ANDROID_BRIDGE_HPP

#ifdef __ANDROID__
#include <jni.h>
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/platform/http_client.hpp"

namespace cauth::core::runtime {

bool is_android_platform_bridge_available();

#ifdef __ANDROID__
struct AndroidWebSocketConnectResult {
    bool ok = false;
    std::int64_t handle = 0;
    std::string error_message;
};

struct AndroidWebSocketCallResult {
    bool ok = false;
    std::string error_message;
};

struct AndroidWebSocketReceiveResult {
    bool ok = false;
    std::string error_message;
    std::vector<std::uint8_t> bytes;
};

struct AndroidHttpTextResult {
    bool ok = false;
    std::string error_message;
    std::string body;
};

struct AndroidHttpResponse {
    bool ok = false;
    std::string error_message;
    std::uint32_t status_code = 0;
    std::vector<std::uint8_t> body;
};

struct AndroidRsaEncryptResult {
    bool ok = false;
    std::string error_message;
    std::string base64_ciphertext;
};

AndroidWebSocketConnectResult android_bridge_websocket_connect(std::string_view url,
                                                               std::int32_t connect_timeout_ms);
AndroidWebSocketCallResult android_bridge_websocket_send(std::int64_t handle,
                                                         const std::vector<std::uint8_t>& bytes);
AndroidWebSocketReceiveResult android_bridge_websocket_receive(std::int64_t handle,
                                                               std::int32_t receive_timeout_ms);
AndroidHttpResponse android_bridge_http_request(std::string_view method,
                                                std::string_view url,
                                                const std::vector<std::uint8_t>& body,
                                                std::string_view content_type,
                                                const std::vector<cauth::core::platform::HttpHeader>& headers,
                                                std::int32_t connect_timeout_ms,
                                                std::int32_t read_timeout_ms);
AndroidHttpTextResult android_bridge_http_get_text(std::string_view url,
                                                   std::int32_t timeout_ms);
AndroidRsaEncryptResult android_bridge_encrypt_password_pkcs1(
    std::string_view modulus_hex,
    std::string_view exponent_hex,
    std::string_view password);
void android_bridge_websocket_close(std::int64_t handle);

bool initialize_android_platform_bridge(JNIEnv* env, jobject application_context);
void shutdown_android_platform_bridge(JNIEnv* env);
#endif

} // namespace cauth::core::runtime

#endif
