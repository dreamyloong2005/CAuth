#include "core/platform/http_client.hpp"

#ifdef __ANDROID__
#include "core/runtime/android/bridge.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#ifdef _WIN32
#include <Windows.h>
#include <winhttp.h>
#endif

namespace cauth::core::platform {
namespace {

#ifdef _WIN32
struct WinHttpHandleDeleter {
    void operator()(HINTERNET handle) const noexcept {
        if (handle != nullptr) {
            WinHttpCloseHandle(handle);
        }
    }
};

using WinHttpHandle = std::unique_ptr<std::remove_pointer_t<HINTERNET>, WinHttpHandleDeleter>;

std::wstring widen_ascii(std::string_view value) {
    return std::wstring{value.begin(), value.end()};
}

std::string narrow_ascii(std::wstring_view value) {
    std::string narrowed;
    narrowed.reserve(value.size());
    for (const auto ch : value) {
        narrowed.push_back(static_cast<char>(ch & 0xff));
    }
    return narrowed;
}
#endif

#ifdef _WIN32
bool is_request_canceled(const HttpRequestCallbacks& callbacks) {
    return callbacks.cancel_hook != nullptr && callbacks.cancel_hook(callbacks.user_data);
}

void report_transfer_progress(const HttpRequestCallbacks& callbacks,
                              HttpTransferDirection direction,
                              std::uint64_t bytes_transferred,
                              std::uint64_t total_bytes) {
    if (callbacks.progress_hook == nullptr) {
        return;
    }
    callbacks.progress_hook(HttpTransferProgress{direction, bytes_transferred, total_bytes},
                            callbacks.user_data);
}
#endif

struct ParsedUrl {
    bool secure = false;
    std::string host;
    std::uint16_t port = 0;
    std::string path_and_query;
};

struct ResolvedRequestBody {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

std::optional<ResolvedRequestBody> resolve_request_body(const HttpRequest& request,
                                                        std::string& error_message) {
    const auto* body = request.body_view != nullptr ? request.body_view : &request.body;
    if (request.body_offset > body->size()) {
        error_message = "HTTP request body offset is out of range";
        return std::nullopt;
    }

    const auto remaining = body->size() - static_cast<std::size_t>(request.body_offset);
    const auto requested_length = request.body_length == 0
                                      ? remaining
                                      : static_cast<std::size_t>(request.body_length);
    if (requested_length > remaining) {
        error_message = "HTTP request body length is out of range";
        return std::nullopt;
    }

    ResolvedRequestBody resolved;
    resolved.size = requested_length;
    if (requested_length > 0) {
        resolved.data = body->data() + static_cast<std::ptrdiff_t>(request.body_offset);
    }
    return resolved;
}

#ifdef _WIN32
std::optional<ParsedUrl> parse_url(std::string_view url) {
    ParsedUrl parsed;
    std::string_view remainder;
    if (url.rfind("https://", 0) == 0) {
        parsed.secure = true;
        remainder = url.substr(8);
        parsed.port = 443;
    } else if (url.rfind("http://", 0) == 0) {
        remainder = url.substr(7);
        parsed.port = 80;
    } else {
        return std::nullopt;
    }

    const auto slash = remainder.find('/');
    const auto authority = slash == std::string_view::npos ? remainder : remainder.substr(0, slash);
    parsed.path_and_query = slash == std::string_view::npos ? "/" : std::string{remainder.substr(slash)};
    if (authority.empty()) {
        return std::nullopt;
    }

    const auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        parsed.host = std::string{authority.substr(0, colon)};
        unsigned long port = 0;
        for (const auto ch : authority.substr(colon + 1)) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return std::nullopt;
            }
            port = (port * 10UL) + static_cast<unsigned long>(ch - '0');
            if (port > 65535UL) {
                return std::nullopt;
            }
        }
        if (parsed.host.empty() || port == 0) {
            return std::nullopt;
        }
        parsed.port = static_cast<std::uint16_t>(port);
        return parsed;
    }

    parsed.host = std::string{authority};
    return parsed.host.empty() ? std::nullopt : std::optional<ParsedUrl>{parsed};
}
#endif

