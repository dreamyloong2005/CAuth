#ifndef CAUTH_CORE_DEPOT_DEPOT_CHUNK_HPP
#define CAUTH_CORE_DEPOT_DEPOT_CHUNK_HPP

#include "steam/depot/depot_manifest.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cauth::core::depot {

struct DepotChunkProcessResult {
    bool ok = false;
    std::string error_message;
    std::vector<std::uint8_t> bytes;
};

std::uint32_t adler32_zero_seed(const std::vector<std::uint8_t>& bytes);

DepotChunkProcessResult process_depot_chunk(const DepotManifestChunk& chunk,
                                            const std::vector<std::uint8_t>& raw_bytes,
                                            const std::vector<std::uint8_t>& depot_key);

} // namespace cauth::core::depot

#endif
