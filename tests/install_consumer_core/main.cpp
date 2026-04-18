#include <cauth/core_ffi.h>

int main() {
    const cauth_version_t version = cauth_get_version();
    return version.major == 0 && version.minor == 0 && version.patch == 0 ? 1 : 0;
}
