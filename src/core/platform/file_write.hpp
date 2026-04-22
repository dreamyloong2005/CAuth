#ifndef CAUTH_CORE_PLATFORM_FILE_WRITE_HPP
#define CAUTH_CORE_PLATFORM_FILE_WRITE_HPP

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cauth::core::platform {

enum class FileWriteMode {
    Overwrite = 0,
    SkipExisting = 1,
    FailIfExists = 2,
};

struct FileWriteOptions {
    FileWriteMode mode = FileWriteMode::Overwrite;
    bool atomic_write = true;
    std::string temp_suffix = ".cauthdownload";
};

class PreparedFileWrite {
  public:
    PreparedFileWrite() = default;
    PreparedFileWrite(PreparedFileWrite&& other) noexcept;
    PreparedFileWrite& operator=(PreparedFileWrite&& other) noexcept;
    PreparedFileWrite(const PreparedFileWrite&) = delete;
    PreparedFileWrite& operator=(const PreparedFileWrite&) = delete;
    ~PreparedFileWrite();

    [[nodiscard]] bool ok() const { return error_message_.empty(); }
    [[nodiscard]] bool skipped() const { return skipped_; }
    [[nodiscard]] const std::string& error_message() const { return error_message_; }
    [[nodiscard]] const std::filesystem::path& final_path() const { return final_path_; }
    [[nodiscard]] const std::filesystem::path& write_path() const { return write_path_; }

    bool open_binary_output(std::ofstream& output, std::string& error_message) const;
    bool write_all(const std::vector<std::uint8_t>& bytes, std::string& error_message);
    bool commit(std::string& error_message);

  private:
    friend PreparedFileWrite prepare_file_write(const std::filesystem::path& final_path,
                                                const FileWriteOptions& options);

    FileWriteOptions options_;
    std::filesystem::path final_path_;
    std::filesystem::path write_path_;
    std::string error_message_;
    bool skipped_ = false;
    bool committed_ = false;

    void cleanup() noexcept;
};

PreparedFileWrite prepare_file_write(const std::filesystem::path& final_path,
                                     const FileWriteOptions& options = {});

} // namespace cauth::core::platform

#endif