#ifdef _WIN32
std::vector<HttpHeader> parse_raw_headers(const std::wstring& raw_headers) {
    std::vector<HttpHeader> headers;
    std::size_t offset = 0;
    while (offset < raw_headers.size()) {
        auto line_end = raw_headers.find(L"\r\n", offset);
        if (line_end == std::wstring::npos) {
            line_end = raw_headers.size();
        }

        const std::wstring_view line{raw_headers.data() + offset, line_end - offset};
        offset = line_end + (line_end < raw_headers.size() ? 2U : 0U);
        if (line.empty() || line.rfind(L"HTTP/", 0) == 0) {
            continue;
        }

        const auto colon = line.find(L':');
        if (colon == std::wstring::npos) {
            continue;
        }

        auto value_start = colon + 1;
        while (value_start < line.size() &&
               ::iswspace(static_cast<wint_t>(line[value_start])) != 0) {
            ++value_start;
        }

        headers.push_back(
            {narrow_ascii(line.substr(0, colon)), narrow_ascii(line.substr(value_start))});
    }
    return headers;
}

bool header_name_equals_ascii_case_insensitive(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

std::uint64_t parse_header_u64(std::string_view value) {
    std::uint64_t parsed_value = 0;
    for (const auto ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return 0;
        }
        parsed_value = (parsed_value * 10U) + static_cast<std::uint64_t>(ch - '0');
    }
    return parsed_value;
}

std::uint64_t response_content_length(const std::vector<HttpHeader>& headers) {
    for (const auto& header : headers) {
        if (!header_name_equals_ascii_case_insensitive(header.name, "Content-Length")) {
            continue;
        }
        return parse_header_u64(header.value);
    }
    return 0;
}

std::uint64_t response_total_length(const HttpRequest& request,
                                    std::uint32_t status_code,
                                    const std::vector<HttpHeader>& headers) {
    const auto content_length = response_content_length(headers);
    if (request.use_range && request.range_start > 0 && status_code == 206 && content_length > 0) {
        return request.range_start + content_length;
    }
    return content_length;
}

