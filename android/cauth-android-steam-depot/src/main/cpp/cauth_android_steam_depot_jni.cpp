#include "cauth/steam_depot_ffi.h"
#include "steam/depot/steam_depot_application.hpp"

#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {

constexpr const char* kIllegalStateException = "java/lang/IllegalStateException";
constexpr const char* kLogTag = "CAuthNative";
constexpr const char* kAppBranchEntrySnapshotClassName =
    "com/cauth/android/steam/depot/AppBranchEntrySnapshot";
constexpr const char* kAppBranchListSnapshotClassName =
    "com/cauth/android/steam/depot/AppBranchListSnapshot";
constexpr const char* kDepotManifestEntrySnapshotClassName =
    "com/cauth/android/steam/depot/DepotManifestEntrySnapshot";
constexpr const char* kDepotManifestListSnapshotClassName =
    "com/cauth/android/steam/depot/DepotManifestListSnapshot";
constexpr const char* kDepotPreflightEntrySnapshotClassName =
    "com/cauth/android/steam/depot/DepotPreflightEntrySnapshot";
constexpr const char* kDepotPreflightSnapshotClassName =
    "com/cauth/android/steam/depot/DepotPreflightSnapshot";
constexpr const char* kDepotKeySnapshotClassName =
    "com/cauth/android/steam/depot/DepotKeySnapshot";
constexpr const char* kManifestRequestCodeSnapshotClassName =
    "com/cauth/android/steam/depot/ManifestRequestCodeSnapshot";
constexpr const char* kManifestInfoSnapshotClassName =
    "com/cauth/android/steam/depot/ManifestInfoSnapshot";
constexpr const char* kManifestFileEntrySnapshotClassName =
    "com/cauth/android/steam/depot/ManifestFileEntrySnapshot";
constexpr const char* kManifestFileListSnapshotClassName =
    "com/cauth/android/steam/depot/ManifestFileListSnapshot";
constexpr const char* kDepotLocalVerifySnapshotClassName =
    "com/cauth/android/steam/depot/DepotLocalVerifySnapshot";
constexpr const char* kDepotDownloadTaskSnapshotClassName =
    "com/cauth/android/steam/depot/DepotDownloadTaskSnapshot";

jclass g_app_branch_entry_snapshot_class = nullptr;
jclass g_app_branch_list_snapshot_class = nullptr;
jclass g_depot_manifest_entry_snapshot_class = nullptr;
jclass g_depot_manifest_list_snapshot_class = nullptr;
jclass g_depot_preflight_entry_snapshot_class = nullptr;
jclass g_depot_preflight_snapshot_class = nullptr;
jclass g_depot_key_snapshot_class = nullptr;
jclass g_manifest_request_code_snapshot_class = nullptr;
jclass g_manifest_info_snapshot_class = nullptr;
jclass g_manifest_file_entry_snapshot_class = nullptr;
jclass g_manifest_file_list_snapshot_class = nullptr;
jclass g_depot_local_verify_snapshot_class = nullptr;
jclass g_depot_download_task_snapshot_class = nullptr;

enum class DepotDownloadTaskKind : jint {
    Manifest = 1,
    Chunk = 2,
    File = 3,
    AllFiles = 4,
};

struct DepotDownloadTask {
    explicit DepotDownloadTask(DepotDownloadTaskKind task_kind) : kind(task_kind) {}

    std::mutex mutex;
    std::atomic_bool cancel_requested{false};
    std::atomic_bool finished{false};
    DepotDownloadTaskKind kind = DepotDownloadTaskKind::Manifest;
    bool succeeded = false;
    bool canceled = false;
    std::string phase = "Queued";
    std::string target;
    std::string message;
    std::uint64_t completed_steps = 0;
    std::uint64_t total_steps = 0;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
};

std::mutex g_download_tasks_mutex;
std::unordered_map<jlong, std::shared_ptr<DepotDownloadTask>> g_download_tasks;
std::atomic<jlong> g_next_download_task_handle{1};

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

void throw_result_exception_with_detail(JNIEnv* env,
                                        const char* prefix,
                                        cauth_result_t result,
                                        const char* extra_detail) {
    std::string message = prefix == nullptr ? "CAuth native error" : std::string{prefix};
    const char* detail = cauth_result_message(result);
    if (detail != nullptr && detail[0] != '\0') {
        message.append(": ");
        message.append(detail);
    }
    if (extra_detail != nullptr && extra_detail[0] != '\0') {
        message.append(" [");
        message.append(extra_detail);
        message.push_back(']');
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

jclass ensure_app_branch_entry_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_app_branch_entry_snapshot_class, kAppBranchEntrySnapshotClassName);
}

jclass ensure_app_branch_list_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_app_branch_list_snapshot_class, kAppBranchListSnapshotClassName);
}

