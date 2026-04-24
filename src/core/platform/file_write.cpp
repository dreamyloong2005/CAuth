#include "core/platform/file_write.hpp"
#include "core/transfer/transfer_checkpoint.hpp"
#include "core/transfer/transfer_resume.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace cauth::core::platform {
namespace {

std::filesystem::path::string_type to_native_suffix(std::string_view suffix) {
    return std::filesystem::path::string_type(suffix.begin(), suffix.end());
}

std::string format_filesystem_error(std::string_view message, const std::error_code& error) {
    std::string result{message};
    if (error) {
        result.append(": ");
        result.append(error.message());
    }
    return result;
}

std::filesystem::path make_atomic_temp_path(const std::filesystem::path& final_path,
                                            std::string_view suffix) {
    auto temp_name = final_path.filename().native();
    temp_name += to_native_suffix(suffix);
    return final_path.parent_path() / std::filesystem::path{temp_name};
}

std::uint64_t read_file_size_or_zero(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : size;
}

bool remove_if_exists(const std::filesystem::path& path,
                      std::string_view failure_message,
                      std::string& error_message) {
    if (path.empty()) {
        return true;
    }
    std::error_code ec;
    const auto exists = std::filesystem::exists(path, ec);
    if (ec) {
        error_message = format_filesystem_error(failure_message, ec);
        return false;
    }
    if (!exists) {
        return true;
    }
    std::filesystem::remove(path, ec);
    if (ec) {
        error_message = format_filesystem_error(failure_message, ec);
        return false;
    }
    return true;
}

bool write_resume_state_file(const std::filesystem::path& state_path,
                             const FileResumeState& state,
                             std::string& error_message) {
    return cauth::core::transfer::save_transfer_resume_state(
        state_path,
        cauth::core::transfer::TransferResumeState{
            state.token,
            state.committed_bytes,
            0,
            0,
            {},
        },
        error_message);
}

bool read_resume_state_file(const std::filesystem::path& state_path,
                            FileResumeState& state,
                            std::string& error_message) {
    cauth::core::transfer::TransferResumeState resume_state;
    if (!cauth::core::transfer::load_transfer_resume_state(
            state_path, resume_state, error_message)) {
        return false;
    }
    state.token = resume_state.token;
    state.committed_bytes = resume_state.committed_bytes;
    return true;
}

bool commit_atomic_replace(const std::filesystem::path& write_path,
                           const std::filesystem::path& final_path,
                           FileWriteMode mode,
                           std::string& error_message) {
    std::error_code ec;
    if (mode == FileWriteMode::SkipExisting || mode == FileWriteMode::FailIfExists) {
        const auto exists = std::filesystem::exists(final_path, ec);
        if (ec) {
            error_message = format_filesystem_error("Failed to inspect destination path", ec);
            return false;
        }
        if (exists) {
            error_message = "Destination file appeared while writing: " + final_path.string();
            return false;
        }
    }

#ifdef _WIN32
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (mode == FileWriteMode::Overwrite) {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    if (!MoveFileExW(write_path.c_str(), final_path.c_str(), flags)) {
        error_message = "Failed to commit file write to " + final_path.string();
        return false;
    }
    return true;
#else
    std::filesystem::rename(write_path, final_path, ec);
    if (!ec) {
        return true;
    }
    error_message = format_filesystem_error("Failed to commit file write", ec);
    return false;
#endif
}

} // namespace

PreparedFileWrite::PreparedFileWrite(PreparedFileWrite&& other) noexcept
    : options_(std::move(other.options_)),
      final_path_(std::move(other.final_path_)),
      write_path_(std::move(other.write_path_)),
      state_path_(std::move(other.state_path_)),
      error_message_(std::move(other.error_message_)),
      skipped_(other.skipped_),
      committed_(other.committed_),
      resume_available_(other.resume_available_),
      preserve_partial_(other.preserve_partial_),
      resume_offset_(other.resume_offset_) {
    other.skipped_ = false;
    other.committed_ = true;
    other.resume_available_ = false;
    other.preserve_partial_ = false;
    other.resume_offset_ = 0;
}

PreparedFileWrite& PreparedFileWrite::operator=(PreparedFileWrite&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    cleanup();
    options_ = std::move(other.options_);
    final_path_ = std::move(other.final_path_);
    write_path_ = std::move(other.write_path_);
    state_path_ = std::move(other.state_path_);
    error_message_ = std::move(other.error_message_);
    skipped_ = other.skipped_;
    committed_ = other.committed_;
    resume_available_ = other.resume_available_;
    preserve_partial_ = other.preserve_partial_;
    resume_offset_ = other.resume_offset_;
    other.skipped_ = false;
    other.committed_ = true;
    other.resume_available_ = false;
    other.preserve_partial_ = false;
    other.resume_offset_ = 0;
    return *this;
}

PreparedFileWrite::~PreparedFileWrite() { cleanup(); }

void PreparedFileWrite::cleanup() noexcept {
    if (committed_ || skipped_ || preserve_partial_) {
        return;
    }
    std::string ignored_error;
    if (options_.atomic_write) {
        (void)remove_if_exists(write_path_, "Failed to remove partial write path", ignored_error);
    }
    (void)remove_if_exists(state_path_, "Failed to remove resume state", ignored_error);
}

bool PreparedFileWrite::open_binary_output(std::ofstream& output, std::string& error_message) const {
    if (!ok()) {
        error_message = error_message_;
        return false;
    }
    if (skipped_) {
        error_message.clear();
        return false;
    }

    const auto open_mode = resume_available_ && resume_offset_ > 0
                               ? (std::ios::binary | std::ios::app)
                               : (std::ios::binary | std::ios::trunc);
    output.open(write_path_, open_mode);
    if (!output) {
        error_message = "Failed to open output path: " + write_path_.string();
        return false;
    }
    return true;
}

bool PreparedFileWrite::write_all(const std::vector<std::uint8_t>& bytes, std::string& error_message) {
    if (!ok()) {
        error_message = error_message_;
        return false;
    }
    if (skipped_) {
        error_message.clear();
        return true;
    }

    std::ofstream output;
    if (!open_binary_output(output, error_message)) {
        return false;
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        error_message = "Failed to write output path: " + write_path_.string();
        return false;
    }
    output.close();
    if (!output) {
        error_message = "Failed to finalize output path: " + write_path_.string();
        return false;
    }
    return commit(error_message);
}

bool PreparedFileWrite::commit(std::string& error_message) {
    if (!ok()) {
        error_message = error_message_;
        return false;
    }
    if (skipped_ || committed_) {
        return true;
    }
    if (!options_.atomic_write) {
        if (!clear_resume_state(error_message)) {
            return false;
        }
        committed_ = true;
        return true;
    }
    if (!commit_atomic_replace(write_path_, final_path_, options_.mode, error_message)) {
        return false;
    }
    if (!clear_resume_state(error_message)) {
        return false;
    }
    committed_ = true;
    return true;
}

bool PreparedFileWrite::save_resume_state(std::uint64_t committed_bytes, std::string& error_message) {
    if (!ok()) {
        error_message = error_message_;
        return false;
    }
    if (!options_.allow_resume || options_.resume_token.empty() || skipped_) {
        return true;
    }
    const FileResumeState state{options_.resume_token, committed_bytes};
    if (!write_resume_state_file(state_path_, state, error_message)) {
        return false;
    }
    resume_available_ = true;
    resume_offset_ = committed_bytes;
    return true;
}

bool PreparedFileWrite::clear_resume_state(std::string& error_message) {
    if (state_path_.empty()) {
        return true;
    }
    if (!remove_if_exists(state_path_, "Failed to clear resume state", error_message)) {
        return false;
    }
    resume_available_ = false;
    resume_offset_ = 0;
    return true;
}

bool PreparedFileWrite::discard_partial(std::string& error_message) {
    if (!clear_resume_state(error_message)) {
        return false;
    }
    if (write_path_.empty()) {
        return true;
    }
    if (!remove_if_exists(write_path_, "Failed to remove partial output", error_message)) {
        return false;
    }
    preserve_partial_ = false;
    return true;
}

PreparedFileWrite prepare_file_write(const std::filesystem::path& final_path,
                                     const FileWriteOptions& options) {
    PreparedFileWrite prepared;
    prepared.options_ = options;
    prepared.final_path_ = final_path;

    if (final_path.empty() || final_path.filename().empty()) {
        prepared.error_message_ = "Output path is required";
        return prepared;
    }

    const auto parent = final_path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec && !std::filesystem::exists(parent, ec)) {
            prepared.error_message_ =
                format_filesystem_error("Failed to create output directory", ec);
            return prepared;
        }
    }

    std::error_code exists_error;
    const auto final_exists = std::filesystem::exists(final_path, exists_error);
    if (exists_error) {
        prepared.error_message_ =
            format_filesystem_error("Failed to inspect output path", exists_error);
        return prepared;
    }

    if (options.atomic_write) {
        const auto suffix =
            options.temp_suffix.empty() ? std::string_view{".cauthdownload"}
                                        : std::string_view{options.temp_suffix};
        prepared.write_path_ = make_atomic_temp_path(final_path, suffix);
        prepared.state_path_ =
            cauth::core::transfer::append_checkpoint_suffix(prepared.write_path_, ".resume");
        if (prepared.write_path_ == final_path) {
            prepared.error_message_ = "Atomic write temp path must differ from output path";
            return prepared;
        }
    } else {
        prepared.write_path_ = final_path;
        prepared.state_path_ =
            cauth::core::transfer::append_checkpoint_suffix(prepared.write_path_, ".resume");
    }

    const auto write_exists = std::filesystem::exists(prepared.write_path_, exists_error);
    if (exists_error) {
        prepared.error_message_ =
            format_filesystem_error("Failed to inspect write path", exists_error);
        return prepared;
    }
    const auto state_exists = std::filesystem::exists(prepared.state_path_, exists_error);
    if (exists_error) {
        prepared.error_message_ =
            format_filesystem_error("Failed to inspect resume state path", exists_error);
        return prepared;
    }

    auto clear_stale_partial = [&](bool remove_write_path) -> bool {
        if (state_exists) {
            std::string clear_error;
            if (!remove_if_exists(prepared.state_path_, "Failed to clear stale resume state",
                                  clear_error)) {
                prepared.error_message_ = clear_error;
                return false;
            }
        }
        if (remove_write_path && write_exists) {
            std::string clear_error;
            if (!remove_if_exists(prepared.write_path_, "Failed to clear stale partial output",
                                  clear_error)) {
                prepared.error_message_ = clear_error;
                return false;
            }
        }
        return true;
    };

    if (options.allow_resume && !options.resume_token.empty() && state_exists && write_exists) {
        FileResumeState resume_state;
        std::string resume_error;
        if (read_resume_state_file(prepared.state_path_, resume_state, resume_error)) {
            const auto current_size = read_file_size_or_zero(prepared.write_path_);
            if (resume_state.token == options.resume_token &&
                current_size == resume_state.committed_bytes) {
                prepared.resume_available_ = true;
                prepared.resume_offset_ = resume_state.committed_bytes;
            } else {
                const auto remove_write = prepared.write_path_ != prepared.final_path_;
                if (!clear_stale_partial(remove_write)) {
                    return prepared;
                }
            }
        } else {
            const auto remove_write = prepared.write_path_ != prepared.final_path_;
            if (!clear_stale_partial(remove_write)) {
                return prepared;
            }
        }
    } else if (state_exists || (options.atomic_write && write_exists)) {
        if (!clear_stale_partial(prepared.write_path_ != prepared.final_path_)) {
            return prepared;
        }
    }

    if (!prepared.resume_available_ && final_exists) {
        switch (options.mode) {
        case FileWriteMode::SkipExisting:
            prepared.skipped_ = true;
            prepared.committed_ = true;
            return prepared;
        case FileWriteMode::FailIfExists:
            prepared.error_message_ = "Output path already exists: " + final_path.string();
            return prepared;
        case FileWriteMode::Overwrite:
        default:
            break;
        }
    }

    return prepared;
}

} // namespace cauth::core::platform