HttpResponse winhttp_request(const HttpRequest& request) {
    const auto parsed = parse_url(request.url);
    if (!parsed.has_value()) {
        return {false, "invalid URL", 0, {}};
    }

    WinHttpHandle session{WinHttpOpen(L"CAuth/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) {
        return {false, "WinHttpOpen failed", 0, {}};
    }

    WinHttpHandle connection{
        WinHttpConnect(session.get(), widen_ascii(parsed->host).c_str(), parsed->port, 0)};
    if (!connection) {
        return {false, "WinHttpConnect failed", 0, {}};
    }

    const wchar_t* verb = L"GET";
    switch (request.method) {
    case HttpMethod::Post:
        verb = L"POST";
        break;
    case HttpMethod::Put:
        verb = L"PUT";
        break;
    case HttpMethod::Get:
    default:
        verb = L"GET";
        break;
    }
    WinHttpHandle http_request{WinHttpOpenRequest(
        connection.get(), verb, widen_ascii(parsed->path_and_query).c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parsed->secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!http_request) {
        return {false, "WinHttpOpenRequest failed", 0, {}};
    }

    const auto connect_timeout = request.connect_timeout_ms <= 0 ? 0 : request.connect_timeout_ms;
    const auto read_timeout = request.read_timeout_ms <= 0 ? 0 : request.read_timeout_ms;
    WinHttpSetTimeouts(http_request.get(), connect_timeout, connect_timeout, read_timeout,
                       read_timeout);

    std::wstring headers;
    if (!request.content_type.empty()) {
        headers = widen_ascii("Content-Type: " + request.content_type + "\r\n");
    }
    if (request.use_range) {
        headers += widen_ascii("Range: bytes=" + std::to_string(request.range_start) + "-\r\n");
    }
    for (const auto& header : request.headers) {
        headers += widen_ascii(header.name);
        headers += L": ";
        headers += widen_ascii(header.value);
        headers += L"\r\n";
    }

    std::string body_error;
    const auto resolved_body = resolve_request_body(request, body_error);
    if (!resolved_body.has_value()) {
        return {false, body_error, 0, {}, {}};
    }
    if (resolved_body->size >
        static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())) {
        return {false, "HTTP request body is too large", 0, {}, {}};
    }

    if (is_request_canceled(request.callbacks)) {
        return {false, "operation canceled", 0, {}, {}};
    }

    const auto body_size = static_cast<DWORD>(resolved_body->size);
    if (WinHttpSendRequest(http_request.get(),
                           headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                           headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
                           WINHTTP_NO_REQUEST_DATA,
                           0,
                           body_size,
                           0) == FALSE) {
        return {false, "WinHTTP request failed", 0, {}, {}};
    }

    if (resolved_body->size > 0) {
        constexpr std::size_t kUploadChunkSize = 64U * 1024U;
        report_transfer_progress(
            request.callbacks, HttpTransferDirection::Upload, 0, resolved_body->size);

        std::size_t offset = 0;
        while (offset < resolved_body->size) {
            if (is_request_canceled(request.callbacks)) {
                return {false, "operation canceled", 0, {}, {}};
            }

            const auto remaining = resolved_body->size - offset;
            const auto requested =
                static_cast<DWORD>((std::min<std::size_t>)(remaining, kUploadChunkSize));
            DWORD written = 0;
            if (WinHttpWriteData(http_request.get(),
                                 resolved_body->data + static_cast<std::ptrdiff_t>(offset),
                                 requested,
                                 &written) == FALSE) {
                return {false, "WinHttpWriteData failed", 0, {}, {}};
            }
            if (written == 0 && requested != 0) {
                return {false, "WinHttpWriteData wrote zero bytes", 0, {}, {}};
            }

            offset += static_cast<std::size_t>(written);
            report_transfer_progress(request.callbacks,
                                     HttpTransferDirection::Upload,
                                     offset,
                                     resolved_body->size);
        }
    }

    if (is_request_canceled(request.callbacks)) {
        return {false, "operation canceled", 0, {}, {}};
    }

    if (WinHttpReceiveResponse(http_request.get(), nullptr) == FALSE) {
        return {false, "WinHTTP request failed", 0, {}, {}};
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    if (WinHttpQueryHeaders(http_request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size,
                            WINHTTP_NO_HEADER_INDEX) == FALSE) {
        return {false, "WinHttpQueryHeaders failed", 0, {}, {}};
    }

    DWORD raw_headers_size = 0;
    (void)WinHttpQueryHeaders(http_request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                              WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                              &raw_headers_size, WINHTTP_NO_HEADER_INDEX);
    std::vector<wchar_t> raw_headers_buffer(raw_headers_size / sizeof(wchar_t), L'\0');
    if (raw_headers_size > 0 &&
        WinHttpQueryHeaders(http_request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, raw_headers_buffer.data(),
                            &raw_headers_size, WINHTTP_NO_HEADER_INDEX) == FALSE) {
        return {false, "WinHttpQueryHeaders raw headers failed", status_code, {}, {}};
    }
    const std::wstring raw_headers =
        raw_headers_buffer.empty() ? std::wstring{} : std::wstring{raw_headers_buffer.data()};
    auto response_headers = parse_raw_headers(raw_headers);
    const auto total_length = response_total_length(request, status_code, response_headers);

    if (request.use_range && request.range_start > 0 && status_code != 206) {
        return {false,
                "HTTP range request was not honored",
                status_code,
                {},
                std::move(response_headers)};
    }

    std::vector<std::uint8_t> body;
    report_transfer_progress(request.callbacks,
                             HttpTransferDirection::Download,
                             request.use_range ? request.range_start : 0,
                             total_length);
    std::uint64_t streamed_bytes = request.use_range ? request.range_start : 0;
    while (true) {
        if (is_request_canceled(request.callbacks)) {
            return {false, "operation canceled", status_code, {}, response_headers};
        }
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(http_request.get(), &available) == FALSE) {
            return {false, "WinHttpQueryDataAvailable failed", status_code, {}, response_headers};
        }
        if (available == 0) {
            break;
        }

        const auto offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (WinHttpReadData(http_request.get(), body.data() + offset, available, &read) == FALSE) {
            return {false, "WinHttpReadData failed", status_code, {}, response_headers};
        }
        if (request.response_write_hook != nullptr && read > 0) {
            if (!request.response_write_hook(body.data() + offset,
                                             static_cast<std::size_t>(read),
                                             request.response_write_user_data)) {
                return {false,
                        "HTTP response sink rejected data",
                        status_code,
                        {},
                        response_headers};
            }
            streamed_bytes += read;
            body.resize(offset);
            report_transfer_progress(request.callbacks,
                                     HttpTransferDirection::Download,
                                     streamed_bytes,
                                     total_length);
            continue;
        }
        body.resize(offset + read);
        report_transfer_progress(request.callbacks,
                                 HttpTransferDirection::Download,
                                 (request.use_range ? request.range_start : 0) + body.size(),
                                 total_length);
    }

    if (status_code < 200 || status_code >= 300) {
        return {false,
                "HTTP " + std::to_string(status_code),
                status_code,
                std::move(body),
                std::move(response_headers)};
    }
    return {true, "", status_code, std::move(body), std::move(response_headers)};
}
#endif

} // namespace

