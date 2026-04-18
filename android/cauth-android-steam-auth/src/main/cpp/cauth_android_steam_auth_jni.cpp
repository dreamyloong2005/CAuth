#include "cauth/steam_auth_ffi.h"

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <string>

namespace {

constexpr const char* kIllegalStateException = "java/lang/IllegalStateException";
constexpr const char* kLogTag = "CAuthNative";
constexpr const char* kLoginResultSnapshotClassName =
    "com/cauth/android/steam/auth/LoginResultSnapshot";
constexpr const char* kSavedSessionSnapshotClassName =
    "com/cauth/android/steam/auth/SavedSessionSnapshot";
constexpr const char* kCmProbeSnapshotClassName =
    "com/cauth/android/steam/auth/CmProbeSnapshot";
constexpr const char* kCmLogonSnapshotClassName =
    "com/cauth/android/steam/auth/CmLogonSnapshot";

jclass g_login_result_snapshot_class = nullptr;
jclass g_saved_session_snapshot_class = nullptr;
jclass g_cm_probe_snapshot_class = nullptr;
jclass g_cm_logon_snapshot_class = nullptr;

cauth_client_t* client_from_handle(jlong handle) {
    return reinterpret_cast<cauth_client_t*>(static_cast<std::intptr_t>(handle));
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

jclass require_global_class(JNIEnv* env, jclass& slot, const char* name) {
    if (slot != nullptr) {
        return slot;
    }
    jclass local_class = env->FindClass(name);
    if (local_class == nullptr) {
        return nullptr;
    }
    slot = static_cast<jclass>(env->NewGlobalRef(local_class));
    env->DeleteLocalRef(local_class);
    return slot;
}

jclass ensure_login_result_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_login_result_snapshot_class, kLoginResultSnapshotClassName);
}

jclass ensure_saved_session_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_saved_session_snapshot_class, kSavedSessionSnapshotClassName);
}

jclass ensure_cm_probe_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_cm_probe_snapshot_class, kCmProbeSnapshotClassName);
}

jclass ensure_cm_logon_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_cm_logon_snapshot_class, kCmLogonSnapshotClassName);
}

jobject make_login_result(JNIEnv* env, const cauth_login_result_t& result) {
    jclass cls = ensure_login_result_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(IILjava/lang/String;JLjava/lang/String;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring message = env->NewStringUTF(result.message == nullptr ? "" : result.message);
    jstring account_name = result.account_name == nullptr ? nullptr
                                                          : env->NewStringUTF(result.account_name);
    jobject instance = env->NewObject(cls, ctor, static_cast<jint>(result.status),
                                      static_cast<jint>(result.result), message,
                                      static_cast<jlong>(result.steam_id), account_name);
    env->DeleteLocalRef(message);
    if (account_name != nullptr) {
        env->DeleteLocalRef(account_name);
    }
    return instance;
}

jobject make_saved_session(JNIEnv* env, const cauth_saved_session_t& session) {
    jclass cls = ensure_saved_session_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZJLjava/lang/String;ZZJ)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring account_name = session.account_name == nullptr ? nullptr
                                                           : env->NewStringUTF(session.account_name);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(session.present != 0), static_cast<jlong>(session.steam_id),
        account_name, static_cast<jboolean>(session.has_refresh_token != 0),
        static_cast<jboolean>(session.has_access_token != 0),
        static_cast<jlong>(session.created_at_unix_seconds));
    if (account_name != nullptr) {
        env->DeleteLocalRef(account_name);
    }
    return instance;
}

jobject make_cm_probe(JNIEnv* env, const cauth_cm_probe_result_t& result) {
    jclass cls = ensure_cm_probe_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZLjava/lang/String;Ljava/lang/String;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring endpoint = result.endpoint == nullptr ? nullptr : env->NewStringUTF(result.endpoint);
    jstring status = result.status == nullptr ? nullptr : env->NewStringUTF(result.status);
    jobject instance = env->NewObject(cls, ctor, static_cast<jboolean>(result.ok != 0), endpoint,
                                      status);
    if (endpoint != nullptr) {
        env->DeleteLocalRef(endpoint);
    }
    if (status != nullptr) {
        env->DeleteLocalRef(status);
    }
    return instance;
}

