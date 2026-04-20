#include "cauth/steam_cloud_ffi.h"

#include "core/session/auth_session.hpp"
#include "ffi/client_internal.hpp"
#include "steam/cloud/steam_cloud_application.hpp"

#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {

constexpr const char* kIllegalStateException = "java/lang/IllegalStateException";
constexpr const char* kLogTag = "CAuthNative";
constexpr const char* kSteamCloudFileEntrySnapshotClassName =
    "com/cauth/android/steam/cloud/SteamCloudFileEntrySnapshot";
constexpr const char* kSteamCloudFileListSnapshotClassName =
    "com/cauth/android/steam/cloud/SteamCloudFileListSnapshot";
constexpr const char* kSteamCloudResultSnapshotClassName =
    "com/cauth/android/steam/cloud/SteamCloudResultSnapshot";
constexpr const char* kSteamCloudVerifySnapshotClassName =
    "com/cauth/android/steam/cloud/SteamCloudVerifySnapshot";
constexpr const char* kSteamCloudTransferTaskSnapshotClassName =
    "com/cauth/android/steam/cloud/SteamCloudTransferTaskSnapshot";

jclass g_steam_cloud_file_entry_snapshot_class = nullptr;
jclass g_steam_cloud_file_list_snapshot_class = nullptr;
jclass g_steam_cloud_result_snapshot_class = nullptr;
jclass g_steam_cloud_verify_snapshot_class = nullptr;
jclass g_steam_cloud_transfer_task_snapshot_class = nullptr;

enum class CloudTransferTaskKind : jint {
    Pull = 1,
    Push = 2,
};

struct CloudTransferRequestParams {
    unsigned int app_id = 0;
    unsigned long long steam_id = 0;
    std::string access_token;
    std::string local_root;
    std::string remote_root;
    bool dry_run = false;
    bool delete_remote_orphans = false;
    cauth_steam_cloud_conflict_policy_t conflict_policy = CAUTH_STEAM_CLOUD_CONFLICT_DEFAULT;
};

struct CloudTransferTask {
    explicit CloudTransferTask(CloudTransferTaskKind task_kind) : kind(task_kind) {}

    std::mutex mutex;
    std::atomic_bool cancel_requested{false};
    std::atomic_bool finished{false};
    CloudTransferTaskKind kind = CloudTransferTaskKind::Pull;
    bool succeeded = false;
    bool canceled = false;
    bool has_result = false;
    std::string phase = "Queued";
    std::string target;
    std::string message;
    std::string result_message;
    std::uint64_t completed_steps = 0;
    std::uint64_t total_steps = 0;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
    cauth_steam_cloud_result_t result{};
};

std::mutex g_transfer_tasks_mutex;
std::unordered_map<jlong, std::shared_ptr<CloudTransferTask>> g_transfer_tasks;
std::atomic<jlong> g_next_transfer_task_handle{1};

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

jclass ensure_steam_cloud_file_entry_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_steam_cloud_file_entry_snapshot_class, kSteamCloudFileEntrySnapshotClassName);
}

jclass ensure_steam_cloud_file_list_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_steam_cloud_file_list_snapshot_class, kSteamCloudFileListSnapshotClassName);
}

jclass ensure_steam_cloud_result_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_steam_cloud_result_snapshot_class, kSteamCloudResultSnapshotClassName);
}

jclass ensure_steam_cloud_verify_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env, g_steam_cloud_verify_snapshot_class, kSteamCloudVerifySnapshotClassName);
}

jclass ensure_steam_cloud_transfer_task_snapshot_class(JNIEnv* env) {
    return require_global_class(
        env,
        g_steam_cloud_transfer_task_snapshot_class,
        kSteamCloudTransferTaskSnapshotClassName);
}