HttpResponse perform_platform_http_request(const HttpRequest& request) {
    if (request.url.empty()) {
        return {false, "URL is required", 0, {}, {}};
    }
    if (request.callbacks.cancel_hook != nullptr && request.callbacks.cancel_hook(request.callbacks.user_data)) {
        return {false, "operation canceled", 0, {}, {}};
    }

#ifdef _WIN32
    return winhttp_request(request);
#elif defined(__ANDROID__)
    auto request_copy = request;
    if (request_copy.body_view != nullptr) {
        std::string body_error;
        const auto resolved_body = resolve_request_body(request_copy, body_error);
        if (!resolved_body.has_value()) {
            return {false, body_error, 0, {}, {}};
        }
        if (resolved_body->size == 0) {
            request_copy.body.clear();
        } else {
            request_copy.body.assign(resolved_body->data,
                                     resolved_body->data + resolved_body->size);
        }
        request_copy.body_view = nullptr;
        request_copy.body_offset = 0;
        request_copy.body_length = 0;
    } else if (request_copy.body_offset != 0 || request_copy.body_length != 0) {
        std::string body_error;
        const auto resolved_body = resolve_request_body(request_copy, body_error);
        if (!resolved_body.has_value()) {
            return {false, body_error, 0, {}, {}};
        }
        if (resolved_body->size == 0) {
            request_copy.body.clear();
        } else {
            request_copy.body.assign(resolved_body->data,
                                     resolved_body->data + resolved_body->size);
        }
        request_copy.body_offset = 0;
        request_copy.body_length = 0;
    }
    if (request_copy.use_range) {
        request_copy.headers.push_back(
            {"Range", "bytes=" + std::to_string(request_copy.range_start) + "-"});
    }
    const auto response = cauth::core::runtime::android_bridge_http_request(
        request.method == HttpMethod::Post
            ? "POST"
            : request.method == HttpMethod::Put ? "PUT" : "GET",
        request_copy.url,
        request_copy.body,
        request_copy.content_type,
        request_copy.headers,
        request_copy.connect_timeout_ms,
        request_copy.read_timeout_ms);
    if (!response.ok && response.error_message.find("operation canceled") != std::string::npos) {
        return {false, "operation canceled", response.status_code, {}, {}};
    }
    if (request_copy.use_range && request_copy.range_start > 0 && response.ok &&
        response.status_code != 206) {
        return {false, "HTTP range request was not honored", response.status_code, {}, {}};
    }
    if (request_copy.response_write_hook != nullptr && response.ok && !response.body.empty()) {
        if (!request_copy.response_write_hook(response.body.data(),
                                              response.body.size(),
                                              request_copy.response_write_user_data)) {
            return {false, "HTTP response sink rejected data", response.status_code, {}, {}};
        }
        return {response.ok, response.error_message, response.status_code, {}, {}};
    }
    return {response.ok,
            response.error_message,
            response.status_code,
            std::move(response.body),
            {}};
#else
    (void)request;
    return {false, "Platform HTTP client is not implemented on this platform yet", 0, {}, {}};
#endif
}

bool is_platform_http_client_available() {
#ifdef _WIN32
    return true;
#elif defined(__ANDROID__)
    return cauth::core::runtime::is_android_platform_bridge_available();
#else
    return false;
#endif
}

std::optional<std::string> http_body_as_string(const HttpResponse& response) {
    if (!response.ok) {
        return std::nullopt;
    }
    return std::string{response.body.begin(), response.body.end()};
}

std::vector<std::string> http_header_values(const HttpResponse& response, std::string_view name) {
    std::vector<std::string> values;
    for (const auto& header : response.headers) {
        if (header.name.size() != name.size()) {
            continue;
        }

        bool equal = true;
        for (std::size_t index = 0; index < name.size(); ++index) {
            if (std::tolower(static_cast<unsigned char>(header.name[index])) !=
                std::tolower(static_cast<unsigned char>(name[index]))) {
                equal = false;
                break;
            }
        }

        if (equal) {
            values.push_back(header.value);
        }
    }
    return values;
}

} // namespace cauth::core::platform
