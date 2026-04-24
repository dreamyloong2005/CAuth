#include "core/transfer/transfer_checkpoint.hpp"

#include <algorithm>
#include <fstream>

namespace cauth::core::transfer {
namespace {

std::filesystem::path::string_type to_native_suffix(std::string_view suffix) {
    return std::filesystem::path::string_type(suffix.begin(), suffix.end());
}

bool remove_if_exists(const std::filesystem::path& path, std::string& error_message) {
    std::error_code ec;
    const auto exists = std::filesystem::exists(path, ec);
    if (ec) {
        error_message = "Failed to inspect checkpoint path: " + ec.message();
        return false;
    }
    if (!exists) {
        return true;
    }
    std::filesystem::remove(path, ec);
    if (ec) {
        error_message = "Failed to remove checkpoint path: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

std::filesystem::path append_checkpoint_suffix(const std::filesystem::path& path,
                                               std::string_view suffix) {
    auto filename = path.filename().native();
    filename += to_native_suffix(suffix);
    return path.parent_path() / std::filesystem::path{filename};
}

bool load_transfer_checkpoint(const std::filesystem::path& path,
                              TransferCheckpointState& state,
                              std::string& error_message) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error_message = "Failed to open checkpoint: " + path.string();
        return false;
    }

    state = {};
    std::string line;
    if (!std::getline(input, line)) {
        error_message = "Checkpoint is truncated: " + path.string();
        return false;
    }
    if (line != "cauth-transfer-v1") {
        error_message = "Checkpoint version is unsupported: " + path.string();
        return false;
    }

    while (std::getline(input, line)) {
        if (line.rfind("token=", 0) == 0) {
            state.token = line.substr(6);
            continue;
        }
        if (line.rfind("bytes=", 0) == 0) {
            try {
                state.committed_bytes = std::stoull(line.substr(6));
            } catch (...) {
                error_message = "Checkpoint byte count is invalid: " + path.string();
                return false;
            }
            continue;
        }
        if (line.rfind("field:", 0) == 0) {
            const auto separator = line.find('=', 6);
            if (separator == std::string::npos) {
                error_message = "Checkpoint field is invalid: " + path.string();
                return false;
            }
            state.fields.emplace_back(line.substr(6, separator - 6), line.substr(separator + 1));
            continue;
        }
        if (!line.empty()) {
            error_message = "Checkpoint contains an unknown line: " + path.string();
            return false;
        }
    }

    return true;
}

bool save_transfer_checkpoint(const std::filesystem::path& path,
                              const TransferCheckpointState& state,
                              std::string& error_message) {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec && !std::filesystem::exists(parent, ec)) {
            error_message = "Failed to create checkpoint directory: " + parent.string();
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error_message = "Failed to open checkpoint for writing: " + path.string();
        return false;
    }

    output << "cauth-transfer-v1\n";
    output << "token=" << state.token << "\n";
    output << "bytes=" << state.committed_bytes << "\n";
    for (const auto& [key, value] : state.fields) {
        output << "field:" << key << "=" << value << "\n";
    }
    output.close();
    if (!output) {
        error_message = "Failed to finalize checkpoint: " + path.string();
        return false;
    }
    return true;
}

bool clear_transfer_checkpoint(const std::filesystem::path& path,
                               std::string& error_message) {
    return remove_if_exists(path, error_message);
}

std::optional<std::string> find_checkpoint_field(const TransferCheckpointState& state,
                                                 std::string_view key) {
    for (const auto& [entry_key, entry_value] : state.fields) {
        if (entry_key == key) {
            return entry_value;
        }
    }
    return std::nullopt;
}

void set_checkpoint_field(TransferCheckpointState& state,
                          std::string key,
                          std::string value) {
    for (auto& [entry_key, entry_value] : state.fields) {
        if (entry_key == key) {
            entry_value = std::move(value);
            return;
        }
    }
    state.fields.emplace_back(std::move(key), std::move(value));
}

void erase_checkpoint_field(TransferCheckpointState& state, std::string_view key) {
    state.fields.erase(
        std::remove_if(
            state.fields.begin(),
            state.fields.end(),
            [&](const auto& entry) { return entry.first == key; }),
        state.fields.end());
}

} // namespace cauth::core::transfer
