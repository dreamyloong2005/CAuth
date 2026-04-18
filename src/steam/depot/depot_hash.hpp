#ifndef CAUTH_CORE_DEPOT_DEPOT_HASH_HPP
#define CAUTH_CORE_DEPOT_DEPOT_HASH_HPP

#include "core/hash/sha1.hpp"

namespace cauth::core::depot {

using cauth::core::hash::Sha1Hasher;
using cauth::core::hash::hash_bytes_to_hex;
using cauth::core::hash::sha1_digest;
using cauth::core::hash::sha1_matches;
using cauth::core::hash::sha1_to_hex;

} // namespace cauth::core::depot

#endif
