#include "core/runtime/android/bridge.hpp"

#ifdef __ANDROID__

#include <jni.h>

#include <mutex>
#include <string_view>

namespace cauth::core::runtime {
namespace {

constexpr const char* kBridgeClassName = "com/cauth/android/CAuthAndroidWebSocketBridge";
constexpr const char* kHttpBridgeClassName = "com/cauth/android/CAuthAndroidHttpBridge";
constexpr const char* kCryptoBridgeClassName = "com/cauth/android/CAuthAndroidCryptoBridge";
constexpr const char* kConnectName = "connect";
constexpr const char* kConnectSignature = "(Ljava/lang/String;I)J";
constexpr const char* kSendBinaryName = "sendBinary";
constexpr const char* kSendBinarySignature = "(J[B)V";
constexpr const char* kReceiveBinaryName = "receiveBinary";
constexpr const char* kReceiveBinarySignature = "(JI)[B";
constexpr const char* kCloseName = "close";
constexpr const char* kCloseSignature = "(J)V";
constexpr const char* kGetTextName = "getText";
constexpr const char* kGetTextSignature = "(Ljava/lang/String;I)Ljava/lang/String;";
constexpr const char* kRequestBytesName = "requestBytes";
constexpr const char* kRequestBytesSignature =
    "(Ljava/lang/String;Ljava/lang/String;[BLjava/lang/String;[Ljava/lang/String;[Ljava/lang/String;II)[B";
constexpr const char* kEncryptPasswordPkcs1Name = "encryptPasswordPkcs1";
constexpr const char* kEncryptPasswordPkcs1Signature =
    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;";

std::mutex g_android_bridge_mutex;
JavaVM* g_android_java_vm = nullptr;
jobject g_android_application_context = nullptr;
jclass g_android_bridge_class = nullptr;
jclass g_android_http_bridge_class = nullptr;
jclass g_android_crypto_bridge_class = nullptr;
jmethodID g_android_connect_method = nullptr;
jmethodID g_android_send_binary_method = nullptr;
jmethodID g_android_receive_binary_method = nullptr;
jmethodID g_android_close_method = nullptr;
jmethodID g_android_get_text_method = nullptr;
jmethodID g_android_request_bytes_method = nullptr;
jmethodID g_android_encrypt_password_pkcs1_method = nullptr;

class ScopedEnvAttachment {
  public:
    ScopedEnvAttachment() = default;

    JNIEnv* env() {
        if (env_ != nullptr) {
            return env_;
        }

        JavaVM* vm = nullptr;
        {
            std::lock_guard lock{g_android_bridge_mutex};
            vm = g_android_java_vm;
        }
        if (vm == nullptr) {
            return nullptr;
        }

        if (vm->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) == JNI_OK) {
            return env_;
        }

        if (vm->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
            env_ = nullptr;
            return nullptr;
        }

        attached_ = true;
        return env_;
    }

    ~ScopedEnvAttachment() {
        if (!attached_) {
            return;
        }

        JavaVM* vm = nullptr;
        {
            std::lock_guard lock{g_android_bridge_mutex};
            vm = g_android_java_vm;
        }
        if (vm != nullptr) {
            vm->DetachCurrentThread();
        }
    }

  private:
    JNIEnv* env_ = nullptr;
    bool attached_ = false;
};

std::string describe_java_exception(JNIEnv* env) {
    if (env == nullptr || !env->ExceptionCheck()) {
        return {};
    }

    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();
    if (throwable == nullptr) {
        return "java exception";
    }

    jclass throwable_class = env->GetObjectClass(throwable);
    if (throwable_class == nullptr) {
        env->DeleteLocalRef(throwable);
        return "java exception";
    }

    const jmethodID to_string =
        env->GetMethodID(throwable_class, "toString", "()Ljava/lang/String;");
    if (to_string == nullptr) {
        env->DeleteLocalRef(throwable_class);
        env->DeleteLocalRef(throwable);
        return "java exception";
    }

    auto* text = static_cast<jstring>(env->CallObjectMethod(throwable, to_string));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(throwable_class);
        env->DeleteLocalRef(throwable);
        return "java exception";
    }