jclass ensure_depot_manifest_entry_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_depot_manifest_entry_snapshot_class, kDepotManifestEntrySnapshotClassName);
}

jclass ensure_depot_manifest_list_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_depot_manifest_list_snapshot_class, kDepotManifestListSnapshotClassName);
}

jclass ensure_depot_preflight_entry_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_depot_preflight_entry_snapshot_class, kDepotPreflightEntrySnapshotClassName);
}

jclass ensure_depot_preflight_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_depot_preflight_snapshot_class, kDepotPreflightSnapshotClassName);
}

jclass ensure_depot_key_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_depot_key_snapshot_class, kDepotKeySnapshotClassName);
}

jclass ensure_manifest_request_code_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_manifest_request_code_snapshot_class, kManifestRequestCodeSnapshotClassName);
}

jclass ensure_manifest_info_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_manifest_info_snapshot_class, kManifestInfoSnapshotClassName);
}

jclass ensure_manifest_file_entry_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_manifest_file_entry_snapshot_class, kManifestFileEntrySnapshotClassName);
}

jclass ensure_manifest_file_list_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_manifest_file_list_snapshot_class, kManifestFileListSnapshotClassName);
}

jclass ensure_depot_local_verify_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_depot_local_verify_snapshot_class, kDepotLocalVerifySnapshotClassName);
}

jclass ensure_depot_download_task_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_depot_download_task_snapshot_class, kDepotDownloadTaskSnapshotClassName);
}

jobject make_app_branch_entry(JNIEnv* env, const cauth_app_branch_entry_t& entry) {
    jclass cls = ensure_app_branch_entry_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor =
        env->GetMethodID(cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IZ)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring name = env->NewStringUTF(entry.name == nullptr ? "" : entry.name);
    jstring build_id = env->NewStringUTF(entry.build_id == nullptr ? "" : entry.build_id);
    jstring description = env->NewStringUTF(entry.description == nullptr ? "" : entry.description);
    jobject instance = env->NewObject(
        cls, ctor, name, build_id, description, static_cast<jint>(entry.time_updated),
        static_cast<jboolean>(entry.password_required != 0));
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(build_id);
    env->DeleteLocalRef(description);
    return instance;
}

jobject make_app_branch_list(JNIEnv* env, const cauth_app_branch_list_t& result) {
    jclass entry_cls = ensure_app_branch_entry_snapshot_class(env);
    jclass cls = ensure_app_branch_list_snapshot_class(env);
    if (entry_cls == nullptr || cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor =
        env->GetMethodID(cls, "<init>", "(ZI[Lcom/cauth/android/steam/depot/AppBranchEntrySnapshot;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jobjectArray entries = env->NewObjectArray(
        static_cast<jsize>(result.branch_count), entry_cls, nullptr);
    for (jsize index = 0; index < static_cast<jsize>(result.branch_count); ++index) {
        jobject item = make_app_branch_entry(env, result.branches[index]);
        env->SetObjectArrayElement(entries, index, item);
        env->DeleteLocalRef(item);
    }
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0), static_cast<jint>(result.app_id),
        entries);
    env->DeleteLocalRef(entries);
    return instance;
}

jobject make_depot_manifest_entry(JNIEnv* env, const cauth_depot_manifest_entry_t& entry) {
    jclass cls = ensure_depot_manifest_entry_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(IJJJZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring platform_label =
        env->NewStringUTF(entry.platform_label == nullptr ? "" : entry.platform_label);
    jstring os_list = env->NewStringUTF(entry.os_list == nullptr ? "" : entry.os_list);
    jstring os_arch = env->NewStringUTF(entry.os_arch == nullptr ? "" : entry.os_arch);
    jstring depot_from_app =
        env->NewStringUTF(entry.depot_from_app == nullptr ? "" : entry.depot_from_app);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jint>(entry.depot_id), static_cast<jlong>(entry.manifest_gid),
        static_cast<jlong>(entry.size), static_cast<jlong>(entry.download_size),
        static_cast<jboolean>(entry.encrypted != 0), platform_label, os_list, os_arch,
        depot_from_app, static_cast<jboolean>(entry.shared_install != 0));
    env->DeleteLocalRef(platform_label);
    env->DeleteLocalRef(os_list);
    env->DeleteLocalRef(os_arch);
    env->DeleteLocalRef(depot_from_app);
    return instance;
}