jobject make_cm_logon(JNIEnv* env, const cauth_cm_logon_result_t& result) {
    jclass cls = ensure_cm_logon_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor =
        env->GetMethodID(cls, "<init>", "(ZLjava/lang/String;Ljava/lang/String;IIIJ)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring endpoint = result.endpoint == nullptr ? nullptr : env->NewStringUTF(result.endpoint);
    jstring status = result.status == nullptr ? nullptr : env->NewStringUTF(result.status);
    jobject instance = env->NewObject(cls, ctor, static_cast<jboolean>(result.ok != 0), endpoint,
                                      status, static_cast<jint>(result.eresult),
                                      static_cast<jint>(result.eresult_extended),
                                      static_cast<jint>(result.heartbeat_seconds),
                                      static_cast<jlong>(result.steam_id));
    if (endpoint != nullptr) {
        env->DeleteLocalRef(endpoint);
    }
    if (status != nullptr) {
        env->DeleteLocalRef(status);
    }
    return instance;
}

} // namespace

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_auth_CAuthNativeSteamAuth_nativeLoginPassword(
    JNIEnv* env,
    jclass,
    jlong handle,
    jstring account_name,
    jstring password,
    jstring steam_guard_code,
    jstring device_name,
    jboolean remember_session,
    jint platform_type) {
    const char* account_name_chars =
        account_name == nullptr ? nullptr : env->GetStringUTFChars(account_name, nullptr);
    const char* password_chars =
        password == nullptr ? nullptr : env->GetStringUTFChars(password, nullptr);
    const char* guard_code_chars = steam_guard_code == nullptr
                                       ? nullptr
                                       : env->GetStringUTFChars(steam_guard_code, nullptr);
    const char* device_name_chars =
        device_name == nullptr ? nullptr : env->GetStringUTFChars(device_name, nullptr);

    cauth_login_request_t request{};
    request.account_name = account_name_chars;
    request.password = password_chars;
    request.steam_guard_code = guard_code_chars;
    request.device_name = device_name_chars;
    request.remember_session = remember_session ? 1 : 0;
    request.platform_type = static_cast<int>(platform_type);

    cauth_login_result_t result{};
    const cauth_result_t native_result =
        cauth_auth_login_password(client_from_handle(handle), &request, &result);

    if (account_name_chars != nullptr) {
        env->ReleaseStringUTFChars(account_name, account_name_chars);
    }
    if (password_chars != nullptr) {
        env->ReleaseStringUTFChars(password, password_chars);
    }
    if (guard_code_chars != nullptr) {
        env->ReleaseStringUTFChars(steam_guard_code, guard_code_chars);
    }
    if (device_name_chars != nullptr) {
        env->ReleaseStringUTFChars(device_name, device_name_chars);
    }

    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Steam login failed", native_result);
        return nullptr;
    }
    return make_login_result(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_auth_CAuthNativeSteamAuth_nativeGetSavedSession(
    JNIEnv* env,
    jclass,
    jlong handle) {
    cauth_saved_session_t session{};
    const cauth_result_t result = cauth_auth_get_saved_session(client_from_handle(handle), &session);
    if (result != CAUTH_OK) {
        throw_result_exception(env, "Failed to load saved session", result);
        return nullptr;
    }
    return make_saved_session(env, session);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_auth_CAuthNativeSteamAuth_nativeClearSavedSession(
    JNIEnv* env,
    jclass,
    jlong handle) {
    const cauth_result_t result = cauth_auth_clear_saved_session(client_from_handle(handle));
    if (result != CAUTH_OK) {
        throw_result_exception(env, "Failed to clear saved session", result);
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_auth_CAuthNativeSteamAuth_nativeCmProbe(JNIEnv* env, jclass) {
    cauth_cm_probe_result_t result{};
    const cauth_result_t native_result = cauth_cm_probe(&result);
    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "CM probe failed", native_result);
        return nullptr;
    }
    return make_cm_probe(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_auth_CAuthNativeSteamAuth_nativeCmLogon(
    JNIEnv* env,
    jclass,
    jlong handle) {
    cauth_cm_logon_result_t result{};
    const cauth_result_t native_result = cauth_cm_logon(client_from_handle(handle), &result);
    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "CM logon failed", native_result);
        return nullptr;
    }
    return make_cm_logon(env, result);
}