    std::string description = "java exception";
    if (text != nullptr) {
        const char* chars = env->GetStringUTFChars(text, nullptr);
        if (chars != nullptr) {
            description = chars;
            env->ReleaseStringUTFChars(text, chars);
        }
        env->DeleteLocalRef(text);
    }

    env->DeleteLocalRef(throwable_class);
    env->DeleteLocalRef(throwable);
    return description;
}

bool bridge_ready_unlocked() {
    return g_android_java_vm != nullptr && g_android_application_context != nullptr &&
           g_android_bridge_class != nullptr && g_android_connect_method != nullptr &&
           g_android_send_binary_method != nullptr &&
           g_android_receive_binary_method != nullptr &&
           g_android_close_method != nullptr &&
           g_android_http_bridge_class != nullptr &&
           g_android_get_text_method != nullptr &&
           g_android_request_bytes_method != nullptr &&
           g_android_crypto_bridge_class != nullptr &&
           g_android_encrypt_password_pkcs1_method != nullptr;
}

} // namespace

bool initialize_android_platform_bridge(JNIEnv* env, jobject application_context) {
    if (env == nullptr || application_context == nullptr) {
        return false;
    }

    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
        return false;
    }

    jclass local_bridge_class = env->FindClass(kBridgeClassName);
    if (local_bridge_class == nullptr) {
        (void)describe_java_exception(env);
        return false;
    }
    jclass local_http_bridge_class = env->FindClass(kHttpBridgeClassName);
    if (local_http_bridge_class == nullptr) {
        env->DeleteLocalRef(local_bridge_class);
        (void)describe_java_exception(env);
        return false;
    }
    jclass local_crypto_bridge_class = env->FindClass(kCryptoBridgeClassName);
    if (local_crypto_bridge_class == nullptr) {
        env->DeleteLocalRef(local_bridge_class);
        env->DeleteLocalRef(local_http_bridge_class);
        (void)describe_java_exception(env);
        return false;
    }

    const jmethodID connect = env->GetStaticMethodID(local_bridge_class, kConnectName,
                                                     kConnectSignature);
    const jmethodID send_binary = env->GetStaticMethodID(local_bridge_class, kSendBinaryName,
                                                         kSendBinarySignature);
    const jmethodID receive_binary =
        env->GetStaticMethodID(local_bridge_class, kReceiveBinaryName, kReceiveBinarySignature);
    const jmethodID close = env->GetStaticMethodID(local_bridge_class, kCloseName,
                                                   kCloseSignature);
    const jmethodID get_text =
        env->GetStaticMethodID(local_http_bridge_class, kGetTextName, kGetTextSignature);
    const jmethodID request_bytes =
        env->GetStaticMethodID(local_http_bridge_class, kRequestBytesName, kRequestBytesSignature);
    const jmethodID encrypt_password_pkcs1 = env->GetStaticMethodID(
        local_crypto_bridge_class, kEncryptPasswordPkcs1Name, kEncryptPasswordPkcs1Signature);
    if (connect == nullptr || send_binary == nullptr || receive_binary == nullptr ||
        close == nullptr || get_text == nullptr || request_bytes == nullptr ||
        encrypt_password_pkcs1 == nullptr) {
        env->DeleteLocalRef(local_bridge_class);
        env->DeleteLocalRef(local_http_bridge_class);
        env->DeleteLocalRef(local_crypto_bridge_class);
        (void)describe_java_exception(env);
        return false;
    }

    const jobject global_context = env->NewGlobalRef(application_context);
    const auto global_bridge_class = static_cast<jclass>(env->NewGlobalRef(local_bridge_class));
    const auto global_http_bridge_class =
        static_cast<jclass>(env->NewGlobalRef(local_http_bridge_class));
    const auto global_crypto_bridge_class =
        static_cast<jclass>(env->NewGlobalRef(local_crypto_bridge_class));
    env->DeleteLocalRef(local_bridge_class);
    env->DeleteLocalRef(local_http_bridge_class);
    env->DeleteLocalRef(local_crypto_bridge_class);
    if (global_context == nullptr || global_bridge_class == nullptr ||
        global_http_bridge_class == nullptr || global_crypto_bridge_class == nullptr) {
        if (global_context != nullptr) {
            env->DeleteGlobalRef(global_context);
        }
        if (global_bridge_class != nullptr) {
            env->DeleteGlobalRef(global_bridge_class);
        }
        if (global_http_bridge_class != nullptr) {
            env->DeleteGlobalRef(global_http_bridge_class);
        }
        if (global_crypto_bridge_class != nullptr) {
            env->DeleteGlobalRef(global_crypto_bridge_class);
        }
        (void)describe_java_exception(env);
        return false;
    }

    std::lock_guard lock{g_android_bridge_mutex};
    if (g_android_application_context != nullptr) {
        env->DeleteGlobalRef(g_android_application_context);
    }
    if (g_android_bridge_class != nullptr) {
        env->DeleteGlobalRef(g_android_bridge_class);
    }
    if (g_android_http_bridge_class != nullptr) {
        env->DeleteGlobalRef(g_android_http_bridge_class);
    }
    if (g_android_crypto_bridge_class != nullptr) {
        env->DeleteGlobalRef(g_android_crypto_bridge_class);
    }

    g_android_java_vm = vm;
    g_android_application_context = global_context;
    g_android_bridge_class = global_bridge_class;
    g_android_http_bridge_class = global_http_bridge_class;
    g_android_crypto_bridge_class = global_crypto_bridge_class;
    g_android_connect_method = connect;
    g_android_send_binary_method = send_binary;
    g_android_receive_binary_method = receive_binary;
    g_android_close_method = close;
    g_android_get_text_method = get_text;
    g_android_request_bytes_method = request_bytes;
    g_android_encrypt_password_pkcs1_method = encrypt_password_pkcs1;
    return true;
}

