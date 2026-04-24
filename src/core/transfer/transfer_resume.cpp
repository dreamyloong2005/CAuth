#include "core/transfer/transfer_resume.hpp"

#include "core/transfer/transfer_checkpoint.hpp"

namespace cauth::core::transfer {
namespace {

std::optional<std::string> find_resume_field(const TransferCheckpointState& checkpoint,
                                             std::string_view primary,
                                             std::string_view legacy) {
    if (const auto value = find_checkpoint_field(checkpoint, primary)) {
        return value;
    }
    if (!legacy.empty()) {
        return find_checkpoint_field(checkpoint, legacy);
    }
    return std::nullopt;
}

bool try_parse_u64(std::string_view value, std::uint64_t& result) {
    try {
        result = std::stoull(std::string{value});
        return true;
    } catch (...) {
        return false;
    }
}

bool try_parse_size(std::string_view value, std::size_t& result) {
    std::uint64_t parsed = 0;
    if (!try_parse_u64(value, parsed)) {
        return false;
    }
    result = static_cast<std::size_t>(parsed);
    return parsed == static_cast<std::uint64_t>(result);
}

} // namespace

bool load_transfer_resume_state(const std::filesystem::path& path,
                                TransferResumeState& state,
                                std::string& error_message) {
    TransferCheckpointState checkpoint;
    if (!load_transfer_checkpoint(path, checkpoint, error_message)) {
        return false;
    }

    state = {};
    state.token = checkpoint.token;
    state.committed_bytes = checkpoint.committed_bytes;

    if (const auto group_id = find_resume_field(checkpoint, "group_id", "batch_id")) {
        if (!try_parse_u64(*group_id, state.group_id)) {
            error_message = "Transfer resume group id is invalid: " + path.string();
            return false;
        }
    }
    if (const auto item_index = find_resume_field(checkpoint, "item_index", "file_index")) {
        if (!try_parse_size(*item_index, state.item_index)) {
            error_message = "Transfer resume item index is invalid: " + path.string();
            return false;
        }
    }
    if (const auto item_token = find_resume_field(checkpoint, "item_token", "file_token")) {
        state.item_token = *item_token;
    }

    return true;
}

bool save_transfer_resume_state(const std::filesystem::path& path,
                                const TransferResumeState& state,
                                std::string& error_message) {
    TransferCheckpointState checkpoint;
    checkpoint.token = state.token;
    checkpoint.committed_bytes = state.committed_bytes;
    if (state.group_id != 0) {
        set_checkpoint_field(checkpoint, "group_id", std::to_string(state.group_id));
    }
    if (state.item_index != 0) {
        set_checkpoint_field(checkpoint, "item_index", std::to_string(state.item_index));
    }
    if (!state.item_token.empty()) {
        set_checkpoint_field(checkpoint, "item_token", state.item_token);
    }
    return save_transfer_checkpoint(path, checkpoint, error_message);
}

bool clear_transfer_resume_state(const std::filesystem::path& path,
                                 std::string& error_message) {
    return clear_transfer_checkpoint(path, error_message);
}

} // namespace cauth::core::transfer
