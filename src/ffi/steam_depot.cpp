#include "cauth/steam_depot_ffi.h"

#include "ffi/client_internal.hpp"
#include "steam/auth/steam_auth_provider.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/depot/depot_preflight.hpp"
#include "steam/depot/depot_resolver.hpp"
#include "steam/depot/steam_depot_application.hpp"
#include "steam/depot/depot_cm_client.hpp"

#include <sstream>
#include <exception>
#include <string>
#include <vector>

namespace {

thread_local std::string g_last_depot_key_hex;
thread_local std::string g_last_manifest_branch;
thread_local std::string g_last_preflight_branch;
thread_local std::string g_last_preflight_build_id;
thread_local std::string g_last_error_detail;
thread_local std::vector<std::string> g_branch_names;
thread_local std::vector<std::string> g_branch_build_ids;
thread_local std::vector<std::string> g_branch_descriptions;
thread_local std::vector<cauth_app_branch_entry_t> g_branch_entries;
thread_local std::vector<std::string> g_manifest_platform_labels;
thread_local std::vector<std::string> g_manifest_os_lists;
thread_local std::vector<std::string> g_manifest_os_arches;
thread_local std::vector<std::string> g_manifest_depot_from_apps;
thread_local std::vector<cauth_depot_manifest_entry_t> g_manifest_entries;
thread_local std::vector<std::string> g_preflight_platform_labels;
thread_local std::vector<std::string> g_preflight_os_lists;
thread_local std::vector<std::string> g_preflight_os_arches;
thread_local std::vector<std::string> g_preflight_depot_from_apps;
thread_local std::vector<cauth_depot_preflight_entry_t> g_preflight_entries;
thread_local std::vector<std::string> g_preflight_access_statuses;
thread_local std::vector<std::string> g_manifest_file_names;
thread_local std::vector<cauth_manifest_file_entry_t> g_manifest_file_entries;

std::string nullable_string(const char* value) {
    return value == nullptr ? std::string{} : std::string{value};
}

void clear_last_error_detail() {
    g_last_error_detail.clear();
}

void set_last_error_detail(std::string detail) {
    g_last_error_detail = std::move(detail);
}

std::optional<std::vector<std::uint8_t>> hex_to_bytes(std::string_view hex) {
    if (hex.empty()) {
        return std::vector<std::uint8_t>{};
    }
    if ((hex.size() % 2) != 0) {
        return std::nullopt;
    }

    auto decode_nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };

    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t index = 0; index < hex.size(); index += 2) {
        const auto hi = decode_nibble(hex[index]);
        const auto lo = decode_nibble(hex[index + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return bytes;
}

std::optional<cauth::core::depot::AppInfo> fetch_app_info(cauth_client_t* client,
                                                          unsigned int app_id,
                                                          unsigned int max_count) {
    cauth::steam::auth::StoredSteamAuthProvider auth_provider{*client->session_repository};
    cauth::core::depot::DepotCmClient depot_client{auth_provider};
    return depot_client.fetch_app_info(app_id, max_count);
}

std::optional<std::vector<std::uint8_t>> parse_optional_depot_key_hex(const char* depot_key_hex,
                                                                      cauth_result_t& result) {
    result = CAUTH_OK;
    if (depot_key_hex == nullptr || *depot_key_hex == '\0') {
        return std::vector<std::uint8_t>{};
    }
    const auto bytes = hex_to_bytes(depot_key_hex);
    if (!bytes.has_value()) {
        result = CAUTH_ERROR_INVALID_ARGUMENT;
        return std::nullopt;
    }
    return bytes;
}

cauth_result_t load_manifest_for_file_operation(const char* input_path,
                                                const char* depot_key_hex,
                                                cauth::steam::depot::LoadedDepotManifest& loaded_manifest) {
    clear_last_error_detail();
    if (input_path == nullptr || *input_path == '\0') {
        set_last_error_detail("manifest input path is required");
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    cauth_result_t key_result = CAUTH_OK;
    const auto depot_key_bytes = parse_optional_depot_key_hex(depot_key_hex, key_result);
    if (key_result != CAUTH_OK) {
        set_last_error_detail("invalid depot key hex");
        return key_result;
    }

    std::optional<std::vector<std::uint8_t>> depot_key;
    if (depot_key_bytes.has_value() && !depot_key_bytes->empty()) {
        depot_key = *depot_key_bytes;
    }

    std::ostringstream err;
    if (!cauth::steam::depot::load_manifest_from_path(
            input_path,
            depot_key,
            loaded_manifest,
            err)) {
        set_last_error_detail(err.str());
        return CAUTH_ERROR_INTERNAL;
    }
    return CAUTH_OK;
}

} // namespace

cauth_result_t cauth_depot_fetch_branches(cauth_client_t* client,
                                          unsigned int app_id,
                                          unsigned int max_count,
                                          cauth_app_branch_list_t* out_response) {
    if (client == nullptr || out_response == nullptr || app_id == 0 || max_count == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->app_id = 0;
    out_response->branch_count = 0;
    out_response->branches = nullptr;
    g_branch_names.clear();
    g_branch_build_ids.clear();
    g_branch_descriptions.clear();
    g_branch_entries.clear();

    try {
        const auto app_info = fetch_app_info(client, app_id, max_count);
        if (!app_info.has_value()) {
            return CAUTH_OK;
        }

        g_branch_names.reserve(app_info->branches.size());
        g_branch_build_ids.reserve(app_info->branches.size());
        g_branch_descriptions.reserve(app_info->branches.size());
        for (const auto& branch : app_info->branches) {
            g_branch_names.push_back(branch.name);
            g_branch_build_ids.push_back(branch.build_id);
            g_branch_descriptions.push_back(branch.description);
        }

        g_branch_entries.reserve(app_info->branches.size());
        for (std::size_t index = 0; index < app_info->branches.size(); ++index) {
            const auto& branch = app_info->branches[index];
            g_branch_entries.push_back(cauth_app_branch_entry_t{
                g_branch_names[index].c_str(),
                g_branch_build_ids[index].c_str(),
                g_branch_descriptions[index].c_str(),
                branch.time_updated,
                branch.password_required ? 1 : 0,
            });
        }

        out_response->present = 1;
        out_response->app_id = app_info->app_id;
        out_response->branch_count = static_cast<unsigned long long>(g_branch_entries.size());
        out_response->branches = g_branch_entries.empty() ? nullptr : g_branch_entries.data();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_fetch_manifests(cauth_client_t* client,
                                           unsigned int app_id,
                                           const char* branch,
                                           unsigned int max_count,
                                           cauth_depot_manifest_list_t* out_response) {
    if (client == nullptr || out_response == nullptr || app_id == 0 || max_count == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->app_id = 0;
    out_response->branch = "";
    out_response->manifest_count = 0;
    out_response->manifests = nullptr;
    g_manifest_platform_labels.clear();
    g_manifest_os_lists.clear();
    g_manifest_os_arches.clear();
    g_manifest_depot_from_apps.clear();
    g_manifest_entries.clear();
    g_last_manifest_branch = branch == nullptr || *branch == '\0' ? "public" : branch;

    try {
        const auto app_info = fetch_app_info(client, app_id, max_count);
        if (!app_info.has_value()) {
            return CAUTH_OK;
        }

        const auto selection =
            cauth::core::depot::resolve_depot_manifests(*app_info, g_last_manifest_branch);
        if (!selection.has_value()) {
            return CAUTH_OK;
        }

        g_manifest_platform_labels.reserve(selection->manifests.size());
        g_manifest_os_lists.reserve(selection->manifests.size());
        g_manifest_os_arches.reserve(selection->manifests.size());
        g_manifest_depot_from_apps.reserve(selection->manifests.size());
        for (const auto& manifest : selection->manifests) {
            g_manifest_platform_labels.push_back(manifest.platform_label);
            g_manifest_os_lists.push_back(manifest.os_list);
            g_manifest_os_arches.push_back(manifest.os_arch);
            g_manifest_depot_from_apps.push_back(manifest.depot_from_app);
        }

        g_manifest_entries.reserve(selection->manifests.size());
        for (std::size_t index = 0; index < selection->manifests.size(); ++index) {
            const auto& manifest = selection->manifests[index];
            g_manifest_entries.push_back(cauth_depot_manifest_entry_t{
                manifest.depot_id,
                manifest.manifest_gid,
                manifest.size,
                manifest.download_size,
                manifest.encrypted ? 1 : 0,
                g_manifest_platform_labels[index].c_str(),
                g_manifest_os_lists[index].c_str(),
                g_manifest_os_arches[index].c_str(),
                g_manifest_depot_from_apps[index].c_str(),
                manifest.shared_install ? 1 : 0,
            });
        }

        out_response->present = 1;
        out_response->app_id = app_info->app_id;
        out_response->branch = g_last_manifest_branch.c_str();
        out_response->manifest_count = static_cast<unsigned long long>(g_manifest_entries.size());
        out_response->manifests = g_manifest_entries.empty() ? nullptr : g_manifest_entries.data();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_fetch_preflight(cauth_client_t* client,
                                           unsigned int app_id,
                                           const char* branch,
                                           unsigned int max_count,
                                           cauth_depot_preflight_report_t* out_response) {
    if (client == nullptr || out_response == nullptr || app_id == 0 || max_count == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->app_id = 0;
    out_response->branch = "";
    out_response->build_id = "";
    out_response->depot_count = 0;
    out_response->depots = nullptr;
    g_preflight_platform_labels.clear();
    g_preflight_os_lists.clear();
    g_preflight_os_arches.clear();
    g_preflight_depot_from_apps.clear();
    g_preflight_entries.clear();
    g_preflight_access_statuses.clear();
    g_last_preflight_build_id.clear();
    g_last_preflight_branch = branch == nullptr || *branch == '\0' ? "public" : branch;

    try {
        const auto app_info = fetch_app_info(client, app_id, max_count);
        if (!app_info.has_value()) {
            return CAUTH_OK;
        }

        const auto selection =
            cauth::core::depot::resolve_depot_manifests(*app_info, g_last_preflight_branch);
        if (!selection.has_value()) {
            return CAUTH_OK;
        }

        cauth::steam::auth::StoredSteamAuthProvider auth_provider{*client->session_repository};
        cauth::core::depot::DepotCmClient depot_client{auth_provider};

        std::vector<cauth::core::depot::DepotDecryptionKeyResponse> key_responses;
        key_responses.reserve(selection->manifests.size());
        for (const auto& manifest : selection->manifests) {
            const auto key_response = depot_client.fetch_depot_key(app_id, manifest.depot_id, max_count);
            if (key_response.has_value()) {
                key_responses.push_back(*key_response);
            } else {
                key_responses.push_back(cauth::core::depot::DepotDecryptionKeyResponse{});
            }
        }

        const auto report = cauth::core::depot::make_depot_preflight_report(
            *app_info, *selection, key_responses);
        if (!report.has_value()) {
            return CAUTH_OK;
        }

        g_last_preflight_build_id = report->build_id;
        g_preflight_platform_labels.reserve(report->depots.size());
        g_preflight_os_lists.reserve(report->depots.size());
        g_preflight_os_arches.reserve(report->depots.size());
        g_preflight_depot_from_apps.reserve(report->depots.size());
        g_preflight_access_statuses.reserve(report->depots.size());
        for (const auto& depot : report->depots) {
            g_preflight_platform_labels.push_back(depot.manifest.platform_label);
            g_preflight_os_lists.push_back(depot.manifest.os_list);
            g_preflight_os_arches.push_back(depot.manifest.os_arch);
            g_preflight_depot_from_apps.push_back(depot.manifest.depot_from_app);
            g_preflight_access_statuses.push_back(
                cauth::core::depot::depot_access_status_name(depot.status));
        }

        g_preflight_entries.reserve(report->depots.size());
        for (std::size_t index = 0; index < report->depots.size(); ++index) {
            const auto& depot = report->depots[index];
            g_preflight_entries.push_back(cauth_depot_preflight_entry_t{
                depot.manifest.depot_id,
                depot.manifest.manifest_gid,
                depot.manifest.size,
                depot.manifest.download_size,
                depot.manifest.encrypted ? 1 : 0,
                g_preflight_platform_labels[index].c_str(),
                g_preflight_os_lists[index].c_str(),
                g_preflight_os_arches[index].c_str(),
                g_preflight_depot_from_apps[index].c_str(),
                depot.manifest.shared_install ? 1 : 0,
                g_preflight_access_statuses[index].c_str(),
                depot.key_eresult,
                depot.key_available ? 1 : 0,
            });
        }

        out_response->present = 1;
        out_response->app_id = report->app_id;
        out_response->branch = g_last_preflight_branch.c_str();
        out_response->build_id = g_last_preflight_build_id.c_str();
        out_response->depot_count = static_cast<unsigned long long>(g_preflight_entries.size());
        out_response->depots = g_preflight_entries.empty() ? nullptr : g_preflight_entries.data();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_download_manifest(unsigned int depot_id,
                                             unsigned long long manifest_gid,
                                             unsigned long long request_code,
                                             unsigned int max_count,
                                             const char* output_path) {
    clear_last_error_detail();
    if (depot_id == 0 || manifest_gid == 0 || request_code == 0 || max_count == 0 ||
        output_path == nullptr || *output_path == '\0') {
        set_last_error_detail("depot_id, manifest_gid, request_code, max_count, and output_path are required");
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        std::ostringstream out;
        std::ostringstream err;
        const auto exit_code = cauth::steam::depot::download_manifest_to_path(
            depot_id,
            manifest_gid,
            request_code,
            max_count,
            output_path,
            out,
            err);
        if (exit_code == 0) {
            return CAUTH_OK;
        }
        if (!err.str().empty()) {
            set_last_error_detail(err.str());
        } else if (!out.str().empty()) {
            set_last_error_detail(out.str());
        } else {
            set_last_error_detail("download_manifest_to_path returned a non-zero exit code");
        }
        return CAUTH_ERROR_INTERNAL;
    } catch (const std::exception& ex) {
        set_last_error_detail(ex.what());
        return CAUTH_ERROR_INTERNAL;
    } catch (...) {
        set_last_error_detail("unknown exception");
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_fetch_key(cauth_client_t* client,
                                     unsigned int app_id,
                                     unsigned int depot_id,
                                     unsigned int max_count,
                                     cauth_depot_key_response_t* out_response) {
    if (client == nullptr || out_response == nullptr || app_id == 0 || depot_id == 0 ||
        max_count == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->eresult = 0;
    out_response->depot_id = 0;
    out_response->key_hex = "";

    try {
        cauth::steam::auth::StoredSteamAuthProvider auth_provider{*client->session_repository};
        cauth::core::depot::DepotCmClient depot_client{auth_provider};
        const auto response = depot_client.fetch_depot_key(app_id, depot_id, max_count);
        if (!response.has_value()) {
            return CAUTH_OK;
        }

        g_last_depot_key_hex = cauth::core::cm::bytes_to_hex(response->key);
        out_response->present = 1;
        out_response->eresult = response->eresult;
        out_response->depot_id = response->depot_id;
        out_response->key_hex = g_last_depot_key_hex.c_str();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_load_manifest_info(const char* input_path,
                                              const char* depot_key_hex,
                                              cauth_manifest_info_t* out_response) {
    if (input_path == nullptr || *input_path == '\0' || out_response == nullptr) {
        clear_last_error_detail();
        set_last_error_detail("manifest input path and output struct are required");
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->depot_id = 0;
    out_response->manifest_gid = 0;
    out_response->creation_time = 0;
    out_response->filenames_encrypted = 0;
    out_response->file_count = 0;
    out_response->chunk_count = 0;
    out_response->total_uncompressed_size = 0;
    out_response->total_compressed_size = 0;
    out_response->unique_chunks = 0;
    clear_last_error_detail();

    try {
        cauth_result_t key_result = CAUTH_OK;
        const auto depot_key_bytes = parse_optional_depot_key_hex(depot_key_hex, key_result);
        if (key_result != CAUTH_OK) {
            set_last_error_detail("invalid depot key hex");
            return key_result;
        }

        std::optional<std::vector<std::uint8_t>> depot_key;
        if (depot_key_bytes.has_value() && !depot_key_bytes->empty()) {
            depot_key = *depot_key_bytes;
        }

        cauth::steam::depot::LoadedDepotManifest loaded_manifest;
        std::ostringstream err;
        if (!cauth::steam::depot::load_manifest_from_path(
                input_path,
                depot_key,
                loaded_manifest,
                err)) {
            set_last_error_detail(err.str());
            return CAUTH_ERROR_INTERNAL;
        }

        std::uint64_t chunk_count = 0;
        for (const auto& file : loaded_manifest.manifest.files) {
            chunk_count += file.chunks.size();
        }

        out_response->present = 1;
        out_response->depot_id = loaded_manifest.manifest.depot_id;
        out_response->manifest_gid = loaded_manifest.manifest.manifest_gid;
        out_response->creation_time = loaded_manifest.manifest.creation_time;
        out_response->filenames_encrypted = loaded_manifest.manifest.filenames_encrypted ? 1 : 0;
        out_response->file_count =
            static_cast<unsigned long long>(loaded_manifest.manifest.files.size());
        out_response->chunk_count = chunk_count;
        out_response->total_uncompressed_size = loaded_manifest.manifest.total_uncompressed_size;
        out_response->total_compressed_size = loaded_manifest.manifest.total_compressed_size;
        out_response->unique_chunks = loaded_manifest.manifest.unique_chunks;
        return CAUTH_OK;
    } catch (const std::exception& exception) {
        set_last_error_detail(exception.what());
        return CAUTH_ERROR_INTERNAL;
    } catch (...) {
        set_last_error_detail("unexpected exception while loading manifest info");
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_list_manifest_files(const char* input_path,
                                               const char* depot_key_hex,
                                               const char* filter_text,
                                               unsigned int limit,
                                               cauth_manifest_file_list_t* out_response) {
    if (input_path == nullptr || *input_path == '\0' || out_response == nullptr || limit == 0) {
        clear_last_error_detail();
        set_last_error_detail("manifest input path, output struct, and limit are required");
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->matched_count = 0;
    out_response->printed_count = 0;
    out_response->total_count = 0;
    out_response->files = nullptr;
    g_manifest_file_names.clear();
    g_manifest_file_entries.clear();
    clear_last_error_detail();

    try {
        cauth_result_t key_result = CAUTH_OK;
        const auto depot_key_bytes = parse_optional_depot_key_hex(depot_key_hex, key_result);
        if (key_result != CAUTH_OK) {
            set_last_error_detail("invalid depot key hex");
            return key_result;
        }

        std::optional<std::vector<std::uint8_t>> depot_key;
        if (depot_key_bytes.has_value() && !depot_key_bytes->empty()) {
            depot_key = *depot_key_bytes;
        }

        cauth::steam::depot::LoadedDepotManifest loaded_manifest;
        std::ostringstream err;
        if (!cauth::steam::depot::load_manifest_from_path(
                input_path,
                depot_key,
                loaded_manifest,
                err)) {
            set_last_error_detail(err.str());
            return CAUTH_ERROR_INTERNAL;
        }

        const auto filter = nullable_string(filter_text);
        std::uint64_t matched_count = 0;
        std::uint64_t total_count = 0;
        for (const auto& file : loaded_manifest.manifest.files) {
            if (cauth::core::depot::depot_file_is_directory(file)) {
                continue;
            }
            ++total_count;
            if (!filter.empty() &&
                file.filename.find(filter) == std::string::npos) {
                continue;
            }
            ++matched_count;
            if (g_manifest_file_entries.size() >= limit) {
                continue;
            }
            g_manifest_file_names.push_back(file.filename);
        }

        g_manifest_file_entries.reserve(g_manifest_file_names.size());
        std::size_t printed_index = 0;
        for (const auto& file : loaded_manifest.manifest.files) {
            if (cauth::core::depot::depot_file_is_directory(file)) {
                continue;
            }
            if (!filter.empty() &&
                file.filename.find(filter) == std::string::npos) {
                continue;
            }
            if (printed_index >= g_manifest_file_names.size()) {
                break;
            }
            g_manifest_file_entries.push_back(cauth_manifest_file_entry_t{
                g_manifest_file_names[printed_index].c_str(),
                file.flags,
                file.size,
                static_cast<unsigned long long>(file.chunks.size()),
            });
            ++printed_index;
        }

        out_response->present = 1;
        out_response->matched_count = matched_count;
        out_response->printed_count =
            static_cast<unsigned long long>(g_manifest_file_entries.size());
        out_response->total_count = total_count;
        out_response->files =
            g_manifest_file_entries.empty() ? nullptr : g_manifest_file_entries.data();
        return CAUTH_OK;
    } catch (const std::exception& exception) {
        set_last_error_detail(exception.what());
        return CAUTH_ERROR_INTERNAL;
    } catch (...) {
        set_last_error_detail("unexpected exception while listing manifest files");
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_download_chunk(const char* input_path,
                                          const char* depot_key_hex,
                                          const char* file_path,
                                          unsigned long long file_index,
                                          int has_file_index,
                                          unsigned long long chunk_index,
                                          int process_chunk,
                                          unsigned int max_count,
                                          const char* output_path) {
    clear_last_error_detail();
    if (output_path == nullptr || *output_path == '\0' || max_count == 0) {
        set_last_error_detail("output path is required and max_count must be > 0");
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        cauth::steam::depot::LoadedDepotManifest loaded_manifest;
        const auto load_result =
            load_manifest_for_file_operation(input_path, depot_key_hex, loaded_manifest);
        if (load_result != CAUTH_OK) {
            return load_result;
        }

        std::ostringstream err;
        const auto selected_file_index = cauth::steam::depot::resolve_file_selection(
            loaded_manifest,
            static_cast<std::size_t>(file_index),
            has_file_index != 0,
            nullable_string(file_path),
            err);
        if (!selected_file_index.has_value()) {
            set_last_error_detail(err.str());
            return CAUTH_ERROR_INVALID_ARGUMENT;
        }
        if (!cauth::steam::depot::validate_chunk_selection(
                loaded_manifest,
                *selected_file_index,
                static_cast<std::size_t>(chunk_index),
                err)) {
            set_last_error_detail(err.str());
            return CAUTH_ERROR_INVALID_ARGUMENT;
        }

        std::ostringstream out;
        const auto exit_code = cauth::steam::depot::download_chunk_from_manifest(
            loaded_manifest,
            *selected_file_index,
            static_cast<std::size_t>(chunk_index),
            process_chunk != 0,
            max_count,
            output_path,
            out,
            err);
        if (exit_code != 0) {
            set_last_error_detail(err.str().empty() ? out.str() : err.str());
            return CAUTH_ERROR_INTERNAL;
        }
        return CAUTH_OK;
    } catch (const std::exception& exception) {
        set_last_error_detail(exception.what());
        return CAUTH_ERROR_INTERNAL;
    } catch (...) {
        set_last_error_detail("unexpected exception while downloading depot chunk");
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_download_file(const char* input_path,
                                         const char* depot_key_hex,
                                         const char* file_path,
                                         unsigned long long file_index,
                                         int has_file_index,
                                         unsigned int max_count,
                                         const char* output_path) {
    clear_last_error_detail();
    if (output_path == nullptr || *output_path == '\0' || max_count == 0 ||
        depot_key_hex == nullptr || *depot_key_hex == '\0') {
        set_last_error_detail("input path, output path, max_count, and depot key are required");
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        cauth::steam::depot::LoadedDepotManifest loaded_manifest;
        const auto load_result =
            load_manifest_for_file_operation(input_path, depot_key_hex, loaded_manifest);
        if (load_result != CAUTH_OK) {
            return load_result;
        }

        std::ostringstream err;
        const auto selected_file_index = cauth::steam::depot::resolve_file_selection(
            loaded_manifest,
            static_cast<std::size_t>(file_index),
            has_file_index != 0,
            nullable_string(file_path),
            err);
        if (!selected_file_index.has_value()) {
            set_last_error_detail(err.str());
            return CAUTH_ERROR_INVALID_ARGUMENT;
        }

        std::ostringstream out;
        const auto exit_code = cauth::steam::depot::download_file_from_manifest(
            loaded_manifest,
            *selected_file_index,
            max_count,
            output_path,
            out,
            err);
        if (exit_code != 0) {
            set_last_error_detail(err.str().empty() ? out.str() : err.str());
            return CAUTH_ERROR_INTERNAL;
        }
        return CAUTH_OK;
    } catch (const std::exception& exception) {
        set_last_error_detail(exception.what());
        return CAUTH_ERROR_INTERNAL;
    } catch (...) {
        set_last_error_detail("unexpected exception while downloading depot file");
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_download_all_files(const char* input_path,
                                              const char* depot_key_hex,
                                              unsigned int max_count,
                                              const char* output_root) {
    clear_last_error_detail();
    if (output_root == nullptr || *output_root == '\0' || max_count == 0 ||
        depot_key_hex == nullptr || *depot_key_hex == '\0') {
        set_last_error_detail("input path, output root, max_count, and depot key are required");
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        cauth::steam::depot::LoadedDepotManifest loaded_manifest;
        const auto load_result =
            load_manifest_for_file_operation(input_path, depot_key_hex, loaded_manifest);
        if (load_result != CAUTH_OK) {
            return load_result;
        }

        std::ostringstream out;
        std::ostringstream err;
        const auto exit_code = cauth::steam::depot::download_all_files_from_manifest(
            loaded_manifest,
            max_count,
            output_root,
            out,
            err);
        if (exit_code != 0) {
            set_last_error_detail(err.str().empty() ? out.str() : err.str());
            return CAUTH_ERROR_INTERNAL;
        }
        return CAUTH_OK;
    } catch (const std::exception& exception) {
        set_last_error_detail(exception.what());
        return CAUTH_ERROR_INTERNAL;
    } catch (...) {
        set_last_error_detail("unexpected exception while downloading all manifest files");
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_depot_verify_local_files(const char* input_path,
                                              const char* depot_key_hex,
                                              const char* local_root,
                                              const char* filter_text,
                                              cauth_depot_local_verify_report_t* out_response) {
    clear_last_error_detail();
    if (out_response == nullptr || local_root == nullptr || *local_root == '\0') {
        set_last_error_detail("manifest input path, local root, and output struct are required");
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->clean = 0;
    out_response->checked_count = 0;
    out_response->ok_count = 0;
    out_response->missing_count = 0;
    out_response->mismatched_count = 0;
    out_response->size_only_count = 0;
    out_response->filtered_out_count = 0;
    out_response->total_count = 0;

    try {
        cauth::steam::depot::LoadedDepotManifest loaded_manifest;
        const auto load_result =
            load_manifest_for_file_operation(input_path, depot_key_hex, loaded_manifest);
        if (load_result != CAUTH_OK) {
            return load_result;
        }

        cauth::steam::depot::LocalVerifyReport verify_report;
        std::ostringstream out;
        std::ostringstream err;
        const auto exit_code = cauth::steam::depot::verify_local_files_against_manifest(
            loaded_manifest,
            local_root,
            nullable_string(filter_text),
            out,
            err,
            &verify_report);

        out_response->present = 1;
        out_response->clean = exit_code == 0 ? 1 : 0;
        out_response->checked_count = verify_report.checked_count;
        out_response->ok_count = verify_report.ok_count;
        out_response->missing_count = verify_report.missing_count;
        out_response->mismatched_count = verify_report.mismatched_count;
        out_response->size_only_count = verify_report.size_only_count;
        out_response->filtered_out_count = verify_report.filtered_out_count;
        out_response->total_count = verify_report.total_count;

        if (verify_report.fatal_error) {
            if (!err.str().empty()) {
                set_last_error_detail(err.str());
            } else {
                set_last_error_detail("local verify could not run");
            }
            return CAUTH_ERROR_INTERNAL;
        }

        if (!err.str().empty()) {
            set_last_error_detail(err.str());
        } else if (!out.str().empty()) {
            set_last_error_detail(out.str());
        } else {
            clear_last_error_detail();
        }
        return CAUTH_OK;
    } catch (const std::exception& exception) {
        set_last_error_detail(exception.what());
        return CAUTH_ERROR_INTERNAL;
    } catch (...) {
        set_last_error_detail("unexpected exception while verifying local depot files");
        return CAUTH_ERROR_INTERNAL;
    }
}

const char* cauth_depot_last_error_detail(void) {
    return g_last_error_detail.empty() ? nullptr : g_last_error_detail.c_str();
}

cauth_result_t cauth_depot_fetch_manifest_request_code(
    cauth_client_t* client,
    unsigned int app_id,
    unsigned int depot_id,
    unsigned long long manifest_gid,
    const char* branch,
    const char* branch_password_hash,
    unsigned int max_count,
    cauth_manifest_request_code_response_t* out_response) {
    if (client == nullptr || out_response == nullptr || app_id == 0 || depot_id == 0 ||
        manifest_gid == 0 || max_count == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->manifest_request_code = 0;

    try {
        cauth::steam::auth::StoredSteamAuthProvider auth_provider{*client->session_repository};
        cauth::core::depot::DepotCmClient depot_client{auth_provider};
        cauth::core::depot::ManifestRequestCodeRequest request;
        request.app_id = app_id;
        request.depot_id = depot_id;
        request.manifest_gid = manifest_gid;
        request.branch = branch == nullptr || *branch == '\0' ? "public" : branch;
        if (branch_password_hash != nullptr) {
            request.branch_password_hash = branch_password_hash;
        }

        const auto response = depot_client.fetch_manifest_request_code(request, max_count);
        if (!response.has_value()) {
            return CAUTH_OK;
        }

        out_response->present = 1;
        out_response->manifest_request_code = response->manifest_request_code;
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}
