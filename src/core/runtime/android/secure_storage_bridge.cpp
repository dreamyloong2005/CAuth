#include "core/runtime/android/secure_storage_bridge.hpp"

#include "core/runtime/android/bridge.hpp"

#ifdef __ANDROID__

#include <jni.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cauth::core::runtime {
namespace {

std::mutex g_bridge_mutex;
JavaVM* g_java_vm = nullptr;
jobject g_application_context = nullptr;

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        hex.push_back(kHex[(byte >> 4) & 0x0f]);
        hex.push_back(kHex[byte & 0x0f]);
    }
    return hex;
}

std::optional<std::vector<std::uint8_t>> hex_to_bytes(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }

    auto decode_nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };

    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t index = 0; index < hex.size(); index += 2) {
        const auto high = decode_nibble(hex[index]);
        const auto low = decode_nibble(hex[index + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return bytes;
}

bool clear_pending_exception(JNIEnv* env) {
    if (!env->ExceptionCheck()) {
        return false;
    }
    env->ExceptionClear();
    return true;
}

class ScopedEnvAttachment {
  public:
    ScopedEnvAttachment() = default;

    JNIEnv* env() {
        if (env_ != nullptr) {
            return env_;
        }

        if (g_java_vm == nullptr) {
            return nullptr;
        }

        if (g_java_vm->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) == JNI_OK) {
            return env_;
        }

        if (g_java_vm->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
            env_ = nullptr;
            return nullptr;
        }

        attached_ = true;
        return env_;
    }

    ~ScopedEnvAttachment() {
        if (attached_ && g_java_vm != nullptr) {
            g_java_vm->DetachCurrentThread();
        }
    }

  private:
    JNIEnv* env_ = nullptr;
    bool attached_ = false;
};

class JniAndroidSecureStorageBridge final : public AndroidSecureStorageBridge {
  public:
    explicit JniAndroidSecureStorageBridge(AndroidSecureStorageConfig config)
        : config_(std::move(config)) {}

    void save_bytes(std::vector<std::uint8_t> bytes) override {
        const auto value = bytes_to_hex(bytes);
        with_preferences_editor([&](JNIEnv* env, jobject editor, jclass editor_class) {
            const jmethodID put_string = env->GetMethodID(
                editor_class, "putString",
                "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");
            const jmethodID apply = env->GetMethodID(editor_class, "apply", "()V");
            if (put_string == nullptr || apply == nullptr) {
                clear_pending_exception(env);
                return false;
            }

            jstring key = env->NewStringUTF(config_.session_key.c_str());
            jstring stored = env->NewStringUTF(value.c_str());
            if (key == nullptr || stored == nullptr) {
                if (key != nullptr) {
                    env->DeleteLocalRef(key);
                }
                if (stored != nullptr) {
                    env->DeleteLocalRef(stored);
                }
                clear_pending_exception(env);
                return false;
            }

            env->CallObjectMethod(editor, put_string, key, stored);
            if (clear_pending_exception(env)) {
                env->DeleteLocalRef(key);
                env->DeleteLocalRef(stored);
                return false;
            }

            env->CallVoidMethod(editor, apply);
            const bool failed = clear_pending_exception(env);
            env->DeleteLocalRef(key);
            env->DeleteLocalRef(stored);
            return !failed;
        });
    }

    std::optional<std::vector<std::uint8_t>> load_bytes() const override {
        ScopedEnvAttachment attachment;
        JNIEnv* env = attachment.env();
        if (env == nullptr) {
            return std::nullopt;
        }

        jobject prefs = open_preferences(env);
        if (prefs == nullptr) {
            return std::nullopt;
        }

        jclass prefs_class = env->FindClass("android/content/SharedPreferences");
        if (prefs_class == nullptr) {
            env->DeleteLocalRef(prefs);
            clear_pending_exception(env);
            return std::nullopt;
        }

        const jmethodID get_string = env->GetMethodID(
            prefs_class, "getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        if (get_string == nullptr) {
            env->DeleteLocalRef(prefs_class);
            env->DeleteLocalRef(prefs);
            clear_pending_exception(env);
            return std::nullopt;
        }

        jstring key = env->NewStringUTF(config_.session_key.c_str());
        if (key == nullptr) {
            env->DeleteLocalRef(prefs_class);
            env->DeleteLocalRef(prefs);
            clear_pending_exception(env);
            return std::nullopt;
        }

        auto* value = static_cast<jstring>(env->CallObjectMethod(prefs, get_string, key, nullptr));
        const bool failed = clear_pending_exception(env);
        env->DeleteLocalRef(key);
        env->DeleteLocalRef(prefs_class);
        env->DeleteLocalRef(prefs);
        if (failed || value == nullptr) {
            if (value != nullptr) {
                env->DeleteLocalRef(value);
            }
            return std::nullopt;
        }

        const char* chars = env->GetStringUTFChars(value, nullptr);
        if (chars == nullptr) {
            env->DeleteLocalRef(value);
            clear_pending_exception(env);
            return std::nullopt;
        }

        std::string hex{chars};
        env->ReleaseStringUTFChars(value, chars);
        env->DeleteLocalRef(value);
        return hex_to_bytes(hex);
    }

    void clear_bytes() override {
        with_preferences_editor([&](JNIEnv* env, jobject editor, jclass editor_class) {
            const jmethodID remove = env->GetMethodID(
                editor_class, "remove",
                "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");
            const jmethodID apply = env->GetMethodID(editor_class, "apply", "()V");
            if (remove == nullptr || apply == nullptr) {
                clear_pending_exception(env);
                return false;
            }

            jstring key = env->NewStringUTF(config_.session_key.c_str());
            if (key == nullptr) {
                clear_pending_exception(env);
                return false;
            }

            env->CallObjectMethod(editor, remove, key);
            if (clear_pending_exception(env)) {
                env->DeleteLocalRef(key);
                return false;
            }

            env->CallVoidMethod(editor, apply);
            const bool failed = clear_pending_exception(env);
            env->DeleteLocalRef(key);
            return !failed;
        });
    }

  private:
    jobject open_preferences(JNIEnv* env) const {
        std::lock_guard lock{g_bridge_mutex};
        if (g_application_context == nullptr) {
            return nullptr;
        }

        jclass context_class = env->FindClass("android/content/Context");
        if (context_class == nullptr) {
            clear_pending_exception(env);
            return nullptr;
        }

        const jmethodID get_shared_preferences = env->GetMethodID(
            context_class, "getSharedPreferences",
            "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");
        if (get_shared_preferences == nullptr) {
            env->DeleteLocalRef(context_class);
            clear_pending_exception(env);
            return nullptr;
        }

        jstring prefs_name = env->NewStringUTF(config_.preferences_name.c_str());
        if (prefs_name == nullptr) {
            env->DeleteLocalRef(context_class);
            clear_pending_exception(env);
            return nullptr;
        }

        jobject prefs =
            env->CallObjectMethod(g_application_context, get_shared_preferences, prefs_name, 0);
        const bool failed = clear_pending_exception(env);
        env->DeleteLocalRef(prefs_name);
        env->DeleteLocalRef(context_class);
        if (failed) {
            return nullptr;
        }
        return prefs;
    }

    template <typename Callback> bool with_preferences_editor(Callback&& callback) const {
        ScopedEnvAttachment attachment;
        JNIEnv* env = attachment.env();
        if (env == nullptr) {
            return false;
        }

        jobject prefs = open_preferences(env);
        if (prefs == nullptr) {
            return false;
        }

        jclass prefs_class = env->FindClass("android/content/SharedPreferences");
        if (prefs_class == nullptr) {
            env->DeleteLocalRef(prefs);
            clear_pending_exception(env);
            return false;
        }

        const jmethodID edit = env->GetMethodID(
            prefs_class, "edit", "()Landroid/content/SharedPreferences$Editor;");
        if (edit == nullptr) {
            env->DeleteLocalRef(prefs_class);
            env->DeleteLocalRef(prefs);
            clear_pending_exception(env);
            return false;
        }

        jobject editor = env->CallObjectMethod(prefs, edit);
        if (clear_pending_exception(env) || editor == nullptr) {
            if (editor != nullptr) {
                env->DeleteLocalRef(editor);
            }
            env->DeleteLocalRef(prefs_class);
            env->DeleteLocalRef(prefs);
            return false;
        }

        jclass editor_class = env->FindClass("android/content/SharedPreferences$Editor");
        if (editor_class == nullptr) {
            env->DeleteLocalRef(editor);
            env->DeleteLocalRef(prefs_class);
            env->DeleteLocalRef(prefs);
            clear_pending_exception(env);
            return false;
        }

        const bool ok = callback(env, editor, editor_class);
        env->DeleteLocalRef(editor_class);
        env->DeleteLocalRef(editor);
        env->DeleteLocalRef(prefs_class);
        env->DeleteLocalRef(prefs);
        return ok;
    }

  private:
    AndroidSecureStorageConfig config_;
};

JniAndroidSecureStorageBridge g_bridge{AndroidSecureStorageConfig{}};
bool g_bridge_ready = false;

bool initialize_android_secure_storage_bridge(JNIEnv* env, jobject application_context) {
    if (env == nullptr || application_context == nullptr) {
        return false;
    }

    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
        return false;
    }

    const jobject global_context = env->NewGlobalRef(application_context);
    if (global_context == nullptr) {
        clear_pending_exception(env);
        return false;
    }

    std::lock_guard lock{g_bridge_mutex};
    if (g_application_context != nullptr) {
        env->DeleteGlobalRef(g_application_context);
        g_application_context = nullptr;
    }

    g_java_vm = vm;
    g_application_context = global_context;
    g_bridge_ready = true;
    return true;
}

