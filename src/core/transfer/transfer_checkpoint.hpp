#ifndef CAUTH_CORE_TRANSFER_TRANSFER_CHECKPOINT_HPP
#define CAUTH_CORE_TRANSFER_TRANSFER_CHECKPOINT_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cauth::core::transfer {

struct TransferCheckpointState {
    std::string token;
    std::uint64_t committed_bytes = 0;
    std::vector<std::pair<std::string, std::string>> fields;
};

std::filesystem::path append_checkpoint_suffix(const std::filesystem::path& path,
                                               std::string_view suffix);

bool load_transfer_checkpoint(const std::filesystem::path& path,
                              TransferCheckpointState& state,
                              std::string& error_message);
bool save_transfer_checkpoint(const std::filesystem::path& path,
                              const TransferCheckpointState& state,
                              std::string& error_message);
bool clear_transfer_checkpoint(const std::filesystem::path& path,
                               std::string& error_message);

std::optional<std::string> find_checkpoint_field(const TransferCheckpointState& state,
                                                 std::string_view key);
void set_checkpoint_field(TransferCheckpointState& state,
                          std::string key,
                          std::string value);
void erase_checkpoint_field(TransferCheckpointState& state, std::string_view key);

} // namespace cauth::core::transfer

#endif
