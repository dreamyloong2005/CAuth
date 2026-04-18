#ifndef CAUTH_CORE_PLATFORM_RSA_PUBLIC_KEY_HPP
#define CAUTH_CORE_PLATFORM_RSA_PUBLIC_KEY_HPP

#include <string>

namespace cauth::core::platform {

struct RsaPublicKey {
    std::string modulus_hex;
    std::string exponent_hex;
};

} // namespace cauth::core::platform

#endif