void shutdown_android_secure_storage_bridge(JNIEnv* env) {
    std::lock_guard lock{g_bridge_mutex};
    g_bridge_ready = false;
    if (env != nullptr && g_application_context != nullptr) {
        env->DeleteGlobalRef(g_application_context);
    }
    g_application_context = nullptr;
    g_java_vm = nullptr;
}

} // namespace

bool is_android_secure_storage_bridge_available() {
    std::lock_guard lock{g_bridge_mutex};
    return g_bridge_ready && g_application_context != nullptr && g_java_vm != nullptr;
}

AndroidSecureStorageBridge* get_android_secure_storage_bridge() {
    std::lock_guard lock{g_bridge_mutex};
    return g_bridge_ready ? &g_bridge : nullptr;
}

std::unique_ptr<AndroidSecureStorageBridge> create_android_secure_storage_bridge(
    AndroidSecureStorageConfig config) {
    std::lock_guard lock{g_bridge_mutex};
    if (!g_bridge_ready || g_application_context == nullptr || g_java_vm == nullptr) {
        return nullptr;
    }
    return std::make_unique<JniAndroidSecureStorageBridge>(std::move(config));
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_CAuthAndroidRuntime_nativeOnAttachedToRuntime(JNIEnv* env,
                                                                     jclass,
                                                                     jobject application_context) {
    initialize_android_secure_storage_bridge(env, application_context);
    initialize_android_platform_bridge(env, application_context);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_CAuthAndroidRuntime_nativeOnDetachedFromRuntime(JNIEnv* env, jclass) {
    shutdown_android_platform_bridge(env);
    shutdown_android_secure_storage_bridge(env);
}

} // namespace cauth::core::runtime

#else

namespace cauth::core::runtime {

bool is_android_secure_storage_bridge_available() {
    return false;
}

AndroidSecureStorageBridge* get_android_secure_storage_bridge() {
    return nullptr;
}

std::unique_ptr<AndroidSecureStorageBridge> create_android_secure_storage_bridge(
    AndroidSecureStorageConfig) {
    return nullptr;
}

} // namespace cauth::core::runtime

#endif
