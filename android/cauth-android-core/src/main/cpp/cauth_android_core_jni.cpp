#include "cauth/core_ffi.h"

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
Java_com_cauth_android_CAuthNativeCore_nativeCreateClient(JNIEnv* env, jclass) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "nativeCreateClient start");
    cauth_client_t* client = nullptr;
    const cauth_result_t result = cauth_client_create(&client);
    if (result != CAUTH_OK || client == nullptr) {
        throw_result_exception(env, "Failed to create CAuth client", result);
        return 0;
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "nativeCreateClient ok handle=%p", client);
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
