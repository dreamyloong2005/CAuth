#ifndef CAUTH_CORE_TRANSFER_TRANSFER_RESUME_HPP
#define CAUTH_CORE_TRANSFER_TRANSFER_RESUME_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace cauth::core::transfer {

struct TransferResumeState {
    std::string token;
    std::uint64_t committed_bytes = 0;
    std::uint64_t group_id = 0;
    std::size_t item_index = 0;
    std::string item_token;
};

bool load_transfer_resume_state(const std::filesystem::path& path,
                                TransferResumeState& state,
                                std::string& error_message);
bool save_transfer_resume_state(const std::filesystem::path& path,
                                const TransferResumeState& state,
                                std::string& error_message);
bool clear_transfer_resume_state(const std::filesystem::path& path,
                                 std::string& error_message);

} // namespace cauth::core::transfer

#endif
