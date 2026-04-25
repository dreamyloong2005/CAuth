#include "cauth/core_ffi.h"
#include "core/platform/operation_cancel.hpp"

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <string>

namespace {

constexpr const char* kIllegalStateException = "java/lang/IllegalStateException";
constexpr const char* kLogTag = "CAuthNative";

cauth_client_t* client_from_handle(jlong handle) {
    return reinterpret_cast<cauth_client_t*>(static_cast<std::intptr_t>(handle));
}

jlong handle_from_client(cauth_client_t* client) {
    return static_cast<jlong>(reinterpret_cast<std::intptr_t>(client));
}

cauth_client_options_t make_client_options(jint session_storage_kind,
                                           const char* session_storage_path,
                                           const char* session_storage_namespace,
                                           const char* session_storage_key) {
    cauth_client_options_t options{};
    options.session_storage_kind = static_cast<cauth_session_storage_kind_t>(session_storage_kind);
    options.session_storage_path = session_storage_path;
    options.session_storage_namespace = session_storage_namespace;
    options.session_storage_key = session_storage_key;
    return options;
}

void throw_result_exception(JNIEnv* env, const char* prefix, cauth_result_t result) {
    std::string message = prefix == nullptr ? "CAuth native error" : std::string{prefix};
    const char* detail = cauth_result_message(result);
    if (detail != nullptr && detail[0] != '\0') {
        message.append(": ");
        message.append(detail);
    }
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message.c_str());
    env->ThrowNew(env->FindClass(kIllegalStateException), message.c_str());
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_cauth_android_CAuthNativeCore_nativeGetVersionString(JNIEnv* env, jclass) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "nativeGetVersionString");
    const cauth_version_t version = cauth_get_version();
    const std::string value = std::to_string(version.major) + "." + std::to_string(version.minor) +
                              "." + std::to_string(version.patch);
    return env->NewStringUTF(value.c_str());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cauth_android_CAuthNativeCore_nativeCreateClientWithOptions(
    JNIEnv* env,
    jclass,
    jint session_storage_kind,
    jstring session_storage_path,
    jstring session_storage_namespace,
    jstring session_storage_key) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "nativeCreateClientWithOptions start");
    const char* session_storage_path_chars =
        session_storage_path == nullptr ? nullptr : env->GetStringUTFChars(session_storage_path, nullptr);
    const char* session_storage_namespace_chars =
        session_storage_namespace == nullptr ? nullptr : env->GetStringUTFChars(session_storage_namespace, nullptr);
    const char* session_storage_key_chars =
        session_storage_key == nullptr ? nullptr : env->GetStringUTFChars(session_storage_key, nullptr);

    cauth_client_t* client = nullptr;
    const auto options = make_client_options(
        session_storage_kind,
        session_storage_path_chars,
        session_storage_namespace_chars,
        session_storage_key_chars);
    const cauth_result_t result = cauth_client_create_with_options(&options, &client);

    if (session_storage_path_chars != nullptr) {
        env->ReleaseStringUTFChars(session_storage_path, session_storage_path_chars);
    }
    if (session_storage_namespace_chars != nullptr) {
        env->ReleaseStringUTFChars(session_storage_namespace, session_storage_namespace_chars);
    }
    if (session_storage_key_chars != nullptr) {
        env->ReleaseStringUTFChars(session_storage_key, session_storage_key_chars);
    }

    if (result != CAUTH_OK || client == nullptr) {
        throw_result_exception(env, "Failed to create CAuth client", result);
        return 0;
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "nativeCreateClientWithOptions ok handle=%p", client);
    return handle_from_client(client);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_CAuthNativeCore_nativeDestroyClient(JNIEnv*, jclass, jlong handle) {
    if (handle != 0) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "nativeDestroyClient handle=%p",
                            client_from_handle(handle));
        cauth_client_destroy(client_from_handle(handle));
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_cauth_android_CAuthNativeCore_nativeIsOperationCanceled(JNIEnv*, jclass) {
    return cauth::core::platform::current_thread_operation_cancel_requested() ? JNI_TRUE
                                                                              : JNI_FALSE;
}