std::string nullable_string(const char* value) {
    return value == nullptr ? std::string{} : std::string{value};
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

cauth::steam::cloud::SteamCloudConflictPolicy from_ffi_conflict_policy(
    cauth_steam_cloud_conflict_policy_t policy) {
    switch (policy) {
    case CAUTH_STEAM_CLOUD_CONFLICT_LOCAL_WINS:
        return cauth::steam::cloud::SteamCloudConflictPolicy::LocalWins;
    case CAUTH_STEAM_CLOUD_CONFLICT_REMOTE_WINS:
        return cauth::steam::cloud::SteamCloudConflictPolicy::RemoteWins;
    case CAUTH_STEAM_CLOUD_CONFLICT_NEWER_WINS:
        return cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins;
    case CAUTH_STEAM_CLOUD_CONFLICT_FAIL_ON_CONFLICT:
        return cauth::steam::cloud::SteamCloudConflictPolicy::FailOnConflict;
    case CAUTH_STEAM_CLOUD_CONFLICT_DEFAULT:
    default:
        return cauth::steam::cloud::SteamCloudConflictPolicy::Default;
    }
}

cauth_steam_cloud_direction_t to_ffi_direction(cauth::steam::cloud::SteamCloudDirection direction) {
    switch (direction) {
    case cauth::steam::cloud::SteamCloudDirection::Push:
        return CAUTH_STEAM_CLOUD_PUSH;
    case cauth::steam::cloud::SteamCloudDirection::Pull:
    default:
        return CAUTH_STEAM_CLOUD_PULL;
    }
}

cauth_steam_cloud_conflict_policy_t to_ffi_conflict_policy(
    cauth::steam::cloud::SteamCloudConflictPolicy policy) {
    switch (policy) {
    case cauth::steam::cloud::SteamCloudConflictPolicy::LocalWins:
        return CAUTH_STEAM_CLOUD_CONFLICT_LOCAL_WINS;
    case cauth::steam::cloud::SteamCloudConflictPolicy::RemoteWins:
        return CAUTH_STEAM_CLOUD_CONFLICT_REMOTE_WINS;
    case cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins:
        return CAUTH_STEAM_CLOUD_CONFLICT_NEWER_WINS;
    case cauth::steam::cloud::SteamCloudConflictPolicy::FailOnConflict:
        return CAUTH_STEAM_CLOUD_CONFLICT_FAIL_ON_CONFLICT;
    case cauth::steam::cloud::SteamCloudConflictPolicy::Default:
    default:
        return CAUTH_STEAM_CLOUD_CONFLICT_DEFAULT;
    }
}

cauth_steam_cloud_request_t make_request(const char* access_token,
                                         const char* local_root,
                                         const char* remote_root,
                                         jint app_id,
                                         jlong steam_id,
                                         jboolean dry_run,
                                         jboolean delete_remote_orphans,
                                         jint conflict_policy) {
    cauth_steam_cloud_request_t request{};
    request.app_id = static_cast<unsigned int>(app_id);
    request.steam_id = static_cast<unsigned long long>(steam_id);
    request.access_token = access_token;
    request.local_root = local_root;
    request.remote_root = remote_root;
    request.dry_run = dry_run ? 1 : 0;
    request.delete_remote_orphans = delete_remote_orphans ? 1 : 0;
    request.conflict_policy = static_cast<cauth_steam_cloud_conflict_policy_t>(conflict_policy);
    return request;
}

CloudTransferRequestParams make_request_params(JNIEnv* env,
                                               jint app_id,
                                               jlong steam_id,
                                               jstring access_token,
                                               jstring local_root,
                                               jstring remote_root,
                                               jboolean dry_run,
                                               jboolean delete_remote_orphans,
                                               jint conflict_policy) {
    CloudTransferRequestParams params;
    params.app_id = static_cast<unsigned int>(app_id);
    params.steam_id = static_cast<unsigned long long>(steam_id);
    params.access_token = copy_jstring(env, access_token);
    params.local_root = copy_jstring(env, local_root);
    params.remote_root = copy_jstring(env, remote_root);
    params.dry_run = dry_run == JNI_TRUE;
    params.delete_remote_orphans = delete_remote_orphans == JNI_TRUE;
    params.conflict_policy = static_cast<cauth_steam_cloud_conflict_policy_t>(conflict_policy);
    return params;
}

cauth::steam::cloud::SteamCloudRequest build_native_request(cauth_client_t* client,
                                                            const CloudTransferRequestParams& params) {
    if (client == nullptr || client->session_repository == nullptr) {
        throw std::runtime_error("client is not initialized");
    }

    cauth::steam::cloud::SteamCloudRequest native_request;
    native_request.app_id = params.app_id;
    native_request.steam_id = params.steam_id;
    native_request.access_token = params.access_token;
    native_request.local_root = params.local_root;
    native_request.remote_root = params.remote_root;
    native_request.dry_run = params.dry_run;
    native_request.delete_remote_orphans = params.delete_remote_orphans;
    native_request.conflict_policy = from_ffi_conflict_policy(params.conflict_policy);
    native_request.backend = cauth::steam::cloud::SteamCloudBackend::Auto;

    if (native_request.access_token.empty() || native_request.refresh_token.empty()) {
        const auto session = native_request.steam_id == 0
                                 ? std::nullopt
                                 : client->session_repository->load_auth_session(
                                       "steam",
                                       std::to_string(native_request.steam_id));
        if (session.has_value()) {
            if (native_request.access_token.empty()) {
                native_request.access_token = session->access_token;
            }
            if (native_request.refresh_token.empty()) {
                native_request.refresh_token = session->refresh_token;
            }
            if (native_request.session_type.empty()) {
                native_request.session_type = session->session_type;
            }
        }
    }

    return native_request;
}

void fill_ffi_result(const cauth::steam::cloud::SteamCloudResult& result,
                     cauth_steam_cloud_result_t& out_result) {
    out_result.ok = result.ok ? 1 : 0;
    out_result.app_id = result.app_id;
    out_result.direction = to_ffi_direction(result.direction);
    out_result.conflict_policy = to_ffi_conflict_policy(result.conflict_policy);
    out_result.local_file_count = result.local_file_count;
    out_result.remote_file_count = result.remote_file_count;
    out_result.transferred_count = result.transferred_count;
    out_result.deleted_count = result.deleted_count;
    out_result.skipped_count = result.skipped_count;
    out_result.conflict_count = result.conflict_count;
    out_result.transferred_bytes = result.transferred_bytes;
}

jobject make_steam_cloud_file_entry(JNIEnv* env, const cauth_steam_cloud_file_entry_t& entry) {
    jclass cls = ensure_steam_cloud_file_entry_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls, "<init>",
        "(IJLjava/lang/String;JILjava/lang/String;JILjava/lang/String;Ljava/lang/String;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring filename = env->NewStringUTF(entry.filename == nullptr ? "" : entry.filename);
    jstring url = env->NewStringUTF(entry.url == nullptr ? "" : entry.url);
    jstring platforms =
        env->NewStringUTF(entry.platforms_to_sync == nullptr ? "" : entry.platforms_to_sync);
    jstring sha = env->NewStringUTF(entry.file_sha == nullptr ? "" : entry.file_sha);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jint>(entry.app_id), static_cast<jlong>(entry.ugc_id), filename,
        static_cast<jlong>(entry.timestamp), static_cast<jint>(entry.file_size), url,
        static_cast<jlong>(entry.steam_id_creator), static_cast<jint>(entry.flags), platforms, sha);
    env->DeleteLocalRef(filename);
    env->DeleteLocalRef(url);
    env->DeleteLocalRef(platforms);
    env->DeleteLocalRef(sha);
    return instance;
}

