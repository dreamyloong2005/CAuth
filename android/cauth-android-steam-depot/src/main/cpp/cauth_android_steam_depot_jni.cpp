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
#include <vector>

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
constexpr const char* kDepotLocalVerifyEntrySnapshotClassName =
    "com/cauth/android/steam/depot/DepotLocalVerifyEntrySnapshot";
constexpr const char* kDepotLocalVerifySnapshotClassName =
    "com/cauth/android/steam/depot/DepotLocalVerifySnapshot";
constexpr const char* kDepotDownloadTaskSnapshotClassName =
    "com/cauth/android/steam/depot/DepotDownloadTaskSnapshot";
constexpr const char* kRouteProbeEntrySnapshotClassName =
    "com/cauth/android/CAuthRouteProbeEntrySnapshot";
constexpr const char* kRouteProbeSnapshotClassName =
    "com/cauth/android/CAuthRouteProbeSnapshot";

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
jclass g_depot_local_verify_entry_snapshot_class = nullptr;
jclass g_depot_local_verify_snapshot_class = nullptr;
jclass g_depot_download_task_snapshot_class = nullptr;
jclass g_route_probe_entry_snapshot_class = nullptr;
jclass g_route_probe_snapshot_class = nullptr;

struct DepotVerifyStorage {
    std::vector<std::string> manifest_filenames;
    std::vector<std::string> local_paths;
    std::vector<std::string> expected_sha_hex;
    std::vector<std::string> actual_sha_hex;
    std::vector<std::string> reasons;
    std::vector<cauth_depot_local_verify_entry_t> entries;
};

struct RouteSelectionStorage {
    std::string endpoint;
    std::string protocol;
    std::string role;
    cauth_route_selection_t selection{};

    const cauth_route_selection_t* pointer_or_null() const {
        return empty() ? nullptr : &selection;
    }

    bool empty() const {
        return endpoint.empty() && protocol.empty() && role.empty();
    }
};

enum class DepotDownloadTaskKind : jint {
    Manifest = 1,
    Chunk = 2,
    File = 3,
    AllFiles = 4,
    Verify = 5,
};

struct DepotDownloadTask {
    explicit DepotDownloadTask(DepotDownloadTaskKind task_kind) : kind(task_kind) {}

    std::mutex mutex;
    std::atomic_bool cancel_requested{false};
    std::atomic_bool pause_requested{false};
    std::atomic_bool finished{false};
    DepotDownloadTaskKind kind = DepotDownloadTaskKind::Manifest;
    bool succeeded = false;
    bool canceled = false;
    bool paused = false;
    bool has_verify_report = false;
    std::string module_status = "idle";
    std::string phase = "Queued";
    std::string target;
    std::string message;
    std::uint64_t completed_steps = 0;
    std::uint64_t total_steps = 0;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
    std::string verify_report_module_status;
    cauth_depot_local_verify_report_t verify_report{};
    DepotVerifyStorage verify_storage;
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

std::string copy_jstring(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return {};
    }
    std::string result{chars};
    env->ReleaseStringUTFChars(value, chars);
    return result;
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

jclass ensure_depot_local_verify_entry_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_depot_local_verify_entry_snapshot_class, kDepotLocalVerifyEntrySnapshotClassName);
}

jclass ensure_depot_local_verify_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_depot_local_verify_snapshot_class, kDepotLocalVerifySnapshotClassName);
}

jclass ensure_depot_download_task_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_depot_download_task_snapshot_class, kDepotDownloadTaskSnapshotClassName);
}

jclass ensure_route_probe_entry_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_route_probe_entry_snapshot_class, kRouteProbeEntrySnapshotClassName);
}

