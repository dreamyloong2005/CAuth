#include "cauth/core_ffi.h"

#include <filesystem>
#include <cstring>
#include <iostream>

int main() {
    const auto version = cauth_get_version();
    if (version.major != 0 || version.minor != 5 || version.patch != 1) {
        std::cerr << "unexpected version tuple\n";
        return 1;
    }

    if (std::strcmp(version.text, "0.5.1") != 0) {
        std::cerr << "unexpected version text\n";
        return 1;
    }

    cauth_client_t* client = nullptr;
    if (cauth_client_create(&client) != CAUTH_OK || client == nullptr) {
        std::cerr << "client creation failed\n";
        return 1;
    }

    cauth_client_destroy(client);

    if (cauth_client_create(nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "null client output should be rejected\n";
        return 1;
    }

    cauth_client_options_t memory_options{};
    memory_options.session_storage_kind = CAUTH_SESSION_STORAGE_MEMORY;
    if (cauth_client_create_with_options(&memory_options, &client) != CAUTH_OK || client == nullptr) {
        std::cerr << "memory client creation failed\n";
        return 1;
    }
    cauth_client_destroy(client);

    const auto custom_store_path =
        (std::filesystem::temp_directory_path() / "cauth-session-options-test.dpapi").string();
    cauth_client_options_t file_options{};
    file_options.session_storage_kind = CAUTH_SESSION_STORAGE_FILE_PATH;
    file_options.session_storage_path = custom_store_path.c_str();
    if (cauth_client_create_with_options(&file_options, &client) != CAUTH_OK || client == nullptr) {
        std::cerr << "file-path client creation failed\n";
        return 1;
    }
    cauth_client_destroy(client);

    if (cauth_client_create_with_options(nullptr, nullptr) != CAUTH_ERROR_INVALID_ARGUMENT) {
        std::cerr << "null options plus null client output should be rejected\n";
        return 1;
    }

    return 0;
}