jobject make_depot_manifest_list(JNIEnv* env, const cauth_depot_manifest_list_t& result) {
    jclass entry_cls = ensure_depot_manifest_entry_snapshot_class(env);
    jclass cls = ensure_depot_manifest_list_snapshot_class(env);
    if (entry_cls == nullptr || cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls, "<init>",
        "(ZILjava/lang/String;[Lcom/cauth/android/steam/depot/DepotManifestEntrySnapshot;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jobjectArray entries = env->NewObjectArray(
        static_cast<jsize>(result.manifest_count), entry_cls, nullptr);
    for (jsize index = 0; index < static_cast<jsize>(result.manifest_count); ++index) {
        jobject item = make_depot_manifest_entry(env, result.manifests[index]);
        env->SetObjectArrayElement(entries, index, item);
        env->DeleteLocalRef(item);
    }
    jstring branch = env->NewStringUTF(result.branch == nullptr ? "" : result.branch);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0), static_cast<jint>(result.app_id),
        branch, entries);
    env->DeleteLocalRef(branch);
    env->DeleteLocalRef(entries);
    return instance;
}

jobject make_depot_preflight_entry(JNIEnv* env, const cauth_depot_preflight_entry_t& entry) {
    jclass cls = ensure_depot_preflight_entry_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(IJJJZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZLjava/lang/String;IZ)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring platform_label =
        env->NewStringUTF(entry.platform_label == nullptr ? "" : entry.platform_label);
    jstring os_list = env->NewStringUTF(entry.os_list == nullptr ? "" : entry.os_list);
    jstring os_arch = env->NewStringUTF(entry.os_arch == nullptr ? "" : entry.os_arch);
    jstring depot_from_app =
        env->NewStringUTF(entry.depot_from_app == nullptr ? "" : entry.depot_from_app);
    jstring access_status =
        env->NewStringUTF(entry.access_status == nullptr ? "" : entry.access_status);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jint>(entry.depot_id), static_cast<jlong>(entry.manifest_gid),
        static_cast<jlong>(entry.size), static_cast<jlong>(entry.download_size),
        static_cast<jboolean>(entry.encrypted != 0), platform_label, os_list, os_arch,
        depot_from_app, static_cast<jboolean>(entry.shared_install != 0), access_status,
        static_cast<jint>(entry.key_eresult), static_cast<jboolean>(entry.key_available != 0));
    env->DeleteLocalRef(platform_label);
    env->DeleteLocalRef(os_list);
    env->DeleteLocalRef(os_arch);
    env->DeleteLocalRef(depot_from_app);
    env->DeleteLocalRef(access_status);
    return instance;
}

jobject make_depot_preflight(JNIEnv* env, const cauth_depot_preflight_report_t& result) {
    jclass entry_cls = ensure_depot_preflight_entry_snapshot_class(env);
    jclass cls = ensure_depot_preflight_snapshot_class(env);
    if (entry_cls == nullptr || cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls, "<init>",
        "(ZILjava/lang/String;Ljava/lang/String;[Lcom/cauth/android/steam/depot/DepotPreflightEntrySnapshot;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jobjectArray entries = env->NewObjectArray(
        static_cast<jsize>(result.depot_count), entry_cls, nullptr);
    for (jsize index = 0; index < static_cast<jsize>(result.depot_count); ++index) {
        jobject item = make_depot_preflight_entry(env, result.depots[index]);
        env->SetObjectArrayElement(entries, index, item);
        env->DeleteLocalRef(item);
    }
    jstring branch = env->NewStringUTF(result.branch == nullptr ? "" : result.branch);
    jstring build_id = env->NewStringUTF(result.build_id == nullptr ? "" : result.build_id);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0), static_cast<jint>(result.app_id),
        branch, build_id, entries);
    env->DeleteLocalRef(branch);
    env->DeleteLocalRef(build_id);
    env->DeleteLocalRef(entries);
    return instance;
}

jobject make_depot_key(JNIEnv* env, const cauth_depot_key_response_t& result) {
    jclass cls = ensure_depot_key_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZIILjava/lang/String;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring key_hex = env->NewStringUTF(result.key_hex == nullptr ? "" : result.key_hex);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0), static_cast<jint>(result.depot_id),
        static_cast<jint>(result.eresult), key_hex);
    env->DeleteLocalRef(key_hex);
    return instance;
}

jobject make_manifest_request_code(JNIEnv* env, const cauth_manifest_request_code_response_t& result) {
    jclass cls = ensure_manifest_request_code_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZJ)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    return env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0),
        static_cast<jlong>(result.manifest_request_code));
}

jobject make_manifest_info(JNIEnv* env, const cauth_manifest_info_t& result) {
    jclass cls = ensure_manifest_info_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZIJIZJJJJI)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    return env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0), static_cast<jint>(result.depot_id),
        static_cast<jlong>(result.manifest_gid), static_cast<jint>(result.creation_time),
        static_cast<jboolean>(result.filenames_encrypted != 0),
        static_cast<jlong>(result.file_count), static_cast<jlong>(result.chunk_count),
        static_cast<jlong>(result.total_uncompressed_size),
        static_cast<jlong>(result.total_compressed_size),
        static_cast<jint>(result.unique_chunks));
}