jclass ensure_route_probe_snapshot_class(JNIEnv* env) {
    return require_global_class(env, g_route_probe_snapshot_class, kRouteProbeSnapshotClassName);
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

void fill_depot_verify_storage(const cauth_depot_local_verify_report_t& source,
                               DepotVerifyStorage& storage,
                               cauth_depot_local_verify_report_t& destination) {
    storage.manifest_filenames.clear();
    storage.local_paths.clear();
    storage.expected_sha_hex.clear();
    storage.actual_sha_hex.clear();
    storage.reasons.clear();
    storage.entries.clear();
    storage.manifest_filenames.reserve(static_cast<std::size_t>(source.entry_count));
    storage.local_paths.reserve(static_cast<std::size_t>(source.entry_count));
    storage.expected_sha_hex.reserve(static_cast<std::size_t>(source.entry_count));
    storage.actual_sha_hex.reserve(static_cast<std::size_t>(source.entry_count));
    storage.reasons.reserve(static_cast<std::size_t>(source.entry_count));
    for (std::size_t index = 0; index < static_cast<std::size_t>(source.entry_count); ++index) {
        const auto& entry = source.entries[index];
        storage.manifest_filenames.push_back(entry.manifest_filename == nullptr ? "" : entry.manifest_filename);
        storage.local_paths.push_back(entry.local_path == nullptr ? "" : entry.local_path);
        storage.expected_sha_hex.push_back(entry.expected_sha_hex == nullptr ? "" : entry.expected_sha_hex);
        storage.actual_sha_hex.push_back(entry.actual_sha_hex == nullptr ? "" : entry.actual_sha_hex);
        storage.reasons.push_back(entry.reason == nullptr ? "" : entry.reason);
    }
    storage.entries.reserve(static_cast<std::size_t>(source.entry_count));
    for (std::size_t index = 0; index < static_cast<std::size_t>(source.entry_count); ++index) {
        const auto& entry = source.entries[index];
        storage.entries.push_back(cauth_depot_local_verify_entry_t{
            storage.manifest_filenames[index].c_str(),
            storage.local_paths[index].c_str(),
            entry.status,
            entry.expected_size,
            entry.actual_size,
            storage.expected_sha_hex[index].c_str(),
            storage.actual_sha_hex[index].c_str(),
            storage.reasons[index].c_str(),
        });
    }
    destination = source;
    destination.entries = storage.entries.empty() ? nullptr : storage.entries.data();
    destination.entry_count = static_cast<unsigned long long>(storage.entries.size());
}

jobject make_depot_local_verify_entry_snapshot(JNIEnv* env,
                                               const cauth_depot_local_verify_entry_t& entry) {
    jclass cls = ensure_depot_local_verify_entry_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(Ljava/lang/String;Ljava/lang/String;IJJLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring manifest_filename =
        env->NewStringUTF(entry.manifest_filename == nullptr ? "" : entry.manifest_filename);
    jstring local_path = env->NewStringUTF(entry.local_path == nullptr ? "" : entry.local_path);
    jstring expected_sha =
        env->NewStringUTF(entry.expected_sha_hex == nullptr ? "" : entry.expected_sha_hex);
    jstring actual_sha =
        env->NewStringUTF(entry.actual_sha_hex == nullptr ? "" : entry.actual_sha_hex);
    jstring reason = env->NewStringUTF(entry.reason == nullptr ? "" : entry.reason);
    jobject instance = env->NewObject(
        cls,
        ctor,
        manifest_filename,
        local_path,
        static_cast<jint>(entry.status),
        static_cast<jlong>(entry.expected_size),
        static_cast<jlong>(entry.actual_size),
        expected_sha,
        actual_sha,
        reason);
    env->DeleteLocalRef(manifest_filename);
    env->DeleteLocalRef(local_path);
    env->DeleteLocalRef(expected_sha);
    env->DeleteLocalRef(actual_sha);
    env->DeleteLocalRef(reason);
    return instance;
}

jobject make_depot_local_verify_snapshot(JNIEnv* env,
                                         const cauth_depot_local_verify_report_t& result) {
    jclass entry_cls = ensure_depot_local_verify_entry_snapshot_class(env);
    jclass cls = ensure_depot_local_verify_snapshot_class(env);
    if (entry_cls == nullptr || cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(ZZLjava/lang/String;JJJJJJJ[Lcom/cauth/android/steam/depot/DepotLocalVerifyEntrySnapshot;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jobjectArray entries = env->NewObjectArray(
        static_cast<jsize>(result.entry_count),
        entry_cls,
        nullptr);
    for (jsize index = 0; index < static_cast<jsize>(result.entry_count); ++index) {
        jobject item = make_depot_local_verify_entry_snapshot(env, result.entries[index]);
        env->SetObjectArrayElement(entries, index, item);
        env->DeleteLocalRef(item);
    }
    jstring module_status =
        env->NewStringUTF(result.module_status == nullptr ? "idle" : result.module_status);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0),
        static_cast<jboolean>(result.clean != 0), module_status, static_cast<jlong>(result.checked_count),
        static_cast<jlong>(result.ok_count), static_cast<jlong>(result.missing_count),
        static_cast<jlong>(result.mismatched_count), static_cast<jlong>(result.size_only_count),
        static_cast<jlong>(result.filtered_out_count), static_cast<jlong>(result.total_count), entries);
    env->DeleteLocalRef(module_status);
    env->DeleteLocalRef(entries);
    return instance;
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
    task->module_status = progress.module_status.empty() ? "idle" : progress.module_status;
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
    case cauth::steam::depot::DepotDownloadKind::VerifyLocal:
        task->kind = DepotDownloadTaskKind::Verify;
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

bool is_download_task_paused(void* user_data) {
    auto* task = static_cast<DepotDownloadTask*>(user_data);
    return task != nullptr && task->pause_requested.load();
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
        "(JIZZZZZLjava/lang/String;Ljava/lang/String;JJJJLjava/lang/String;Ljava/lang/String;Lcom/cauth/android/steam/depot/DepotLocalVerifySnapshot;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring module_status = env->NewStringUTF(task.module_status.c_str());
    jstring phase = env->NewStringUTF(task.phase.c_str());
    jstring target = env->NewStringUTF(task.target.c_str());
    jstring message = env->NewStringUTF(task.message.c_str());
    jobject verify_result =
        task.has_verify_report ? make_depot_local_verify_snapshot(env, task.verify_report) : nullptr;
    jobject instance = env->NewObject(
        cls,
        ctor,
        handle,
        static_cast<jint>(task.kind),
        static_cast<jboolean>(!task.finished.load()),
        static_cast<jboolean>(task.finished.load()),
        static_cast<jboolean>(task.canceled),
        static_cast<jboolean>(task.paused),
        static_cast<jboolean>(task.succeeded),
        module_status,
        phase,
        static_cast<jlong>(task.completed_steps),
        static_cast<jlong>(task.total_steps),
        static_cast<jlong>(task.completed_bytes),
        static_cast<jlong>(task.total_bytes),
        target,
        message,
        verify_result);
    env->DeleteLocalRef(module_status);
    env->DeleteLocalRef(phase);
    env->DeleteLocalRef(target);
    env->DeleteLocalRef(message);
    if (verify_result != nullptr) {
        env->DeleteLocalRef(verify_result);
    }
    return instance;
}

RouteSelectionStorage make_route_selection(JNIEnv* env,
                                           jstring route_endpoint,
                                           jstring route_protocol,
                                           jstring route_role) {
    RouteSelectionStorage storage;
    storage.endpoint = copy_jstring(env, route_endpoint);
    storage.protocol = copy_jstring(env, route_protocol);
    storage.role = copy_jstring(env, route_role);
    storage.selection.endpoint = storage.endpoint.empty() ? nullptr : storage.endpoint.c_str();
    storage.selection.protocol = storage.protocol.empty() ? nullptr : storage.protocol.c_str();
    storage.selection.role = storage.role.empty() ? nullptr : storage.role.c_str();
    return storage;
}

jobject make_route_probe_entry(JNIEnv* env, const cauth_route_probe_entry_t& entry) {
    jclass cls = ensure_route_probe_entry_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JZZII)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring endpoint = env->NewStringUTF(entry.endpoint == nullptr ? "" : entry.endpoint);
    jstring protocol = env->NewStringUTF(entry.protocol == nullptr ? "" : entry.protocol);
    jstring role = env->NewStringUTF(entry.role == nullptr ? "" : entry.role);
    jstring note = env->NewStringUTF(entry.note == nullptr ? "" : entry.note);
    jobject instance = env->NewObject(
        cls,
        ctor,
        endpoint,
        protocol,
        role,
        note,
        static_cast<jlong>(entry.latency_ms),
        static_cast<jboolean>(entry.latency_known != 0),
        static_cast<jboolean>(entry.recent_success != 0),
        static_cast<jboolean>(entry.recent_failure != 0),
        static_cast<jint>(entry.success_count),
        static_cast<jint>(entry.failure_count));
    env->DeleteLocalRef(endpoint);
    env->DeleteLocalRef(protocol);
    env->DeleteLocalRef(role);
    env->DeleteLocalRef(note);
    return instance;
}

jobject make_route_probe_snapshot(JNIEnv* env, const cauth_route_probe_result_t& result) {
    jclass entry_cls = ensure_route_probe_entry_snapshot_class(env);
    jclass cls = ensure_route_probe_snapshot_class(env);
    if (entry_cls == nullptr || cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(ZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;[Lcom/cauth/android/CAuthRouteProbeEntrySnapshot;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jobjectArray entries =
        env->NewObjectArray(static_cast<jsize>(result.route_count), entry_cls, nullptr);
    if (entries == nullptr) {
        return nullptr;
    }
    for (jsize index = 0; index < static_cast<jsize>(result.route_count); ++index) {
        jobject item = make_route_probe_entry(env, result.routes[index]);
        if (item == nullptr) {
            return nullptr;
        }
        env->SetObjectArrayElement(entries, index, item);
        env->DeleteLocalRef(item);
    }
    jstring module_status =
        env->NewStringUTF(result.module_status == nullptr ? "idle" : result.module_status);
    jstring backend = env->NewStringUTF(result.backend == nullptr ? "" : result.backend);
    jstring message = env->NewStringUTF(result.message == nullptr ? "" : result.message);
    jobject instance = env->NewObject(
        cls,
        ctor,
        static_cast<jboolean>(result.ok != 0),
        module_status,
        backend,
        message,
        entries);
    env->DeleteLocalRef(module_status);
    env->DeleteLocalRef(backend);
    env->DeleteLocalRef(message);
    env->DeleteLocalRef(entries);
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
                is_download_task_paused,
                task.get());
            runner(task, message, succeeded, canceled);
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
        task->paused = !succeeded &&
            (message.find("operation paused") != std::string::npos || task->pause_requested.load());
        task->canceled = !task->paused &&
            (canceled || (!succeeded &&
                          (message.find("operation canceled") != std::string::npos ||
                           task->cancel_requested.load())));
        task->module_status =
            task->paused ? "paused" : (task->canceled ? "canceled" : (succeeded ? "succeeded" : "failed"));
        task->message = message;
        if (task->phase.empty()) {
            task->phase =
                task->paused ? "Paused" : (task->canceled ? "Canceled" : (succeeded ? "Complete" : "Failed"));
        }
        task->finished.store(true);
    }).detach();

    return handle;
}

} // namespace

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeFetchDepotBranches(
    JNIEnv* env,
    jclass,
    jlong handle,
    jlong steam_id,
    jint app_id,
    jint max_count,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role) {
    const auto route_selection = make_route_selection(env, route_endpoint, route_protocol, route_role);
    cauth_app_branch_list_t result{};
    const cauth_result_t native_result =
        route_selection.empty()
            ? cauth_depot_fetch_branches(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id), static_cast<unsigned int>(max_count), &result)
            : cauth_depot_fetch_branches_on_route(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id), static_cast<unsigned int>(max_count),
                  route_selection.pointer_or_null(), &result);
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
    jlong steam_id,
    jint app_id,
    jstring branch,
    jint max_count,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role) {
    const char* branch_chars = branch == nullptr ? nullptr : env->GetStringUTFChars(branch, nullptr);
    const auto route_selection = make_route_selection(env, route_endpoint, route_protocol, route_role);
    cauth_depot_manifest_list_t result{};
    const cauth_result_t native_result =
        route_selection.empty()
            ? cauth_depot_fetch_manifests(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id), branch_chars,
                  static_cast<unsigned int>(max_count), &result)
            : cauth_depot_fetch_manifests_on_route(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id), branch_chars,
                  static_cast<unsigned int>(max_count), route_selection.pointer_or_null(), &result);
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
    jlong steam_id,
    jint app_id,
    jstring branch,
    jint max_count,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role) {
    const char* branch_chars = branch == nullptr ? nullptr : env->GetStringUTFChars(branch, nullptr);
    const auto route_selection = make_route_selection(env, route_endpoint, route_protocol, route_role);
    cauth_depot_preflight_report_t result{};
    const cauth_result_t native_result =
        route_selection.empty()
            ? cauth_depot_fetch_preflight(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id), branch_chars,
                  static_cast<unsigned int>(max_count), &result)
            : cauth_depot_fetch_preflight_on_route(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id), branch_chars,
                  static_cast<unsigned int>(max_count), route_selection.pointer_or_null(), &result);
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
    jlong steam_id,
    jint app_id,
    jint depot_id,
    jint max_count,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role) {
    const auto route_selection = make_route_selection(env, route_endpoint, route_protocol, route_role);
    cauth_depot_key_response_t result{};
    const cauth_result_t native_result =
        route_selection.empty()
            ? cauth_depot_fetch_key(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id), static_cast<unsigned int>(depot_id),
                  static_cast<unsigned int>(max_count), &result)
            : cauth_depot_fetch_key_on_route(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id), static_cast<unsigned int>(depot_id),
                  static_cast<unsigned int>(max_count), route_selection.pointer_or_null(), &result);
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
    jlong steam_id,
    jint app_id,
    jint depot_id,
    jlong manifest_gid,
    jstring branch,
    jstring branch_password_hash,
    jint max_count,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role) {
    const char* branch_chars = branch == nullptr ? nullptr : env->GetStringUTFChars(branch, nullptr);
    const char* branch_password_hash_chars = branch_password_hash == nullptr
                                                 ? nullptr
                                                 : env->GetStringUTFChars(branch_password_hash, nullptr);
    const auto route_selection = make_route_selection(env, route_endpoint, route_protocol, route_role);
    cauth_manifest_request_code_response_t result{};
    const cauth_result_t native_result =
        route_selection.empty()
            ? cauth_depot_fetch_manifest_request_code(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id),
                  static_cast<unsigned int>(depot_id), static_cast<unsigned long long>(manifest_gid),
                  branch_chars, branch_password_hash_chars, static_cast<unsigned int>(max_count), &result)
            : cauth_depot_fetch_manifest_request_code_on_route(
                  client_from_handle(handle), static_cast<unsigned long long>(steam_id),
                  static_cast<unsigned int>(app_id),
                  static_cast<unsigned int>(depot_id), static_cast<unsigned long long>(manifest_gid),
                  branch_chars, branch_password_hash_chars, static_cast<unsigned int>(max_count),
                  route_selection.pointer_or_null(), &result);
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

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeProbeDepotDownloadRoutes(
    JNIEnv* env,
    jclass,
    jint max_count) {
    cauth_route_probe_result_t result{};
    const cauth_result_t native_result =
        cauth_depot_probe_download_routes(static_cast<unsigned int>(max_count), &result);
    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Depot routes failed", native_result);
        return nullptr;
    }
    return make_route_probe_snapshot(env, result);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeDownloadDepotManifest(
    JNIEnv* env,
    jclass,
    jint depot_id,
    jlong manifest_gid,
    jlong request_code,
    jint max_count,
    jstring output_path,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role,
    jint write_mode,
    jboolean atomic_write) {
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const auto route_selection = make_route_selection(env, route_endpoint, route_protocol, route_role);
    const cauth_result_t native_result = cauth_depot_download_manifest_on_route(
        static_cast<unsigned int>(depot_id), static_cast<unsigned long long>(manifest_gid),
        static_cast<unsigned long long>(request_code), static_cast<unsigned int>(max_count),
        output_path_chars,
        route_selection.pointer_or_null(),
        static_cast<cauth_file_write_mode_t>(write_mode),
        atomic_write ? 1 : 0);
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
    jstring output_path,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role,
    jint write_mode,
    jboolean atomic_write) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* file_path_chars =
        file_path == nullptr ? nullptr : env->GetStringUTFChars(file_path, nullptr);
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const auto route_selection = make_route_selection(env, route_endpoint, route_protocol, route_role);
    const cauth_result_t native_result = cauth_depot_download_chunk(
        input_path_chars, depot_key_hex_chars, file_path_chars,
        static_cast<unsigned long long>(file_index), has_file_index ? 1 : 0,
        static_cast<unsigned long long>(chunk_index), process_chunk ? 1 : 0,
        static_cast<unsigned int>(max_count), output_path_chars,
        route_selection.pointer_or_null(),
        static_cast<cauth_file_write_mode_t>(write_mode), atomic_write ? 1 : 0);
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
    jstring output_path,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role,
    jint write_mode,
    jboolean atomic_write) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* file_path_chars =
        file_path == nullptr ? nullptr : env->GetStringUTFChars(file_path, nullptr);
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const auto route_selection = make_route_selection(env, route_endpoint, route_protocol, route_role);
    const cauth_result_t native_result = cauth_depot_download_file(
        input_path_chars, depot_key_hex_chars, file_path_chars,
        static_cast<unsigned long long>(file_index), has_file_index ? 1 : 0,
        static_cast<unsigned int>(max_count), output_path_chars,
        route_selection.pointer_or_null(),
        static_cast<cauth_file_write_mode_t>(write_mode), atomic_write ? 1 : 0);
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
    jstring output_root,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role,
    jint write_mode,
    jboolean atomic_write) {
    const char* input_path_chars =
        input_path == nullptr ? nullptr : env->GetStringUTFChars(input_path, nullptr);
    const char* depot_key_hex_chars =
        depot_key_hex == nullptr ? nullptr : env->GetStringUTFChars(depot_key_hex, nullptr);
    const char* output_root_chars =
        output_root == nullptr ? nullptr : env->GetStringUTFChars(output_root, nullptr);
    const auto route_selection = make_route_selection(env, route_endpoint, route_protocol, route_role);
    const cauth_result_t native_result = cauth_depot_download_all_files(
        input_path_chars,
        depot_key_hex_chars,
        static_cast<unsigned int>(max_count),
        output_root_chars,
        route_selection.pointer_or_null(),
        static_cast<cauth_file_write_mode_t>(write_mode),
        atomic_write ? 1 : 0);
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
    jstring output_path,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role,
    jint write_mode,
    jboolean atomic_write) {
    const char* output_path_chars =
        output_path == nullptr ? nullptr : env->GetStringUTFChars(output_path, nullptr);
    const std::string output_path_text = output_path_chars == nullptr ? "" : output_path_chars;
    const std::string route_endpoint_text = copy_jstring(env, route_endpoint);
    const std::string route_protocol_text = copy_jstring(env, route_protocol);
    const std::string route_role_text = copy_jstring(env, route_role);
    if (output_path_chars != nullptr) {
        env->ReleaseStringUTFChars(output_path, output_path_chars);
    }

    return start_download_task(
        DepotDownloadTaskKind::Manifest,
        [depot_id,
         manifest_gid,
         request_code,
         max_count,
         output_path_text,
         route_endpoint_text,
         route_protocol_text,
         route_role_text,
         write_mode,
         atomic_write](const std::shared_ptr<DepotDownloadTask>&,
                       std::string& message,
                       bool& succeeded,
                       bool& canceled) {
            cauth_route_selection_t route_selection{};
            route_selection.endpoint =
                route_endpoint_text.empty() ? nullptr : route_endpoint_text.c_str();
            route_selection.protocol =
                route_protocol_text.empty() ? nullptr : route_protocol_text.c_str();
            route_selection.role = route_role_text.empty() ? nullptr : route_role_text.c_str();
            const auto native_result = cauth_depot_download_manifest_on_route(
                static_cast<unsigned int>(depot_id),
                static_cast<unsigned long long>(manifest_gid),
                static_cast<unsigned long long>(request_code),
                static_cast<unsigned int>(max_count),
                output_path_text.c_str(),
                (route_endpoint_text.empty() && route_protocol_text.empty() && route_role_text.empty())
                    ? nullptr
                    : &route_selection,
                static_cast<cauth_file_write_mode_t>(write_mode),
                atomic_write ? 1 : 0);
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
    jstring output_path,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role,
    jint write_mode,
    jboolean atomic_write) {
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
    const std::string route_endpoint_text = copy_jstring(env, route_endpoint);
    const std::string route_protocol_text = copy_jstring(env, route_protocol);
    const std::string route_role_text = copy_jstring(env, route_role);
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
         output_path_text,
         route_endpoint_text,
         route_protocol_text,
         route_role_text,
         write_mode,
         atomic_write](const std::shared_ptr<DepotDownloadTask>&,
                       std::string& message,
                       bool& succeeded,
                       bool& canceled) {
            cauth_route_selection_t route_selection{};
            route_selection.endpoint =
                route_endpoint_text.empty() ? nullptr : route_endpoint_text.c_str();
            route_selection.protocol =
                route_protocol_text.empty() ? nullptr : route_protocol_text.c_str();
            route_selection.role = route_role_text.empty() ? nullptr : route_role_text.c_str();
            const auto native_result = cauth_depot_download_chunk(
                input_path_text.c_str(),
                depot_key_hex_text.empty() ? nullptr : depot_key_hex_text.c_str(),
                file_path_text.empty() ? nullptr : file_path_text.c_str(),
                static_cast<unsigned long long>(file_index),
                has_file_index ? 1 : 0,
                static_cast<unsigned long long>(chunk_index),
                process_chunk ? 1 : 0,
                static_cast<unsigned int>(max_count),
                output_path_text.c_str(),
                (route_endpoint_text.empty() && route_protocol_text.empty() && route_role_text.empty())
                    ? nullptr
                    : &route_selection,
                static_cast<cauth_file_write_mode_t>(write_mode),
                atomic_write ? 1 : 0);
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
    jstring output_path,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role,
    jint write_mode,
    jboolean atomic_write) {
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
    const std::string route_endpoint_text = copy_jstring(env, route_endpoint);
    const std::string route_protocol_text = copy_jstring(env, route_protocol);
    const std::string route_role_text = copy_jstring(env, route_role);
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
         output_path_text,
         route_endpoint_text,
         route_protocol_text,
         route_role_text,
         write_mode,
         atomic_write](const std::shared_ptr<DepotDownloadTask>&,
                       std::string& message,
                       bool& succeeded,
                       bool& canceled) {
            cauth_route_selection_t route_selection{};
            route_selection.endpoint =
                route_endpoint_text.empty() ? nullptr : route_endpoint_text.c_str();
            route_selection.protocol =
                route_protocol_text.empty() ? nullptr : route_protocol_text.c_str();
            route_selection.role = route_role_text.empty() ? nullptr : route_role_text.c_str();
            const auto native_result = cauth_depot_download_file(
                input_path_text.c_str(),
                depot_key_hex_text.c_str(),
                file_path_text.empty() ? nullptr : file_path_text.c_str(),
                static_cast<unsigned long long>(file_index),
                has_file_index ? 1 : 0,
                static_cast<unsigned int>(max_count),
                output_path_text.c_str(),
                (route_endpoint_text.empty() && route_protocol_text.empty() && route_role_text.empty())
                    ? nullptr
                    : &route_selection,
                static_cast<cauth_file_write_mode_t>(write_mode),
                atomic_write ? 1 : 0);
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
    jstring output_root,
    jstring route_endpoint,
    jstring route_protocol,
    jstring route_role,
    jint write_mode,
    jboolean atomic_write) {
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
    const std::string route_endpoint_text = copy_jstring(env, route_endpoint);
    const std::string route_protocol_text = copy_jstring(env, route_protocol);
    const std::string route_role_text = copy_jstring(env, route_role);
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
         output_root_text,
         route_endpoint_text,
         route_protocol_text,
         route_role_text,
         write_mode,
         atomic_write](const std::shared_ptr<DepotDownloadTask>&,
                       std::string& message,
                       bool& succeeded,
                       bool& canceled) {
            cauth_route_selection_t route_selection{};
            route_selection.endpoint =
                route_endpoint_text.empty() ? nullptr : route_endpoint_text.c_str();
            route_selection.protocol =
                route_protocol_text.empty() ? nullptr : route_protocol_text.c_str();
            route_selection.role = route_role_text.empty() ? nullptr : route_role_text.c_str();
            const auto native_result = cauth_depot_download_all_files(
                input_path_text.c_str(),
                depot_key_hex_text.c_str(),
                static_cast<unsigned int>(max_count),
                output_root_text.c_str(),
                (route_endpoint_text.empty() && route_protocol_text.empty() && route_role_text.empty())
                    ? nullptr
                    : &route_selection,
                static_cast<cauth_file_write_mode_t>(write_mode),
                atomic_write ? 1 : 0);
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

extern "C" JNIEXPORT jlong JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeStartDepotVerifyLocal(
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
    const std::string input_path_text = input_path_chars == nullptr ? "" : input_path_chars;
    const std::string depot_key_hex_text =
        depot_key_hex_chars == nullptr ? "" : depot_key_hex_chars;
    const std::string local_root_text = local_root_chars == nullptr ? "" : local_root_chars;
    const std::string filter_text_value =
        filter_text_chars == nullptr ? "" : filter_text_chars;
    if (input_path_chars != nullptr) env->ReleaseStringUTFChars(input_path, input_path_chars);
    if (depot_key_hex_chars != nullptr) {
        env->ReleaseStringUTFChars(depot_key_hex, depot_key_hex_chars);
    }
    if (local_root_chars != nullptr) env->ReleaseStringUTFChars(local_root, local_root_chars);
    if (filter_text_chars != nullptr) env->ReleaseStringUTFChars(filter_text, filter_text_chars);

    return start_download_task(
        DepotDownloadTaskKind::Verify,
        [input_path_text,
         depot_key_hex_text,
         local_root_text,
         filter_text_value](const std::shared_ptr<DepotDownloadTask>& task,
                            std::string& message,
                            bool& succeeded,
                            bool& canceled) {
            cauth_depot_local_verify_report_t verify_report{};
            const auto native_result = cauth_depot_verify_local_files(
                input_path_text.c_str(),
                depot_key_hex_text.empty() ? nullptr : depot_key_hex_text.c_str(),
                local_root_text.c_str(),
                filter_text_value.empty() ? nullptr : filter_text_value.c_str(),
                &verify_report);
            const char* detail = cauth_depot_last_error_detail();
            message = detail == nullptr ? "" : detail;
            if (message.empty() && native_result != CAUTH_OK) {
                const char* result_message = cauth_result_message(native_result);
                message = result_message == nullptr ? "local verify failed" : result_message;
            }
            canceled = native_result != CAUTH_OK && message.find("operation canceled") != std::string::npos;
            succeeded = native_result == CAUTH_OK;
            if (succeeded) {
                std::lock_guard<std::mutex> lock(task->mutex);
                task->has_verify_report = true;
                fill_depot_verify_storage(verify_report, task->verify_storage, task->verify_report);
                task->verify_report_module_status =
                    verify_report.module_status == nullptr ? "idle" : verify_report.module_status;
                task->verify_report.module_status = task->verify_report_module_status.c_str();
                task->module_status = verify_report.module_status == nullptr
                                          ? "succeeded"
                                          : verify_report.module_status;
            }
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
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativePauseDepotDownloadTask(
    JNIEnv*,
    jclass,
    jlong handle) {
    const auto task = find_download_task(handle);
    if (task == nullptr) {
        return;
    }
    task->pause_requested = true;
    task->cancel_requested = false;
    std::lock_guard lock(task->mutex);
    task->module_status = "pausing";
    if (task->phase.empty() || task->phase == "Queued") {
        task->phase = "Pause requested";
    }
    task->message = "Pause requested...";
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
    task->pause_requested = false;
    std::lock_guard lock(task->mutex);
    task->module_status = "canceling";
    if (task->phase.empty() || task->phase == "Queued") {
        task->phase = "Cancel requested";
    }
    task->message = "Cancel requested...";
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_depot_CAuthNativeSteamDepot_nativeDisposeDepotDownloadTask(
    JNIEnv*,
    jclass,
    jlong handle) {
    std::lock_guard lock(g_download_tasks_mutex);
    g_download_tasks.erase(handle);
}
