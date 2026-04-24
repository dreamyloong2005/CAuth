#include "core/transfer/transfer_checkpoint.hpp"
#include "core/transfer/transfer_resume.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int run() {
    const auto root = std::filesystem::temp_directory_path() / "cauth-transfer-checkpoint-tests";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const auto checkpoint_path = root / "upload.resume";
    cauth::core::transfer::TransferCheckpointState state;
    state.token = "token-1";
    state.committed_bytes = 42;
    cauth::core::transfer::set_checkpoint_field(state, "batch_id", "77");
    cauth::core::transfer::set_checkpoint_field(state, "file_index", "3");

    std::string error_message;
    if (!cauth::core::transfer::save_transfer_checkpoint(checkpoint_path, state, error_message)) {
        std::cerr << "save_transfer_checkpoint failed: " << error_message << "\n";
        return 1;
    }

    cauth::core::transfer::TransferCheckpointState loaded;
    if (!cauth::core::transfer::load_transfer_checkpoint(checkpoint_path, loaded, error_message)) {
        std::cerr << "load_transfer_checkpoint failed: " << error_message << "\n";
        return 1;
    }

    if (loaded.token != "token-1" || loaded.committed_bytes != 42 ||
        cauth::core::transfer::find_checkpoint_field(loaded, "batch_id").value_or("") != "77" ||
        cauth::core::transfer::find_checkpoint_field(loaded, "file_index").value_or("") != "3") {
        std::cerr << "checkpoint values did not round-trip\n";
        return 1;
    }

    cauth::core::transfer::set_checkpoint_field(loaded, "file_index", "4");
    cauth::core::transfer::erase_checkpoint_field(loaded, "batch_id");
    if (!cauth::core::transfer::save_transfer_checkpoint(checkpoint_path, loaded, error_message)) {
        std::cerr << "save_transfer_checkpoint(update) failed: " << error_message << "\n";
        return 1;
    }

    cauth::core::transfer::TransferCheckpointState updated;
    if (!cauth::core::transfer::load_transfer_checkpoint(checkpoint_path, updated, error_message)) {
        std::cerr << "load_transfer_checkpoint(update) failed: " << error_message << "\n";
        return 1;
    }
    if (cauth::core::transfer::find_checkpoint_field(updated, "batch_id").has_value() ||
        cauth::core::transfer::find_checkpoint_field(updated, "file_index").value_or("") != "4") {
        std::cerr << "checkpoint field update did not persist\n";
        return 1;
    }

    if (!cauth::core::transfer::clear_transfer_checkpoint(checkpoint_path, error_message)) {
        std::cerr << "clear_transfer_checkpoint failed: " << error_message << "\n";
        return 1;
    }
    if (std::filesystem::exists(checkpoint_path)) {
        std::cerr << "checkpoint path still exists after clear\n";
        return 1;
    }

    const auto resume_path = root / "transfer.resume";
    cauth::core::transfer::TransferResumeState resume_state;
    resume_state.token = "resume-token";
    resume_state.committed_bytes = 128;
    resume_state.group_id = 55;
    resume_state.item_index = 2;
    resume_state.item_token = "file-2";
    if (!cauth::core::transfer::save_transfer_resume_state(
            resume_path, resume_state, error_message)) {
        std::cerr << "save_transfer_resume_state failed: " << error_message << "\n";
        return 1;
    }

    cauth::core::transfer::TransferResumeState loaded_resume;
    if (!cauth::core::transfer::load_transfer_resume_state(
            resume_path, loaded_resume, error_message)) {
        std::cerr << "load_transfer_resume_state failed: " << error_message << "\n";
        return 1;
    }
    if (loaded_resume.token != resume_state.token ||
        loaded_resume.committed_bytes != resume_state.committed_bytes ||
        loaded_resume.group_id != resume_state.group_id ||
        loaded_resume.item_index != resume_state.item_index ||
        loaded_resume.item_token != resume_state.item_token) {
        std::cerr << "transfer resume values did not round-trip\n";
        return 1;
    }

    const auto legacy_resume_path = root / "legacy.resume";
    std::ofstream legacy_out(legacy_resume_path, std::ios::binary | std::ios::trunc);
    if (!legacy_out) {
        std::cerr << "failed to open legacy resume checkpoint for writing\n";
        return 1;
    }
    legacy_out << "cauth-transfer-v1\n";
    legacy_out << "token=legacy-token\n";
    legacy_out << "bytes=256\n";
    legacy_out << "field:batch_id=77\n";
    legacy_out << "field:file_index=3\n";
    legacy_out << "field:file_token=file-3\n";
    legacy_out.close();
    if (!legacy_out) {
        std::cerr << "failed to finalize legacy resume checkpoint\n";
        return 1;
    }

    cauth::core::transfer::TransferResumeState legacy_resume;
    if (!cauth::core::transfer::load_transfer_resume_state(
            legacy_resume_path, legacy_resume, error_message)) {
        std::cerr << "load_transfer_resume_state(legacy) failed: " << error_message << "\n";
        return 1;
    }
    if (legacy_resume.token != "legacy-token" || legacy_resume.committed_bytes != 256 ||
        legacy_resume.group_id != 77 || legacy_resume.item_index != 3 ||
        legacy_resume.item_token != "file-3") {
        std::cerr << "legacy transfer resume aliases were not loaded\n";
        return 1;
    }

    return 0;
}

} // namespace

int main() { return run(); }
