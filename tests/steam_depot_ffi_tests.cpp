#include "cauth/core_ffi.h"
#include "cauth/steam_depot_ffi.h"

#include <iostream>

int main() {
    constexpr unsigned long long kSteamId = 76561198000000000ULL;

    if (cauth_depot_fetch_branches(nullptr, kSteamId, 440, 5, nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot branches should reject null arguments\n";
        return 1;
    }

    if (cauth_depot_fetch_manifests(nullptr, kSteamId, 440, "public", 5, nullptr) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot manifests should reject null arguments\n";
        return 1;
    }

    if (cauth_depot_fetch_preflight(nullptr, kSteamId, 440, "public", 5, nullptr) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot preflight should reject null arguments\n";
        return 1;
    }

    if (cauth_depot_download_manifest(
            441, 123, 456, 5, nullptr, CAUTH_FILE_WRITE_OVERWRITE, 0) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "manifest download should reject null output path\n";
        return 1;
    }

    if (cauth_depot_start_manifest_download(
            441, 123, 456, 5, "manifest.bin", CAUTH_FILE_WRITE_OVERWRITE, 1, nullptr) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "manifest start download should reject null handle output\n";
        return 1;
    }

    if (cauth_depot_fetch_key(nullptr, kSteamId, 440, 441, 5, nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot key should reject null arguments\n";
        return 1;
    }

    if (cauth_depot_fetch_manifest_request_code(
            nullptr, kSteamId, 440, 441, 123, "public", "", 5, nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "manifest request code should reject null arguments\n";
        return 1;
    }

    if (cauth_depot_load_manifest_info(nullptr, nullptr, nullptr) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "manifest info should reject null arguments\n";
        return 1;
    }

    if (cauth_depot_list_manifest_files(nullptr, nullptr, nullptr, 0, nullptr) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "manifest file list should reject null arguments\n";
        return 1;
    }

    if (cauth_depot_verify_local_files(nullptr, nullptr, nullptr, nullptr, nullptr) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "verify local files should reject null arguments\n";
        return 1;
    }

    cauth_client_t* client = nullptr;
    if (cauth_client_create(&client) != CAUTH_OK || client == nullptr) {
        std::cerr << "client creation failed\n";
        return 1;
    }

    cauth_depot_key_response_t key_response{};
    if (cauth_depot_fetch_key(client, kSteamId, 0, 441, 5, &key_response) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot key should reject zero app id\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (cauth_depot_fetch_key(client, 0, 440, 441, 5, &key_response) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot key should reject zero steam id\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_app_branch_list_t branch_list{};
    if (cauth_depot_fetch_branches(client, kSteamId, 0, 5, &branch_list) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot branches should reject zero app id\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_depot_manifest_list_t manifest_list{};
    if (cauth_depot_fetch_manifests(client, kSteamId, 0, "public", 5, &manifest_list) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot manifests should reject zero app id\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_depot_preflight_report_t preflight_report{};
    if (cauth_depot_fetch_preflight(client, kSteamId, 0, "public", 5, &preflight_report) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot preflight should reject zero app id\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_manifest_request_code_response_t request_code_response{};
    if (cauth_depot_fetch_manifest_request_code(
            client, kSteamId, 440, 441, 0, "public", "", 5, &request_code_response) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "manifest request code should reject zero manifest gid\n";
        cauth_client_destroy(client);
        return 1;
    }

    if (cauth_depot_download_manifest(
            0, 123, 456, 5, "manifest.bin", CAUTH_FILE_WRITE_OVERWRITE, 0) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "manifest download should reject zero depot id\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_manifest_info_t manifest_info{};
    if (cauth_depot_load_manifest_info("", "", &manifest_info) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "manifest info should reject empty input path\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_manifest_file_list_t file_list{};
    if (cauth_depot_list_manifest_files("", "", "", 10, &file_list) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "manifest file list should reject empty input path\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_depot_local_verify_report_t verify_report{};
    if (cauth_depot_verify_local_files("", "", "", "", &verify_report) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "verify local files should reject empty inputs\n";
        cauth_client_destroy(client);
        return 1;
    }

    unsigned long long task_handle = 0;
    if (cauth_depot_start_verify_local_files("", "", "", "", &task_handle) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "verify local task should reject empty inputs\n";
        cauth_client_destroy(client);
        return 1;
    }

    if (cauth_depot_poll_task(0, nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "depot task poll should reject null snapshot\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_client_destroy(client);
    return 0;
}