jobject make_manifest_file_entry(JNIEnv* env, const cauth_manifest_file_entry_t& entry) {
    jclass cls = ensure_manifest_file_entry_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;IJJ)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring filename = env->NewStringUTF(entry.filename == nullptr ? "" : entry.filename);
    jobject instance = env->NewObject(
        cls, ctor, filename, static_cast<jint>(entry.flags), static_cast<jlong>(entry.size),
        static_cast<jlong>(entry.chunk_count));
    env->DeleteLocalRef(filename);
    return instance;
}

jobject make_manifest_file_list(JNIEnv* env, const cauth_manifest_file_list_t& result) {
    jclass entry_cls = ensure_manifest_file_entry_snapshot_class(env);
    jclass cls = ensure_manifest_file_list_snapshot_class(env);
    if (entry_cls == nullptr || cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls, "<init>",
        "(ZJJJ[Lcom/cauth/android/steam/depot/ManifestFileEntrySnapshot;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jobjectArray entries = env->NewObjectArray(
        static_cast<jsize>(result.printed_count), entry_cls, nullptr);
    for (jsize index = 0; index < static_cast<jsize>(result.printed_count); ++index) {
        jobject item = make_manifest_file_entry(env, result.files[index]);
        env->SetObjectArrayElement(entries, index, item);
        env->DeleteLocalRef(item);
    }
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0),
        static_cast<jlong>(result.matched_count), static_cast<jlong>(result.printed_count),
        static_cast<jlong>(result.total_count), entries);
    env->DeleteLocalRef(entries);
    return instance;
}

jobject make_depot_local_verify_snapshot(JNIEnv* env,
                                         const cauth_depot_local_verify_report_t& result) {
    jclass cls = ensure_depot_local_verify_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZZJJJJJJJ)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    return env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0),
        static_cast<jboolean>(result.clean != 0), static_cast<jlong>(result.checked_count),
        static_cast<jlong>(result.ok_count), static_cast<jlong>(result.missing_count),
        static_cast<jlong>(result.mismatched_count), static_cast<jlong>(result.size_only_count),
        static_cast<jlong>(result.filtered_out_count), static_cast<jlong>(result.total_count));
}

std::shared_ptr<DepotDownloadTask> find_download_task(jlong handle) {
    std::lock_guard lock(g_download_tasks_mutex);
    const auto it = g_download_tasks.find(handle);
    return it == g_download_tasks.end() ? nullptr : it->second;
}

void update_download_task_progress(const cauth::steam::depot::DepotDownloadProgress& progress,
                                   void* user_data) {
    auto* task = static_cast<DepotDownloadTask*>(user_data);
    if (task == nullptr) {
        return;
    }
    std::lock_guard lock(task->mutex);
    switch (progress.kind) {
    case cauth::steam::depot::DepotDownloadKind::Manifest:
        task->kind = DepotDownloadTaskKind::Manifest;
        break;
    case cauth::steam::depot::DepotDownloadKind::Chunk:
        task->kind = DepotDownloadTaskKind::Chunk;
        break;
    case cauth::steam::depot::DepotDownloadKind::File:
        task->kind = DepotDownloadTaskKind::File;
        break;
    case cauth::steam::depot::DepotDownloadKind::AllFiles:
        task->kind = DepotDownloadTaskKind::AllFiles;
        break;
    }
    task->phase = progress.phase;
    task->target = progress.target;
    task->completed_steps = progress.completed_steps;
    task->total_steps = progress.total_steps;
    task->completed_bytes = progress.completed_bytes;
    task->total_bytes = progress.total_bytes;
}

bool is_download_task_canceled(void* user_data) {
    auto* task = static_cast<DepotDownloadTask*>(user_data);
    return task != nullptr && task->cancel_requested.load();
}

