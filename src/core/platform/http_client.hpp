#ifndef CAUTH_CORE_PLATFORM_HTTP_CLIENT_HPP
#define CAUTH_CORE_PLATFORM_HTTP_CLIENT_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::core::platform {

struct HttpHeader {
    std::string name;
    std::string value;
};

enum class HttpTransferDirection {
    Upload,
    Download,
};

struct HttpTransferProgress {
    HttpTransferDirection direction = HttpTransferDirection::Download;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t total_bytes = 0;
};

using HttpTransferProgressHook = void (*)(const HttpTransferProgress& progress, void* user_data);
using HttpTransferCancelHook = bool (*)(void* user_data);

struct HttpRequestCallbacks {
    HttpTransferProgressHook progress_hook = nullptr;
    HttpTransferCancelHook cancel_hook = nullptr;
    void* user_data = nullptr;
};

enum class HttpMethod {
    Get,
    Post,
    Put,
};

struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    std::string url;
    std::vector<std::uint8_t> body;
    std::string content_type;
    std::vector<HttpHeader> headers;
    std::int32_t connect_timeout_ms = 5000;
    std::int32_t read_timeout_ms = 10000;
    HttpRequestCallbacks callbacks;
};

struct HttpResponse {
    bool ok = false;
    std::string error_message;
    std::uint32_t status_code = 0;
    std::vector<std::uint8_t> body;
    std::vector<HttpHeader> headers;
};

HttpResponse perform_platform_http_request(const HttpRequest& request);
bool is_platform_http_client_available();
std::optional<std::string> http_body_as_string(const HttpResponse& response);
std::vector<std::string> http_header_values(const HttpResponse& response, std::string_view name);

} // namespace cauth::core::platform

#endif
