#include "cauth/core_ffi.h"

#include <cstring>
#include <iostream>

int main() {
    const auto version = cauth_get_version();
    if (version.major != 0 || version.minor != 1 || version.patch != 0) {
        std::cerr << "unexpected version tuple\n";
        return 1;
    }

    if (std::strcmp(version.text, "0.1.0") != 0) {
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

    return 0;
}
