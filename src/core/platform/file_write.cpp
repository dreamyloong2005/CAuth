#include "core/platform/file_write.hpp"

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
      error_message_(std::move(other.error_message_)),
      skipped_(other.skipped_),
      committed_(other.committed_) {
    other.skipped_ = false;
    other.committed_ = true;
}

PreparedFileWrite& PreparedFileWrite::operator=(PreparedFileWrite&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    cleanup();
    options_ = std::move(other.options_);
    final_path_ = std::move(other.final_path_);
    write_path_ = std::move(other.write_path_);
    error_message_ = std::move(other.error_message_);
    skipped_ = other.skipped_;
    committed_ = other.committed_;
    other.skipped_ = false;
    other.committed_ = true;
    return *this;
}

PreparedFileWrite::~PreparedFileWrite() { cleanup(); }

void PreparedFileWrite::cleanup() noexcept {
    if (committed_ || skipped_ || !options_.atomic_write || write_path_.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(write_path_, ec);
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

    output.open(write_path_, std::ios::binary | std::ios::trunc);
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
        committed_ = true;
        return true;
    }
    if (!commit_atomic_replace(write_path_, final_path_, options_.mode, error_message)) {
        return false;
    }
    committed_ = true;
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
    if (final_exists) {
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

    if (options.atomic_write) {
        const auto suffix =
            options.temp_suffix.empty() ? std::string_view{".cauthdownload"}
                                        : std::string_view{options.temp_suffix};
        prepared.write_path_ = make_atomic_temp_path(final_path, suffix);
        if (prepared.write_path_ == final_path) {
            prepared.error_message_ = "Atomic write temp path must differ from output path";
            return prepared;
        }
        std::error_code remove_error;
        const auto temp_exists = std::filesystem::exists(prepared.write_path_, remove_error);
        if (remove_error) {
            prepared.error_message_ =
                format_filesystem_error("Failed to inspect atomic temp path", remove_error);
            return prepared;
        }
        if (temp_exists) {
            std::filesystem::remove(prepared.write_path_, remove_error);
            if (remove_error) {
                prepared.error_message_ =
                    format_filesystem_error("Failed to clear stale atomic temp path", remove_error);
                return prepared;
            }
        }
    } else {
        prepared.write_path_ = final_path;
    }

    return prepared;
}

} // namespace cauth::core::platform