void shutdown_android_platform_bridge(JNIEnv* env) {
    std::lock_guard lock{g_android_bridge_mutex};
    if (env != nullptr && g_android_application_context != nullptr) {
        env->DeleteGlobalRef(g_android_application_context);
    }
    if (env != nullptr && g_android_bridge_class != nullptr) {
        env->DeleteGlobalRef(g_android_bridge_class);
    }
    if (env != nullptr && g_android_http_bridge_class != nullptr) {
        env->DeleteGlobalRef(g_android_http_bridge_class);
    }
    if (env != nullptr && g_android_crypto_bridge_class != nullptr) {
        env->DeleteGlobalRef(g_android_crypto_bridge_class);
    }
    g_android_application_context = nullptr;
    g_android_bridge_class = nullptr;
    g_android_http_bridge_class = nullptr;
    g_android_crypto_bridge_class = nullptr;
    g_android_connect_method = nullptr;
    g_android_send_binary_method = nullptr;
    g_android_receive_binary_method = nullptr;
    g_android_close_method = nullptr;
    g_android_get_text_method = nullptr;
    g_android_request_bytes_method = nullptr;
    g_android_encrypt_password_pkcs1_method = nullptr;
    g_android_java_vm = nullptr;
}

bool is_android_platform_bridge_available() {
    std::lock_guard lock{g_android_bridge_mutex};
    return bridge_ready_unlocked();
}

AndroidWebSocketConnectResult android_bridge_websocket_connect(std::string_view url,
                                                               std::int32_t connect_timeout_ms) {
    ScopedEnvAttachment attachment;
    JNIEnv* env = attachment.env();
    if (env == nullptr) {
        return {false, 0, "android platform bridge JNI environment is unavailable"};
    }

    jclass bridge_class = nullptr;
    jmethodID connect = nullptr;
    {
        std::lock_guard lock{g_android_bridge_mutex};
        if (!bridge_ready_unlocked()) {
            return {false, 0, "android platform bridge is not initialized"};
        }
        bridge_class = g_android_bridge_class;
        connect = g_android_connect_method;
    }

    const std::string url_text{url};
    jstring native_url = env->NewStringUTF(url_text.c_str());
    if (native_url == nullptr) {
        return {false, 0, "failed to allocate websocket URL string"};
    }

    const jlong handle = env->CallStaticLongMethod(bridge_class, connect, native_url,
                                                   static_cast<jint>(connect_timeout_ms));
    env->DeleteLocalRef(native_url);
    if (env->ExceptionCheck()) {
        return {false, 0, describe_java_exception(env)};
    }

    if (handle == 0) {
        return {false, 0, "android websocket bridge returned an invalid handle"};
    }

    return {true, static_cast<std::int64_t>(handle), ""};
}