jobject make_steam_cloud_file_list(JNIEnv* env, const cauth_steam_cloud_file_list_t& result) {
    jclass entry_cls = ensure_steam_cloud_file_entry_snapshot_class(env);
    jclass cls = ensure_steam_cloud_file_list_snapshot_class(env);
    if (entry_cls == nullptr || cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls, "<init>",
        "(ZZIIJ[Lcom/cauth/android/steam/cloud/SteamCloudFileEntrySnapshot;Ljava/lang/String;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jobjectArray entries =
        env->NewObjectArray(static_cast<jsize>(result.file_count), entry_cls, nullptr);
    for (jsize index = 0; index < static_cast<jsize>(result.file_count); ++index) {
        jobject item = make_steam_cloud_file_entry(env, result.files[index]);
        env->SetObjectArrayElement(entries, index, item);
        env->DeleteLocalRef(item);
    }
    jstring message = env->NewStringUTF(result.message == nullptr ? "" : result.message);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(result.ok != 0), static_cast<jboolean>(result.present != 0),
        static_cast<jint>(result.app_id), static_cast<jint>(result.eresult),
        static_cast<jlong>(result.total_files), entries, message);
    env->DeleteLocalRef(entries);
    env->DeleteLocalRef(message);
    return instance;
}

jobject make_steam_cloud_result(JNIEnv* env, const cauth_steam_cloud_result_t& result) {
    jclass cls = ensure_steam_cloud_result_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZIIIJJJJJJJLjava/lang/String;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring message = env->NewStringUTF(result.message == nullptr ? "" : result.message);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(result.ok != 0), static_cast<jint>(result.app_id),
        static_cast<jint>(result.direction), static_cast<jint>(result.conflict_policy),
        static_cast<jlong>(result.local_file_count), static_cast<jlong>(result.remote_file_count),
        static_cast<jlong>(result.transferred_count), static_cast<jlong>(result.deleted_count),
        static_cast<jlong>(result.skipped_count), static_cast<jlong>(result.conflict_count),
        static_cast<jlong>(result.transferred_bytes), message);
    env->DeleteLocalRef(message);
    return instance;
}

