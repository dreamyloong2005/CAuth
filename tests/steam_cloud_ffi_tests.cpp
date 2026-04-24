#include "cauth/core_ffi.h"
#include "cauth/steam_cloud_ffi.h"

#include <iostream>

int main() {
    constexpr unsigned long long kSteamId = 76561198000000000ULL;

    if (cauth_steam_cloud_pull(nullptr, nullptr, nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "steam cloud pull should reject null arguments\n";
        return 1;
    }

    if (cauth_steam_cloud_push(nullptr, nullptr, nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "steam cloud push should reject null arguments\n";
        return 1;
    }

    if (cauth_steam_cloud_verify_local_files(nullptr, nullptr, 0, nullptr) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "steam cloud verify should reject null arguments\n";
        return 1;
    }

    if (cauth_steam_cloud_list_remote_files_via_web_page(nullptr, nullptr, 0, 0, nullptr) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "steam cloud web-page list should reject null arguments\n";
        return 1;
    }

    if (cauth_steam_cloud_start_pull(nullptr, nullptr, nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "steam cloud start pull should reject null arguments\n";
        return 1;
    }

    if (cauth_steam_cloud_start_verify_local_files(nullptr, nullptr, 0, nullptr) !=
        CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "steam cloud start verify should reject null arguments\n";
        return 1;
    }

    cauth_client_t* client = nullptr;
    if (cauth_client_create(&client) != CAUTH_OK || client == nullptr) {
        std::cerr << "client creation failed\n";
        return 1;
    }

    cauth_steam_cloud_request_t request{};
    request.app_id = 440;
    request.steam_id = kSteamId;
    request.access_token = "token";
    request.local_root = "D:/saves";
    request.remote_root = "remote";
    request.dry_run = 1;
    request.delete_remote_orphans = 1;
    request.conflict_policy = CAUTH_STEAM_CLOUD_CONFLICT_NEWER_WINS;
    request.local_write_mode = CAUTH_FILE_WRITE_SKIP_EXISTING;
    request.atomic_write = 1;

    cauth_steam_cloud_request_t invalid_list_request{};
    invalid_list_request.app_id = 440;
    invalid_list_request.steam_id = kSteamId;
    cauth_steam_cloud_file_list_t list_result{};
    if (cauth_steam_cloud_list_remote_files(client, &invalid_list_request, 10, 0, 1, &list_result) !=
        CAUTH_OK) {
        std::cerr << "steam cloud list should complete at ABI level for missing token case\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (list_result.present != 1 || list_result.ok != 0 || list_result.message == nullptr) {
        std::cerr << "steam cloud list should return structured failure metadata\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_steam_cloud_file_list_t web_page_list_result{};
    if (cauth_steam_cloud_list_remote_files_via_web_page(
            client, &invalid_list_request, 10, 0, &web_page_list_result) != CAUTH_OK) {
        std::cerr << "steam cloud web-page list should complete at ABI level for missing web material\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (web_page_list_result.present != 1 || web_page_list_result.ok != 0 ||
        web_page_list_result.message == nullptr) {
        std::cerr << "steam cloud web-page list should return structured failure metadata\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_steam_cloud_result_t result{};
    cauth_steam_cloud_request_t invalid_pull_request{};
    invalid_pull_request.app_id = 440;
    invalid_pull_request.steam_id = kSteamId;
    invalid_pull_request.access_token = "token";
    if (cauth_steam_cloud_pull(client, &invalid_pull_request, &result) != CAUTH_OK) {
        std::cerr << "steam cloud pull should return structured validation failure\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (result.ok != 0 || result.message == nullptr) {
        std::cerr << "steam cloud pull should surface validation failure in result\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (cauth_steam_cloud_pull(client, &request, &result) != CAUTH_OK) {
        std::cerr << "steam cloud pull placeholder should succeed at ABI level\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (result.app_id != 440 || result.direction != CAUTH_STEAM_CLOUD_PULL || result.message == nullptr) {
        std::cerr << "steam cloud pull should fill result metadata\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (result.conflict_policy != CAUTH_STEAM_CLOUD_CONFLICT_NEWER_WINS) {
        std::cerr << "steam cloud pull should preserve conflict policy through FFI\n";
        cauth_client_destroy(client);
        return 1;
    }

    if (cauth_steam_cloud_push(client, &request, &result) != CAUTH_OK) {
        std::cerr << "steam cloud push should succeed at ABI level\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (result.app_id != 440 || result.direction != CAUTH_STEAM_CLOUD_PUSH || result.message == nullptr) {
        std::cerr << "steam cloud push should fill result metadata\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (result.conflict_policy != CAUTH_STEAM_CLOUD_CONFLICT_NEWER_WINS) {
        std::cerr << "steam cloud push should preserve conflict policy through FFI\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (result.resumable != 0 || result.resumed != 0 || result.resume_from_bytes != 0) {
        std::cerr << "steam cloud push should default resume metadata when no resumable upload is in play\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_steam_cloud_verify_report_t verify_report{};
    cauth_steam_cloud_request_t invalid_verify_request{};
    invalid_verify_request.app_id = 440;
    invalid_verify_request.steam_id = kSteamId;
    invalid_verify_request.access_token = "token";
    if (cauth_steam_cloud_verify_local_files(client, &invalid_verify_request, 1, &verify_report) !=
        CAUTH_OK) {
        std::cerr << "steam cloud verify should return structured validation failure\n";
        cauth_client_destroy(client);
        return 1;
    }
    if (verify_report.present != 1 || verify_report.clean != 0 || verify_report.message == nullptr) {
        std::cerr << "steam cloud verify should fill report metadata\n";
        cauth_client_destroy(client);
        return 1;
    }

    if (cauth_steam_cloud_poll_task(0, nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "steam cloud poll should reject null snapshot\n";
        cauth_client_destroy(client);
        return 1;
    }

    cauth_client_destroy(client);
    return 0;
}