AndroidWebSocketCallResult android_bridge_websocket_send(std::int64_t handle,
                                                         const std::vector<std::uint8_t>& bytes) {
    ScopedEnvAttachment attachment;
    JNIEnv* env = attachment.env();
    if (env == nullptr) {
        return {false, "android platform bridge JNI environment is unavailable"};
    }

    jclass bridge_class = nullptr;
    jmethodID send_binary = nullptr;
    {
        std::lock_guard lock{g_android_bridge_mutex};
        if (!bridge_ready_unlocked()) {
            return {false, "android platform bridge is not initialized"};
        }
        bridge_class = g_android_bridge_class;
        send_binary = g_android_send_binary_method;
    }

    const auto size = static_cast<jsize>(bytes.size());
    jbyteArray payload = env->NewByteArray(size);
    if (payload == nullptr) {
        return {false, "failed to allocate websocket payload"};
    }
    if (size > 0) {
        env->SetByteArrayRegion(payload, 0, size,
                                reinterpret_cast<const jbyte*>(bytes.data()));
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(payload);
            return {false, describe_java_exception(env)};
        }
    }

    env->CallStaticVoidMethod(bridge_class, send_binary, static_cast<jlong>(handle), payload);
    env->DeleteLocalRef(payload);
    if (env->ExceptionCheck()) {
        return {false, describe_java_exception(env)};
    }

    return {true, ""};
}

AndroidWebSocketReceiveResult android_bridge_websocket_receive(std::int64_t handle,
                                                               std::int32_t receive_timeout_ms) {
    ScopedEnvAttachment attachment;
    JNIEnv* env = attachment.env();
    if (env == nullptr) {
        return {false, "android platform bridge JNI environment is unavailable", {}};
    }

    jclass bridge_class = nullptr;
    jmethodID receive_binary = nullptr;
    {
        std::lock_guard lock{g_android_bridge_mutex};
        if (!bridge_ready_unlocked()) {
            return {false, "android platform bridge is not initialized", {}};
        }
        bridge_class = g_android_bridge_class;
        receive_binary = g_android_receive_binary_method;
    }

    auto* payload = static_cast<jbyteArray>(env->CallStaticObjectMethod(
        bridge_class, receive_binary, static_cast<jlong>(handle),
        static_cast<jint>(receive_timeout_ms)));
    if (env->ExceptionCheck()) {
        return {false, describe_java_exception(env), {}};
    }
    if (payload == nullptr) {
        return {false, "android websocket bridge returned no frame", {}};
    }

    const auto size = env->GetArrayLength(payload);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        env->GetByteArrayRegion(payload, 0, size, reinterpret_cast<jbyte*>(bytes.data()));
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(payload);
            return {false, describe_java_exception(env), {}};
        }
    }
    env->DeleteLocalRef(payload);
    return {true, "", std::move(bytes)};
}

AndroidHttpTextResult android_bridge_http_get_text(std::string_view url, std::int32_t timeout_ms) {
    const auto response =
        android_bridge_http_request("GET", url, {}, "", {}, timeout_ms, timeout_ms);
    if (!response.ok) {
        return {false, response.error_message, ""};
    }
    return {true, "", std::string{response.body.begin(), response.body.end()}};
}

