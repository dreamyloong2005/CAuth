#include "core/runtime/windows/windows_session_repository.hpp"

#ifdef _WIN32

#include "core/session/auth_session_codec.hpp"

#include <Windows.h>
#include <dpapi.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cauth::core::runtime {
namespace {

std::filesystem::path default_session_path() {
    char* app_data = nullptr;
    std::size_t app_data_length = 0;
    if (_dupenv_s(&app_data, &app_data_length, "APPDATA") != 0 || app_data == nullptr ||
        app_data_length == 0) {
        throw std::runtime_error("APPDATA is not set");
    }

    std::unique_ptr<char, decltype(&std::free)> app_data_guard{app_data, std::free};
    return std::filesystem::path{app_data_guard.get()} / "CAuth" / "auth_session.dpapi";
}

DATA_BLOB make_blob(std::vector<std::uint8_t>& bytes) {
    DATA_BLOB blob{};
    blob.pbData = bytes.data();
    blob.cbData = static_cast<DWORD>(bytes.size());
    return blob;
}

std::vector<std::uint8_t> blob_to_vector(const DATA_BLOB& blob) {
    return std::vector<std::uint8_t>{blob.pbData, blob.pbData + blob.cbData};
}

void write_binary_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());

    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        throw std::runtime_error("failed to open credential file for writing");
    }

    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

std::optional<std::vector<std::uint8_t>> read_binary_file(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

std::vector<std::uint8_t> protect_bytes(std::vector<std::uint8_t> plain) {
    DATA_BLOB input = make_blob(plain);
    DATA_BLOB output{};
    if (CryptProtectData(&input, L"CAuth auth session", nullptr, nullptr, nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN, &output) == FALSE) {
        throw std::runtime_error("CryptProtectData failed");
    }

    const auto encrypted = blob_to_vector(output);
    LocalFree(output.pbData);
    return encrypted;
}

std::optional<std::vector<std::uint8_t>> unprotect_bytes(std::vector<std::uint8_t> encrypted) {
    DATA_BLOB input = make_blob(encrypted);
    DATA_BLOB output{};
    if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                           &output) == FALSE) {
        return std::nullopt;
    }

    auto plain = blob_to_vector(output);
    LocalFree(output.pbData);
    return plain;
}

} // namespace

WindowsSessionRepository::WindowsSessionRepository() : WindowsSessionRepository(default_session_path()) {}

WindowsSessionRepository::WindowsSessionRepository(std::filesystem::path path)
    : path_(std::move(path)) {}

void WindowsSessionRepository::save_auth_session(const session::AuthSession& session) {
    auto state = load_repository_state();
    session::upsert_auth_session(state, session);
    save_repository_state(state);
}

std::vector<session::AuthSession> WindowsSessionRepository::list_auth_sessions() const {
    return load_repository_state().sessions;
}

std::optional<session::AuthSession> WindowsSessionRepository::load_auth_session(
    std::string_view provider,
    std::string_view subject_id) const {
    return session::find_auth_session(
        load_repository_state(),
        session::AuthSessionKey{std::string{provider}, std::string{subject_id}});
}

void WindowsSessionRepository::clear_auth_session(std::string_view provider,
                                                  std::string_view subject_id) {
    auto state = load_repository_state();
    session::remove_auth_session(
        state,
        session::AuthSessionKey{std::string{provider}, std::string{subject_id}});
    save_repository_state(state);
}

void WindowsSessionRepository::clear_all_auth_sessions() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
}

session::AuthSessionRepositoryState WindowsSessionRepository::load_repository_state() const {
    auto encrypted = read_binary_file(path_);
    if (!encrypted.has_value()) {
        return {};
    }

    auto plain = unprotect_bytes(std::move(*encrypted));
    if (!plain.has_value()) {
        return {};
    }

    auto state = session::decode_auth_session_repository_state(*plain);
    if (!state.has_value()) {
        return {};
    }
    return *state;
}

void WindowsSessionRepository::save_repository_state(
    const session::AuthSessionRepositoryState& state) {
    if (state.sessions.empty()) {
        clear_all_auth_sessions();
        return;
    }

    auto plain = session::encode_auth_session_repository_state(state);
    if (plain.empty()) {
        throw std::runtime_error("failed to encode auth session");
    }
    const auto encrypted = protect_bytes(std::move(plain));
    write_binary_file(path_, encrypted);
}

const std::filesystem::path& WindowsSessionRepository::path() const noexcept {
    return path_;
}

} // namespace cauth::core::runtime

#else

#include <stdexcept>
#include <utility>

namespace cauth::core::runtime {

WindowsSessionRepository::WindowsSessionRepository() = default;
WindowsSessionRepository::WindowsSessionRepository(std::filesystem::path path)
    : path_(std::move(path)) {}

void WindowsSessionRepository::save_auth_session(const session::AuthSession&) {
    throw std::runtime_error("WindowsSessionRepository is only available on Windows");
}

std::vector<session::AuthSession> WindowsSessionRepository::list_auth_sessions() const {
    return {};
}

std::optional<session::AuthSession> WindowsSessionRepository::load_auth_session(
    std::string_view,
    std::string_view) const {
    return std::nullopt;
}

void WindowsSessionRepository::clear_auth_session(std::string_view, std::string_view) {}

void WindowsSessionRepository::clear_all_auth_sessions() {}

const std::filesystem::path& WindowsSessionRepository::path() const noexcept {
    return path_;
}

} // namespace cauth::core::runtime

#endif