jobject make_steam_cloud_verify_snapshot(JNIEnv* env,
                                         const cauth_steam_cloud_verify_report_t& result) {
    jclass cls = ensure_steam_cloud_verify_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(ZZZIJJJJJJJJLjava/lang/String;)V");
    if (ctor == nullptr) {
        return nullptr;
    }
    jstring message = env->NewStringUTF(result.message == nullptr ? "" : result.message);
    jobject instance = env->NewObject(
        cls, ctor, static_cast<jboolean>(result.present != 0),
        static_cast<jboolean>(result.clean != 0),
        static_cast<jboolean>(result.include_extra_local != 0),
        static_cast<jint>(result.app_id),
        static_cast<jlong>(result.checked_count),
        static_cast<jlong>(result.ok_count),
        static_cast<jlong>(result.missing_count),
        static_cast<jlong>(result.mismatched_count),
        static_cast<jlong>(result.size_only_count),
        static_cast<jlong>(result.filtered_out_count),
        static_cast<jlong>(result.extra_local_count),
        static_cast<jlong>(result.total_count),
        message);
    env->DeleteLocalRef(message);
    return instance;
}

jobject make_steam_cloud_transfer_task(JNIEnv* env,
                                       jlong handle,
                                       const std::shared_ptr<CloudTransferTask>& task) {
    jclass cls = ensure_steam_cloud_transfer_task_snapshot_class(env);
    if (cls == nullptr) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(JIZZZZLjava/lang/String;JJJJLjava/lang/String;Ljava/lang/String;Lcom/cauth/android/steam/cloud/SteamCloudResultSnapshot;)V");
    if (ctor == nullptr) {
        return nullptr;
    }

    const auto active = !task->finished.load();
    const auto finished = task->finished.load();
    CloudTransferTaskKind kind = CloudTransferTaskKind::Pull;
    bool succeeded = false;
    bool canceled = false;
    bool has_result = false;
    std::string phase;
    std::string target;
    std::string message;
    std::string result_message;
    std::uint64_t completed_steps = 0;
    std::uint64_t total_steps = 0;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
    cauth_steam_cloud_result_t result_snapshot{};
    {
        std::lock_guard<std::mutex> lock(task->mutex);
        kind = task->kind;
        succeeded = task->succeeded;
        canceled = task->canceled;
        has_result = task->has_result;
        phase = task->phase;
        target = task->target;
        message = task->message;
        completed_steps = task->completed_steps;
        total_steps = task->total_steps;
        completed_bytes = task->completed_bytes;
        total_bytes = task->total_bytes;
        result_message = task->result_message;
        result_snapshot = task->result;
        result_snapshot.message = result_message.empty() ? "" : result_message.c_str();
    }

    jstring phase_string = env->NewStringUTF(phase.c_str());
    jstring target_string = env->NewStringUTF(target.c_str());
    jstring message_string = env->NewStringUTF(message.c_str());
    jobject result =
        has_result ? make_steam_cloud_result(env, result_snapshot) : nullptr;
    jobject instance = env->NewObject(
        cls,
        ctor,
        handle,
        static_cast<jint>(kind),
        static_cast<jboolean>(active),
        static_cast<jboolean>(finished),
        static_cast<jboolean>(canceled),
        static_cast<jboolean>(succeeded),
        phase_string,
        static_cast<jlong>(completed_steps),
        static_cast<jlong>(total_steps),
        static_cast<jlong>(completed_bytes),
        static_cast<jlong>(total_bytes),
        target_string,
        message_string,
        result);
    env->DeleteLocalRef(phase_string);
    env->DeleteLocalRef(target_string);
    env->DeleteLocalRef(message_string);
    if (result != nullptr) {
        env->DeleteLocalRef(result);
    }
    return instance;
}