AndroidHttpResponse android_bridge_http_request(std::string_view method, std::string_view url,
                                                const std::vector<std::uint8_t>& body,
                                                std::string_view content_type,
                                                const std::vector<cauth::core::platform::HttpHeader>& headers,
                                                std::int32_t connect_timeout_ms,
                                                std::int32_t read_timeout_ms) {
    ScopedEnvAttachment attachment;
    JNIEnv* env = attachment.env();
    if (env == nullptr) {
        return {false, "android platform bridge JNI environment is unavailable", 0, {}};
    }

    jclass bridge_class = nullptr;
    jmethodID request_bytes = nullptr;
    {
        std::lock_guard lock{g_android_bridge_mutex};
        if (!bridge_ready_unlocked()) {
            return {false, "android platform bridge is not initialized", 0, {}};
        }
        bridge_class = g_android_http_bridge_class;
        request_bytes = g_android_request_bytes_method;
    }

    const std::string method_text{method};
    const std::string url_text{url};
    const std::string content_type_text{content_type};
    jstring native_method = env->NewStringUTF(method_text.c_str());
    jstring native_url = env->NewStringUTF(url_text.c_str());
    jstring native_content_type =
        env->NewStringUTF(content_type_text.empty() ? "" : content_type_text.c_str());
    jbyteArray native_body = env->NewByteArray(static_cast<jsize>(body.size()));
    jobjectArray native_header_names =
        env->NewObjectArray(static_cast<jsize>(headers.size()), env->FindClass("java/lang/String"), nullptr);
    jobjectArray native_header_values =
        env->NewObjectArray(static_cast<jsize>(headers.size()), env->FindClass("java/lang/String"), nullptr);
    if (native_method == nullptr || native_url == nullptr || native_content_type == nullptr ||
        native_body == nullptr || native_header_names == nullptr || native_header_values == nullptr) {
        if (native_method != nullptr) env->DeleteLocalRef(native_method);
        if (native_url != nullptr) env->DeleteLocalRef(native_url);
        if (native_content_type != nullptr) env->DeleteLocalRef(native_content_type);
        if (native_body != nullptr) env->DeleteLocalRef(native_body);
        if (native_header_names != nullptr) env->DeleteLocalRef(native_header_names);
        if (native_header_values != nullptr) env->DeleteLocalRef(native_header_values);
        return {false, "failed to allocate HTTP request arguments", 0, {}};
    }
    if (!body.empty()) {
        env->SetByteArrayRegion(native_body, 0, static_cast<jsize>(body.size()),
                                reinterpret_cast<const jbyte*>(body.data()));
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(native_method);
            env->DeleteLocalRef(native_url);
            env->DeleteLocalRef(native_content_type);
            env->DeleteLocalRef(native_body);
            env->DeleteLocalRef(native_header_names);
            env->DeleteLocalRef(native_header_values);
            return {false, describe_java_exception(env), 0, {}};
        }
    }
    for (jsize index = 0; index < static_cast<jsize>(headers.size()); ++index) {
        jstring name = env->NewStringUTF(headers[static_cast<std::size_t>(index)].name.c_str());
        jstring value = env->NewStringUTF(headers[static_cast<std::size_t>(index)].value.c_str());
        if (name == nullptr || value == nullptr) {
            if (name != nullptr) env->DeleteLocalRef(name);
            if (value != nullptr) env->DeleteLocalRef(value);
            env->DeleteLocalRef(native_method);
            env->DeleteLocalRef(native_url);
            env->DeleteLocalRef(native_content_type);
            env->DeleteLocalRef(native_body);
            env->DeleteLocalRef(native_header_names);
            env->DeleteLocalRef(native_header_values);
            return {false, "failed to allocate HTTP request header strings", 0, {}};
        }
        env->SetObjectArrayElement(native_header_names, index, name);
        env->SetObjectArrayElement(native_header_values, index, value);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(value);
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(native_method);
            env->DeleteLocalRef(native_url);
            env->DeleteLocalRef(native_content_type);
            env->DeleteLocalRef(native_body);
            env->DeleteLocalRef(native_header_names);
            env->DeleteLocalRef(native_header_values);
            return {false, describe_java_exception(env), 0, {}};
        }
    }

    auto* response_body = static_cast<jbyteArray>(env->CallStaticObjectMethod(
        bridge_class, request_bytes, native_method, native_url, native_body, native_content_type,
        native_header_names, native_header_values,
        static_cast<jint>(connect_timeout_ms), static_cast<jint>(read_timeout_ms)));
    env->DeleteLocalRef(native_method);
    env->DeleteLocalRef(native_url);
    env->DeleteLocalRef(native_content_type);
    env->DeleteLocalRef(native_body);
    env->DeleteLocalRef(native_header_names);
    env->DeleteLocalRef(native_header_values);
    if (env->ExceptionCheck()) {
        return {false, describe_java_exception(env), 0, {}};
    }
    if (response_body == nullptr) {
        return {false, "android HTTP bridge returned no response body", 0, {}};
    }

    const auto size = env->GetArrayLength(response_body);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        env->GetByteArrayRegion(response_body, 0, size, reinterpret_cast<jbyte*>(bytes.data()));
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(response_body);
            return {false, describe_java_exception(env), 0, {}};
        }
    }
    env->DeleteLocalRef(response_body);
    return {true, "", 200, std::move(bytes)};
}