jobject make_depot_download_task_snapshot(JNIEnv* env,
                                          jlong handle,
                                          const DepotDownloadTask& task) {
    jclass cls = ensure_depot_download_task_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(JIZZZZLjava/lang/String;JJJJLjava/lang/String;Ljava/lang/String;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring phase = env->NewStringUTF(task.phase.c_str());
    jstring target = env->NewStringUTF(task.target.c_str());
    jstring message = env->NewStringUTF(task.message.c_str());
    jobject instance = env->NewObject(
        cls,
        ctor,
        handle,
        static_cast<jint>(task.kind),
        static_cast<jboolean>(!task.finished.load()),
        static_cast<jboolean>(task.finished.load()),
        static_cast<jboolean>(task.canceled),
        static_cast<jboolean>(task.succeeded),
        phase,
        static_cast<jlong>(task.completed_steps),
        static_cast<jlong>(task.total_steps),
        static_cast<jlong>(task.completed_bytes),
        static_cast<jlong>(task.total_bytes),
        target,
        message);
    env->DeleteLocalRef(phase);
    env->DeleteLocalRef(target);
    env->DeleteLocalRef(message);
    return instance;
}

template <typename Runner>
jlong start_download_task(DepotDownloadTaskKind kind, Runner&& runner) {
    const auto handle = g_next_download_task_handle.fetch_add(1);
    auto task = std::make_shared<DepotDownloadTask>(kind);
    {
        std::lock_guard lock(g_download_tasks_mutex);
        g_download_tasks.emplace(handle, task);
    }

    std::thread([task, runner = std::forward<Runner>(runner)]() mutable {
        std::string message;
        bool succeeded = false;
        bool canceled = false;
        try {
            cauth::steam::depot::set_current_thread_depot_download_hooks(
                update_download_task_progress,
                is_download_task_canceled,
                task.get());
            runner(message, succeeded, canceled);
            cauth::steam::depot::clear_current_thread_depot_download_hooks();
        } catch (const std::exception& exception) {
            cauth::steam::depot::clear_current_thread_depot_download_hooks();
            message = exception.what();
            succeeded = false;
        } catch (...) {
            cauth::steam::depot::clear_current_thread_depot_download_hooks();
            message = "unexpected exception";
            succeeded = false;
        }

        std::lock_guard lock(task->mutex);
        task->succeeded = succeeded;
        task->canceled = canceled;
        task->message = message;
        if (task->phase.empty()) {
            task->phase = canceled ? "Canceled" : (succeeded ? "Complete" : "Failed");
        }
        task->finished = true;
    }).detach();

    return handle;
}

} // namespace

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeFetchDepotBranches(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jint max_count) {
    cauth_app_branch_list_t result{};
    const cauth_result_t native_result = cauth_depot_fetch_branches(
        client_from_handle(handle), static_cast<unsigned int>(app_id),
        static_cast<unsigned int>(max_count), &result);
    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Depot branches failed", native_result);
        return nullptr;
    }
    return make_app_branch_list(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeFetchDepotManifests(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jstring branch,
    jint max_count) {
    const char* branch_chars = branch == nullptr ? nullptr : env->GetStringUTFChars(branch, nullptr);
    cauth_depot_manifest_list_t result{};
    const cauth_result_t native_result = cauth_depot_fetch_manifests(
        client_from_handle(handle), static_cast<unsigned int>(app_id), branch_chars,
        static_cast<unsigned int>(max_count), &result);
    if (branch_chars != nullptr) {
        env->ReleaseStringUTFChars(branch, branch_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Depot manifests failed", native_result);
        return nullptr;
    }
    return make_depot_manifest_list(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeFetchDepotPreflight(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jstring branch,
    jint max_count) {
    const char* branch_chars = branch == nullptr ? nullptr : env->GetStringUTFChars(branch, nullptr);
    cauth_depot_preflight_report_t result{};
    const cauth_result_t native_result = cauth_depot_fetch_preflight(
        client_from_handle(handle), static_cast<unsigned int>(app_id), branch_chars,
        static_cast<unsigned int>(max_count), &result);
    if (branch_chars != nullptr) {
        env->ReleaseStringUTFChars(branch, branch_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Depot preflight failed", native_result);
        return nullptr;
    }
    return make_depot_preflight(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeFetchDepotKey(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jint depot_id,
    jint max_count) {
    cauth_depot_key_response_t result{};
    const cauth_result_t native_result = cauth_depot_fetch_key(
        client_from_handle(handle), static_cast<unsigned int>(app_id),
        static_cast<unsigned int>(depot_id), static_cast<unsigned int>(max_count), &result);
    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Depot key failed", native_result);
        return nullptr;
    }
    return make_depot_key(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeFetchManifestRequestCode(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jint depot_id,
    jlong manifest_gid,
    jstring branch,
    jstring branch_password_hash,
    jint max_count) {
    const char* branch_chars = branch == nullptr ? nullptr : env->GetStringUTFChars(branch, nullptr);
    const char* branch_password_hash_chars = branch_password_hash == nullptr
                                                 ? nullptr
                                                 : env->GetStringUTFChars(branch_password_hash, nullptr);
    cauth_manifest_request_code_response_t result{};
    const cauth_result_t native_result = cauth_depot_fetch_manifest_request_code(
        client_from_handle(handle), static_cast<unsigned int>(app_id),
        static_cast<unsigned int>(depot_id), static_cast<unsigned long long>(manifest_gid),
        branch_chars, branch_password_hash_chars, static_cast<unsigned int>(max_count), &result);
    if (branch_chars != nullptr) {
        env->ReleaseStringUTFChars(branch, branch_chars);
    }
    if (branch_password_hash_chars != nullptr) {
        env->ReleaseStringUTFChars(branch_password_hash, branch_password_hash_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Manifest request code failed", native_result);
        return nullptr;
    }
    return make_manifest_request_code(env, result);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeDownloadDepotManifest(
    JNIEnv* env,
    jclass,
    jint depot_id,
    jlong manifest_gid,
    jlong request_code,
    jint max_count,
    jstring output_path) {
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const cauth_result_t native_result = cauth_depot_download_manifest(
        static_cast<unsigned int>(depot_id), static_cast<unsigned long long>(manifest_gid),
        static_cast<unsigned long long>(request_code), static_cast<unsigned int>(max_count),
        output_path_chars);
    if (output_path_chars != nullptr) {
        env->ReleaseStringUTFChars(output_path, output_path_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception_with_detail(
            env,
            "Depot manifest download failed",
            native_result,
            cauth_depot_last_error_detail());
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeLoadManifestInfo(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring depot_key_hex) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    cauth_manifest_info_t result{};
    const cauth_result_t native_result =
        cauth_depot_load_manifest_info(input_path_chars, depot_key_hex_chars, &result);
    if (input_path_chars != nullptr) {
        env->ReleaseStringUTFChars(input_path, input_path_chars);
    }
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception_with_detail(
            env,
            "Manifest info load failed",
            native_result,
            cauth_depot_last_error_detail());
        return nullptr;
    }
    return make_manifest_info(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeListManifestFiles(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring depot_key_hex,
    jstring filter_text,
    jint limit) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* filter_text_chars =
        filter_text == nullptr ? nullptr : env->GetStringUTFChars(filter_text, nullptr);
    cauth_manifest_file_list_t result{};
    const cauth_result_t native_result = cauth_depot_list_manifest_files(
        input_path_chars, depot_key_hex_chars, filter_text_chars, static_cast<unsigned int>(limit),
        &result);
    if (input_path_chars != nullptr) {
        env->ReleaseStringUTFChars(input_path, input_path_chars);
    }
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (filter_text_chars != nullptr) {
        env->ReleaseStringUTFChars(filter_text, filter_text_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception_with_detail(
            env,
            "Manifest file list failed",
            native_result,
            cauth_depot_last_error_detail());
        return nullptr;
    }
    return make_manifest_file_list(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeVerifyLocalFiles(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring depot_key_hex,
    jstring local_root,
    jstring filter_text) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* local_root_chars =
        local_root == nullptr ? nullptr : env->GetStringUTFChars(local_root, nullptr);
    const char* filter_text_chars =
        filter_text == nullptr ? nullptr : env->GetStringUTFChars(filter_text, nullptr);
    cauth_depot_local_verify_report_t result{};
    const cauth_result_t native_result = cauth_depot_verify_local_files(
        input_path_chars,
        depot_key_hex_chars,
        local_root_chars,
        filter_text_chars,
        &result);
    if (input_path_chars != nullptr) {
        env->ReleaseStringUTFChars(input_path, input_path_chars);
    }
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (local_root_chars != nullptr) {
        env->ReleaseStringUTFChars(local_root, local_root_chars);
    }
    if (filter_text_chars != nullptr) {
        env->ReleaseStringUTFChars(filter_text, filter_text_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception_with_detail(
            env,
            "Local verify failed",
            native_result,
            cauth_depot_last_error_detail());
        return nullptr;
    }
    return make_depot_local_verify_snapshot(env, result);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeDownloadDepotChunk(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring depot_key_hex,
    jstring file_path,
    jlong file_index,
    jboolean has_file_index,
    jlong chunk_index,
    jboolean process_chunk,
    jint max_count,
    jstring output_path) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* file_path_chars =
        file_path == nullptr ? nullptr : env->GetStringUTFChars(file_path, nullptr);
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const cauth_result_t native_result = cauth_depot_download_chunk(
        input_path_chars, depot_key_hex_chars, file_path_chars,
        static_cast<unsigned long long>(file_index), has_file_index ? 1 : 0,
        static_cast<unsigned long long>(chunk_index), process_chunk ? 1 : 0,
        static_cast<unsigned int>(max_count), output_path_chars);
    if (input_path_chars != nullptr) {
        env->ReleaseStringUTFChars(input_path, input_path_chars);
    }
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (file_path_chars != nullptr) {
        env->ReleaseStringUTFChars(file_path, file_path_chars);
    }
    if (output_path_chars != nullptr) {
        env->ReleaseStringUTFChars(output_path, output_path_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception_with_detail(
            env,
            "Depot chunk download failed",
            native_result,
            cauth_depot_last_error_detail());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeDownloadDepotFile(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring depot_key_hex,
    jstring file_path,
    jlong file_index,
    jboolean has_file_index,
    jint max_count,
    jstring output_path) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* file_path_chars =
        file_path == nullptr ? nullptr : env->GetStringUTFChars(file_path, nullptr);
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const cauth_result_t native_result = cauth_depot_download_file(
        input_path_chars, depot_key_hex_chars, file_path_chars,
        static_cast<unsigned long long>(file_index), has_file_index ? 1 : 0,
        static_cast<unsigned int>(max_count), output_path_chars);
    if (input_path_chars != nullptr) {
        env->ReleaseStringUTFChars(input_path, input_path_chars);
    }
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (file_path_chars != nullptr) {
        env->ReleaseStringUTFChars(file_path, file_path_chars);
    }
    if (output_path_chars != nullptr) {
        env->ReleaseStringUTFChars(output_path, output_path_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception_with_detail(
            env,
            "Depot file download failed",
            native_result,
            cauth_depot_last_error_detail());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeDownloadDepotAllFiles(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring depot_key_hex,
    jint max_count,
    jstring output_root) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* output_root_chars =
        output_root == nullptr ? nullptr : env->GetStringUTFChars(output_root, nullptr);
    const cauth_result_t native_result = cauth_depot_download_all_files(
        input_path_chars,
        depot_key_hex_chars,
        static_cast<unsigned int>(max_count),
        output_root_chars);
    if (input_path_chars != nullptr) {
        env->ReleaseStringUTFChars(input_path, input_path_chars);
    }
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (output_root_chars != nullptr) {
        env->ReleaseStringUTFChars(output_root, output_root_chars);
    }
    if (native_result != CAUTH_OK) {
        throw_result_exception_with_detail(
            env,
            "Depot all-files download failed",
            native_result,
            cauth_depot_last_error_detail());
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeStartDepotManifestDownload(
    JNIEnv* env,
    jclass,
    jint depot_id,
    jlong manifest_gid,
    jlong request_code,
    jint max_count,
    jstring output_path) {
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const std::string output_path_text = output_path_chars == nullptr ? "" : output_path_chars;
    if (output_path_chars != nullptr) {
        env->ReleaseStringUTFChars(output_path, output_path_chars);
    }

    return start_download_task(
        DepotDownloadTaskKind::Manifest,
        [depot_id,
         manifest_gid,
         request_code,
         max_count,
         output_path_text](std::string& message, bool& succeeded, bool& canceled) {
            const auto native_result = cauth_depot_download_manifest(
                static_cast<unsigned int>(depot_id),
                static_cast<unsigned long long>(manifest_gid),
                static_cast<unsigned long long>(request_code),
                static_cast<unsigned int>(max_count),
                output_path_text.c_str());
            const char* detail = cauth_depot_last_error_detail();
            message = detail == nullptr ? "" : detail;
            if (message.empty() && native_result != CAUTH_OK) {
                const char* result_message = cauth_result_message(native_result);
                message = result_message == nullptr ? "manifest download failed" : result_message;
            }
            canceled = native_result != CAUTH_OK && message.find("operation canceled") != std::string::npos;
            succeeded = native_result == CAUTH_OK;
        });
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeStartDepotChunkDownload(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring depot_key_hex,
    jstring file_path,
    jlong file_index,
    jboolean has_file_index,
    jlong chunk_index,
    jboolean process_chunk,
    jint max_count,
    jstring output_path) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* file_path_chars =
        file_path == nullptr ? nullptr : env->GetStringUTFChars(file_path, nullptr);
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const std::string input_path_text = input_path_chars == nullptr ? "" : input_path_chars;
    const std::string depot_key_hex_text =
        depot_key_hex_chars == nullptr ? "" : depot_key_hex_chars;
    const std::string file_path_text = file_path_chars == nullptr ? "" : file_path_chars;
    const std::string output_path_text = output_path_chars == nullptr ? "" : output_path_chars;
    if (input_path_chars != nullptr) env->ReleaseStringUTFChars(input_path, input_path_chars);
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (file_path_chars != nullptr) env->ReleaseStringUTFChars(file_path, file_path_chars);
    if (output_path_chars != nullptr) env->ReleaseStringUTFChars(output_path, output_path_chars);

    return start_download_task(
        DepotDownloadTaskKind::Chunk,
        [input_path_text,
         depot_key_hex_text,
         file_path_text,
         file_index,
         has_file_index,
         chunk_index,
         process_chunk,
         max_count,
         output_path_text](std::string& message, bool& succeeded, bool& canceled) {
            const auto native_result = cauth_depot_download_chunk(
                input_path_text.c_str(),
                depot_key_hex_text.empty() ? nullptr : depot_key_hex_text.c_str(),
                file_path_text.empty() ? nullptr : file_path_text.c_str(),
                static_cast<unsigned long long>(file_index),
                has_file_index ? 1 : 0,
                static_cast<unsigned long long>(chunk_index),
                process_chunk ? 1 : 0,
                static_cast<unsigned int>(max_count),
                output_path_text.c_str());
            const char* detail = cauth_depot_last_error_detail();
            message = detail == nullptr ? "" : detail;
            if (message.empty() && native_result != CAUTH_OK) {
                const char* result_message = cauth_result_message(native_result);
                message = result_message == nullptr ? "chunk download failed" : result_message;
            }
            canceled = native_result != CAUTH_OK && message.find("operation canceled") != std::string::npos;
            succeeded = native_result == CAUTH_OK;
        });
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeStartDepotFileDownload(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring depot_key_hex,
    jstring file_path,
    jlong file_index,
    jboolean has_file_index,
    jint max_count,
    jstring output_path) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* file_path_chars =
        file_path == nullptr ? nullptr : env->GetStringUTFChars(file_path, nullptr);
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const std::string input_path_text = input_path_chars == nullptr ? "" : input_path_chars;
    const std::string depot_key_hex_text =
        depot_key_hex_chars == nullptr ? "" : depot_key_hex_chars;
    const std::string file_path_text = file_path_chars == nullptr ? "" : file_path_chars;
    const std::string output_path_text = output_path_chars == nullptr ? "" : output_path_chars;
    if (input_path_chars != nullptr) env->ReleaseStringUTFChars(input_path, input_path_chars);
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (file_path_chars != nullptr) env->ReleaseStringUTFChars(file_path, file_path_chars);
    if (output_path_chars != nullptr) env->ReleaseStringUTFChars(output_path, output_path_chars);

    return start_download_task(
        DepotDownloadTaskKind::File,
        [input_path_text,
         depot_key_hex_text,
         file_path_text,
         file_index,
         has_file_index,
         max_count,
         output_path_text](std::string& message, bool& succeeded, bool& canceled) {
            const auto native_result = cauth_depot_download_file(
                input_path_text.c_str(),
                depot_key_hex_text.c_str(),
                file_path_text.empty() ? nullptr : file_path_text.c_str(),
                static_cast<unsigned long long>(file_index),
                has_file_index ? 1 : 0,
                static_cast<unsigned int>(max_count),
                output_path_text.c_str());
            const char* detail = cauth_depot_last_error_detail();
            message = detail == nullptr ? "" : detail;
            if (message.empty() && native_result != CAUTH_OK) {
                const char* result_message = cauth_result_message(native_result);
                message = result_message == nullptr ? "file download failed" : result_message;
            }
            canceled = native_result != CAUTH_OK && message.find("operation canceled") != std::string::npos;
            succeeded = native_result == CAUTH_OK;
        });
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeStartDepotAllFilesDownload(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring depot_key_hex,
    jint max_count,
    jstring output_root) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* output_root_chars =
        output_root == nullptr ? nullptr : env->GetStringUTFChars(output_root, nullptr);
    const std::string input_path_text = input_path_chars == nullptr ? "" : input_path_chars;
    const std::string depot_key_hex_text =
        depot_key_hex_chars == nullptr ? "" : depot_key_hex_chars;
    const std::string output_root_text = output_root_chars == nullptr ? "" : output_root_chars;
    if (input_path_chars != nullptr) env->ReleaseStringUTFChars(input_path, input_path_chars);
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (output_root_chars != nullptr) {
        env->ReleaseStringUTFChars(output_root, output_root_chars);
    }

    return start_download_task(
        DepotDownloadTaskKind::AllFiles,
        [input_path_text,
         depot_key_hex_text,
         max_count,
         output_root_text](std::string& message, bool& succeeded, bool& canceled) {
            const auto native_result = cauth_depot_download_all_files(
                input_path_text.c_str(),
                depot_key_hex_text.c_str(),
                static_cast<unsigned int>(max_count),
                output_root_text.c_str());
            const char* detail = cauth_depot_last_error_detail();
            message = detail == nullptr ? "" : detail;
            if (message.empty() && native_result != CAUTH_OK) {
                const char* result_message = cauth_result_message(native_result);
                message = result_message == nullptr ? "all-files download failed" : result_message;
            }
            canceled = native_result != CAUTH_OK && message.find("operation canceled") != std::string::npos;
            succeeded = native_result == CAUTH_OK;
        });
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativePollDepotDownloadTask(
    JNIEnv* env,
    jclass,
    jlong handle) {
    const auto task = find_download_task(handle);
    if (task == nullptr) {
        env->ThrowNew(env->FindClass(kIllegalStateException), "Depot download task not found");
        return nullptr;
    }
    std::lock_guard lock(task->mutex);
    return make_depot_download_task_snapshot(env, handle, *task);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeCancelDepotDownloadTask(
    JNIEnv*,
    jclass,
    jlong handle) {
    const auto task = find_download_task(handle);
    if (task == nullptr) {
        return;
    }
    task->cancel_requested = true;
    std::lock_guard lock(task->mutex);
    if (task->phase.empty() || task->phase == "Queued") {
        task->phase = "Cancel requested";
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeDisposeDepotDownloadTask(
    JNIEnv*,
    jclass,
    jlong handle) {
    std::lock_guard lock(g_download_tasks_mutex);
    g_download_tasks.erase(handle);
}