std::shared_ptr<CloudTransferTask> find_transfer_task(jlong task_handle) {
    std::lock_guard<std::mutex> lock(g_transfer_tasks_mutex);
    const auto it = g_transfer_tasks.find(task_handle);
    if (it == g_transfer_tasks.end()) {
        return nullptr;
    }
    return it->second;
}

void erase_transfer_task(jlong task_handle) {
    std::lock_guard<std::mutex> lock(g_transfer_tasks_mutex);
    g_transfer_tasks.erase(task_handle);
}

void on_transfer_progress(const cauth::steam::cloud::SteamCloudTransferProgress& progress,
                          void* user_data) {
    auto* task = static_cast<CloudTransferTask*>(user_data);
    if (task == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(task->mutex);
    task->phase = progress.phase;
    task->target = progress.target;
    task->completed_steps = progress.completed_steps;
    task->total_steps = progress.total_steps;
    task->completed_bytes = progress.completed_bytes;
    task->total_bytes = progress.total_bytes;
    task->message = progress.phase;
}

bool is_transfer_cancel_requested(void* user_data) {
    auto* task = static_cast<CloudTransferTask*>(user_data);
    return task != nullptr && task->cancel_requested.load();
}

void run_cloud_transfer_task(jlong client_handle,
                             jlong task_handle,
                             const CloudTransferRequestParams& params,
                             CloudTransferTaskKind kind) {
    const auto task = find_transfer_task(task_handle);
    if (task == nullptr) {
        return;
    }

    cauth_result_t native_result = CAUTH_ERROR_INTERNAL;
    cauth_steam_cloud_result_t ffi_result{};
    std::string result_message;

    try {
        auto* client = client_from_handle(client_handle);
        const auto native_request = build_native_request(client, params);
        cauth::steam::cloud::set_current_thread_steam_cloud_transfer_hooks(
            &on_transfer_progress, &is_transfer_cancel_requested, task.get());
        const auto result =
            kind == CloudTransferTaskKind::Pull ? cauth::steam::cloud::pull_cloud_save(native_request)
                                                : cauth::steam::cloud::push_cloud_save(native_request);
        cauth::steam::cloud::clear_current_thread_steam_cloud_transfer_hooks();
        fill_ffi_result(result, ffi_result);
        result_message = result.message;
        ffi_result.message = result_message.empty() ? "" : result_message.c_str();
        native_result = CAUTH_OK;
    } catch (const std::bad_alloc&) {
        native_result = CAUTH_ERROR_OUT_OF_MEMORY;
        cauth::steam::cloud::clear_current_thread_steam_cloud_transfer_hooks();
    } catch (...) {
        native_result = CAUTH_ERROR_INTERNAL;
        cauth::steam::cloud::clear_current_thread_steam_cloud_transfer_hooks();
    }

    {
        std::lock_guard<std::mutex> lock(task->mutex);
        task->has_result = native_result == CAUTH_OK;
        task->result = ffi_result;
        task->result_message = std::move(result_message);
        task->result.message =
            task->result_message.empty() ? "" : task->result_message.c_str();
        task->succeeded = native_result == CAUTH_OK && task->result.ok != 0;
        task->canceled =
            task->cancel_requested.load() &&
            ((task->has_result && task->result_message.find("operation canceled") != std::string::npos) ||
             native_result != CAUTH_OK);
        const char* native_message = cauth_result_message(native_result);
        task->message =
            task->has_result ? task->result_message
                             : std::string{native_message == nullptr ? "internal error" : native_message};
        if (task->phase.empty()) {
            task->phase = kind == CloudTransferTaskKind::Pull ? "Pull finished" : "Push finished";
        }
        if (task->has_result) {
            const auto completed_items =
                task->result.transferred_count + task->result.deleted_count + task->result.skipped_count;
            if (task->completed_steps < completed_items) {
                task->completed_steps = completed_items;
            }
            if (task->total_steps < completed_items) {
                task->total_steps = completed_items;
            }
            if (task->completed_bytes < task->result.transferred_bytes) {
                task->completed_bytes = task->result.transferred_bytes;
            }
            if (task->total_bytes < task->completed_bytes) {
                task->total_bytes = task->completed_bytes;
            }
        }
    }
    task->finished.store(true);
}

} // namespace

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_cloud_CAuthNativeSteamCloud_nativeListRemoteFiles(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jlong steam_id,
    jstring access_token,
    jstring local_root,
    jstring remote_root,
    jboolean dry_run,
    jboolean delete_remote_orphans,
    jint conflict_policy,
    jint count,
    jint start_index,
    jboolean extended_details) {
    const char* access_token_chars =
        access_token == nullptr ? nullptr : env->GetStringUTFChars(access_token, nullptr);
    const char* local_root_chars =
        local_root == nullptr ? nullptr : env->GetStringUTFChars(local_root, nullptr);
    const char* remote_root_chars =
        remote_root == nullptr ? nullptr : env->GetStringUTFChars(remote_root, nullptr);

    const auto request = make_request(access_token_chars, local_root_chars, remote_root_chars,
                                      app_id, steam_id, dry_run, delete_remote_orphans, conflict_policy);
    cauth_steam_cloud_file_list_t result{};
    const cauth_result_t native_result = cauth_steam_cloud_list_remote_files(
        client_from_handle(handle), &request, static_cast<unsigned int>(count),
        static_cast<unsigned int>(start_index), extended_details ? 1 : 0, &result);

    if (access_token_chars != nullptr) {
        env->ReleaseStringUTFChars(access_token, access_token_chars);
    }
    if (local_root_chars != nullptr) {
        env->ReleaseStringUTFChars(local_root, local_root_chars);
    }
    if (remote_root_chars != nullptr) {
        env->ReleaseStringUTFChars(remote_root, remote_root_chars);
    }

    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Steam cloud list failed", native_result);
        return nullptr;
    }
    return make_steam_cloud_file_list(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_cloud_CAuthNativeSteamCloud_nativePull(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jlong steam_id,
    jstring access_token,
    jstring local_root,
    jstring remote_root,
    jboolean dry_run,
    jboolean delete_remote_orphans,
    jint conflict_policy) {
    const char* access_token_chars =
        access_token == nullptr ? nullptr : env->GetStringUTFChars(access_token, nullptr);
    const char* local_root_chars =
        local_root == nullptr ? nullptr : env->GetStringUTFChars(local_root, nullptr);
    const char* remote_root_chars =
        remote_root == nullptr ? nullptr : env->GetStringUTFChars(remote_root, nullptr);

    const auto request = make_request(access_token_chars, local_root_chars, remote_root_chars,
                                      app_id, steam_id, dry_run, delete_remote_orphans, conflict_policy);
    cauth_steam_cloud_result_t result{};
    const cauth_result_t native_result =
        cauth_steam_cloud_pull(client_from_handle(handle), &request, &result);

    if (access_token_chars != nullptr) {
        env->ReleaseStringUTFChars(access_token, access_token_chars);
    }
    if (local_root_chars != nullptr) {
        env->ReleaseStringUTFChars(local_root, local_root_chars);
    }
    if (remote_root_chars != nullptr) {
        env->ReleaseStringUTFChars(remote_root, remote_root_chars);
    }

    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Steam cloud pull failed", native_result);
        return nullptr;
    }
    return make_steam_cloud_result(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_cloud_CAuthNativeSteamCloud_nativePush(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jlong steam_id,
    jstring access_token,
    jstring local_root,
    jstring remote_root,
    jboolean dry_run,
    jboolean delete_remote_orphans,
    jint conflict_policy) {
    const char* access_token_chars =
        access_token == nullptr ? nullptr : env->GetStringUTFChars(access_token, nullptr);
    const char* local_root_chars =
        local_root == nullptr ? nullptr : env->GetStringUTFChars(local_root, nullptr);
    const char* remote_root_chars =
        remote_root == nullptr ? nullptr : env->GetStringUTFChars(remote_root, nullptr);

    const auto request = make_request(access_token_chars, local_root_chars, remote_root_chars,
                                      app_id, steam_id, dry_run, delete_remote_orphans, conflict_policy);
    cauth_steam_cloud_result_t result{};
    const cauth_result_t native_result =
        cauth_steam_cloud_push(client_from_handle(handle), &request, &result);

    if (access_token_chars != nullptr) {
        env->ReleaseStringUTFChars(access_token, access_token_chars);
    }
    if (local_root_chars != nullptr) {
        env->ReleaseStringUTFChars(local_root, local_root_chars);
    }
    if (remote_root_chars != nullptr) {
        env->ReleaseStringUTFChars(remote_root, remote_root_chars);
    }

    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Steam cloud push failed", native_result);
        return nullptr;
    }
    return make_steam_cloud_result(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_cloud_CAuthNativeSteamCloud_nativeVerifyLocalFiles(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jlong steam_id,
    jstring access_token,
    jstring local_root,
    jstring remote_root,
    jboolean dry_run,
    jboolean delete_remote_orphans,
    jint conflict_policy,
    jboolean include_extra_local) {
    const char* access_token_chars =
        access_token == nullptr ? nullptr : env->GetStringUTFChars(access_token, nullptr);
    const char* local_root_chars =
        local_root == nullptr ? nullptr : env->GetStringUTFChars(local_root, nullptr);
    const char* remote_root_chars =
        remote_root == nullptr ? nullptr : env->GetStringUTFChars(remote_root, nullptr);

    const auto request = make_request(access_token_chars, local_root_chars, remote_root_chars,
                                      app_id, steam_id, dry_run, delete_remote_orphans, conflict_policy);
    cauth_steam_cloud_verify_report_t result{};
    const cauth_result_t native_result = cauth_steam_cloud_verify_local_files(
        client_from_handle(handle), &request, include_extra_local ? 1 : 0, &result);

    if (access_token_chars != nullptr) {
        env->ReleaseStringUTFChars(access_token, access_token_chars);
    }
    if (local_root_chars != nullptr) {
        env->ReleaseStringUTFChars(local_root, local_root_chars);
    }
    if (remote_root_chars != nullptr) {
        env->ReleaseStringUTFChars(remote_root, remote_root_chars);
    }

    if (native_result != CAUTH_OK) {
        throw_result_exception(env, "Steam cloud verify failed", native_result);
        return nullptr;
    }
    return make_steam_cloud_verify_snapshot(env, result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cauth_android_steam_cloud_CAuthNativeSteamCloud_nativeStartPull(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jlong steam_id,
    jstring access_token,
    jstring local_root,
    jstring remote_root,
    jboolean dry_run,
    jboolean delete_remote_orphans,
    jint conflict_policy) {
    if (handle == 0 || app_id <= 0 || steam_id <= 0) {
        env->ThrowNew(env->FindClass(kIllegalStateException), "Cloud pull start failed: invalid argument");
        return 0;
    }

    const auto params = make_request_params(
        env, app_id, steam_id, access_token, local_root, remote_root, dry_run, delete_remote_orphans, conflict_policy);
    const auto task = std::make_shared<CloudTransferTask>(CloudTransferTaskKind::Pull);
    const jlong task_handle = g_next_transfer_task_handle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(g_transfer_tasks_mutex);
        g_transfer_tasks[task_handle] = task;
    }
    std::thread([handle, task_handle, params]() {
        run_cloud_transfer_task(handle, task_handle, params, CloudTransferTaskKind::Pull);
    }).detach();
    return task_handle;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cauth_android_steam_cloud_CAuthNativeSteamCloud_nativeStartPush(
    JNIEnv* env,
    jclass,
    jlong handle,
    jint app_id,
    jlong steam_id,
    jstring access_token,
    jstring local_root,
    jstring remote_root,
    jboolean dry_run,
    jboolean delete_remote_orphans,
    jint conflict_policy) {
    if (handle == 0 || app_id <= 0 || steam_id <= 0) {
        env->ThrowNew(env->FindClass(kIllegalStateException), "Cloud push start failed: invalid argument");
        return 0;
    }

    const auto params = make_request_params(
        env, app_id, steam_id, access_token, local_root, remote_root, dry_run, delete_remote_orphans, conflict_policy);
    const auto task = std::make_shared<CloudTransferTask>(CloudTransferTaskKind::Push);
    const jlong task_handle = g_next_transfer_task_handle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(g_transfer_tasks_mutex);
        g_transfer_tasks[task_handle] = task;
    }
    std::thread([handle, task_handle, params]() {
        run_cloud_transfer_task(handle, task_handle, params, CloudTransferTaskKind::Push);
    }).detach();
    return task_handle;
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cauth_android_steam_cloud_CAuthNativeSteamCloud_nativePollTransferTask(
    JNIEnv* env,
    jclass,
    jlong task_handle) {
    const auto task = find_transfer_task(task_handle);
    if (task == nullptr) {
        env->ThrowNew(env->FindClass(kIllegalStateException), "Cloud transfer task not found");
        return nullptr;
    }
    return make_steam_cloud_transfer_task(env, task_handle, task);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_cloud_CAuthNativeSteamCloud_nativeCancelTransferTask(
    JNIEnv*,
    jclass,
    jlong task_handle) {
    const auto task = find_transfer_task(task_handle);
    if (task == nullptr) {
        return;
    }
    task->cancel_requested.store(true);
    std::lock_guard<std::mutex> lock(task->mutex);
    task->message = "Cancel requested...";
}

extern "C" JNIEXPORT void JNICALL
Java_com_cauth_android_steam_cloud_CAuthNativeSteamCloud_nativeDisposeTransferTask(
    JNIEnv*,
    jclass,
    jlong task_handle) {
    erase_transfer_task(task_handle);
}