AndroidRsaEncryptResult android_bridge_encrypt_password_pkcs1(
    std::string_view modulus_hex,
    std::string_view exponent_hex,
    std::string_view password) {
    ScopedEnvAttachment attachment;
    JNIEnv* env = attachment.env();
    if (env == nullptr) {
        return {false, "android platform bridge JNI environment is unavailable", ""};
    }

    jclass bridge_class = nullptr;
    jmethodID encrypt_method = nullptr;
    {
        std::lock_guard lock{g_android_bridge_mutex};
        if (!bridge_ready_unlocked()) {
            return {false, "android platform bridge is not initialized", ""};
        }
        bridge_class = g_android_crypto_bridge_class;
        encrypt_method = g_android_encrypt_password_pkcs1_method;
    }

    const std::string modulus_text{modulus_hex};
    const std::string exponent_text{exponent_hex};
    const std::string password_text{password};
    jstring native_modulus = env->NewStringUTF(modulus_text.c_str());
    jstring native_exponent = env->NewStringUTF(exponent_text.c_str());
    jstring native_password = env->NewStringUTF(password_text.c_str());
    if (native_modulus == nullptr || native_exponent == nullptr || native_password == nullptr) {
        if (native_modulus != nullptr) env->DeleteLocalRef(native_modulus);
        if (native_exponent != nullptr) env->DeleteLocalRef(native_exponent);
        if (native_password != nullptr) env->DeleteLocalRef(native_password);
        return {false, "failed to allocate RSA encrypt strings", ""};
    }

    auto* ciphertext = static_cast<jstring>(env->CallStaticObjectMethod(
        bridge_class, encrypt_method, native_modulus, native_exponent, native_password));
    env->DeleteLocalRef(native_modulus);
    env->DeleteLocalRef(native_exponent);
    env->DeleteLocalRef(native_password);
    if (env->ExceptionCheck()) {
        return {false, describe_java_exception(env), ""};
    }
    if (ciphertext == nullptr) {
        return {false, "android crypto bridge returned no ciphertext", ""};
    }

    std::string base64;
    const char* chars = env->GetStringUTFChars(ciphertext, nullptr);
    if (chars == nullptr) {
        env->DeleteLocalRef(ciphertext);
        if (env->ExceptionCheck()) {
            return {false, describe_java_exception(env), ""};
        }
        return {false, "failed to read ciphertext text", ""};
    }

    base64 = chars;
    env->ReleaseStringUTFChars(ciphertext, chars);
    env->DeleteLocalRef(ciphertext);
    return {true, "", std::move(base64)};
}

void android_bridge_websocket_close(std::int64_t handle) {
    ScopedEnvAttachment attachment;
    JNIEnv* env = attachment.env();
    if (env == nullptr) {
        return;
    }

    jclass bridge_class = nullptr;
    jmethodID close = nullptr;
    {
        std::lock_guard lock{g_android_bridge_mutex};
        if (!bridge_ready_unlocked()) {
            return;
        }
        bridge_class = g_android_bridge_class;
        close = g_android_close_method;
    }

    env->CallStaticVoidMethod(bridge_class, close, static_cast<jlong>(handle));
    if (env->ExceptionCheck()) {
        (void)describe_java_exception(env);
    }
}

} // namespace cauth::core::runtime

#else

namespace cauth::core::runtime {

bool is_android_platform_bridge_available() {
    return false;
}

} // namespace cauth::core::runtime

#endif
