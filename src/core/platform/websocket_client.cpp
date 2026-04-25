#include "core/platform/websocket_client.hpp"
#include "core/runtime/android/bridge.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#include <Windows.h>
#include <winhttp.h>
#endif

namespace cauth::core::platform {
namespace {

bool is_request_canceled(const WebSocketRequest& request) {
    return request.cancel_context.cancel_hook != nullptr &&
           request.cancel_context.cancel_hook(request.cancel_context.user_data);
}

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

class WinHttpWebSocketConnection final : public WebSocketConnection {
  public:
    explicit WinHttpWebSocketConnection(HINTERNET websocket,
                                        std::int32_t receive_timeout_ms,
                                        OperationCancelContext cancel_context)
        : websocket_(websocket),
          receive_timeout_ms_(receive_timeout_ms),
          cancel_context_(cancel_context) {}

    WebSocketProbeResult send_binary(const std::vector<std::uint8_t>& bytes) override {
        if (!websocket_) {
            return {false, "websocket is closed"};
        }
        if (cancel_context_.cancel_hook != nullptr &&
            cancel_context_.cancel_hook(cancel_context_.user_data)) {
            return {false, "operation canceled"};
        }

        if (WinHttpWebSocketSend(websocket_.get(), WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                 const_cast<std::uint8_t*>(bytes.data()),
                                 static_cast<DWORD>(bytes.size())) != NO_ERROR) {
            return {false, "WinHttpWebSocketSend failed"};
        }

        return {true, ""};
    }

    WebSocketReceiveResult receive() override {
        if (!websocket_) {
            return {false, "websocket is closed", {}};
        }

        std::vector<std::uint8_t> bytes;
        std::array<std::uint8_t, 8192> buffer{};

        while (true) {
            DWORD bytes_read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type{};
            const auto raw_websocket = websocket_.get();
            std::mutex receive_mutex;
            std::condition_variable receive_cv;
            bool receive_done = false;
            bool receive_timed_out = false;
            bool receive_canceled = false;
            std::thread watchdog{[&]() {
                std::unique_lock<std::mutex> lock{receive_mutex};
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds{(std::max)(receive_timeout_ms_, 1)};
                while (!receive_done) {
                    if (cancel_context_.cancel_hook != nullptr &&
                        cancel_context_.cancel_hook(cancel_context_.user_data)) {
                        receive_canceled = true;
                        WinHttpCloseHandle(raw_websocket);
                        return;
                    }
                    if (receive_cv.wait_until(
                            lock,
                            (std::min)(deadline,
                                       std::chrono::steady_clock::now() +
                                           std::chrono::milliseconds{50}),
                            [&]() { return receive_done; })) {
                        return;
                    }
                    if (std::chrono::steady_clock::now() >= deadline) {
                        receive_timed_out = true;
                        WinHttpCloseHandle(raw_websocket);
                        return;
                    }
                }
            }};

            const auto error = WinHttpWebSocketReceive(raw_websocket, buffer.data(),
                                                       static_cast<DWORD>(buffer.size()),
                                                       &bytes_read, &buffer_type);
            {
                std::lock_guard<std::mutex> lock{receive_mutex};
                receive_done = true;
            }
            receive_cv.notify_one();
            watchdog.join();

            if (receive_canceled) {
                websocket_.release();
                return {false, "operation canceled", {}};
            }
            if (receive_timed_out) {
                websocket_.release();
                return {false, "WinHttpWebSocketReceive timed out", {}};
            }

            if (error != NO_ERROR) {
                if (error == ERROR_WINHTTP_TIMEOUT) {
                    return {false, "WinHttpWebSocketReceive timed out", {}};
                }
                return {false, "WinHttpWebSocketReceive failed", {}};
            }

            bytes.insert(bytes.end(), buffer.begin(),
                         buffer.begin() + static_cast<std::ptrdiff_t>(bytes_read));

            if (buffer_type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                return {true, "", std::move(bytes)};
            }

            if (buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                return {false, "websocket closed by server", {}};
            }
        }
    }

    void close() override {
        if (websocket_) {
            WinHttpWebSocketClose(websocket_.get(), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                                  WINHTTP_NO_REQUEST_DATA, 0);
            websocket_.reset();
        }
    }

  private:
    WinHttpHandle websocket_;
    std::int32_t receive_timeout_ms_ = 10000;
    OperationCancelContext cancel_context_{};
};
#endif

#ifdef __ANDROID__
class AndroidWebSocketConnection final : public WebSocketConnection {
  public:
    explicit AndroidWebSocketConnection(std::int64_t handle,
                                        std::int32_t receive_timeout_ms,
                                        OperationCancelContext cancel_context)
        : handle_(handle),
          receive_timeout_ms_(receive_timeout_ms),
          cancel_context_(cancel_context) {}

    WebSocketProbeResult send_binary(const std::vector<std::uint8_t>& bytes) override {
        if (handle_ == 0) {
            return {false, "websocket is closed"};
        }
        if (cancel_context_.cancel_hook != nullptr &&
            cancel_context_.cancel_hook(cancel_context_.user_data)) {
            return {false, "operation canceled"};
        }

        const auto result = cauth::core::runtime::android_bridge_websocket_send(handle_, bytes);
        return {result.ok, result.error_message};
    }

    WebSocketReceiveResult receive() override {
        if (handle_ == 0) {
            return {false, "websocket is closed", {}};
        }
        if (cancel_context_.cancel_hook != nullptr &&
            cancel_context_.cancel_hook(cancel_context_.user_data)) {
            close();
            return {false, "operation canceled", {}};
        }

        const auto result = cauth::core::runtime::android_bridge_websocket_receive(
            handle_, receive_timeout_ms_);
        return {result.ok, result.error_message, std::move(result.bytes)};
    }

    void close() override {
        if (handle_ == 0) {
            return;
        }

        cauth::core::runtime::android_bridge_websocket_close(handle_);
        handle_ = 0;
    }

    ~AndroidWebSocketConnection() override {
        close();
    }

  private:
    std::int64_t handle_ = 0;
    std::int32_t receive_timeout_ms_ = 10000;
    OperationCancelContext cancel_context_{};
};
#endif

#ifdef _WIN32
template <typename Fn>
bool run_winhttp_call_with_cancel(HINTERNET handle,
                                  const WebSocketRequest& request,
                                  Fn&& fn,
                                  std::string& error_message) {
    std::mutex request_mutex;
    std::condition_variable request_cv;
    bool request_done = false;
    bool request_canceled = false;
    std::thread watchdog{[&]() {
        std::unique_lock<std::mutex> lock{request_mutex};
        while (!request_done) {
            if (request.cancel_context.cancel_hook != nullptr &&
                request.cancel_context.cancel_hook(request.cancel_context.user_data)) {
                request_canceled = true;
                WinHttpCloseHandle(handle);
                return;
            }
            request_cv.wait_for(lock, std::chrono::milliseconds{50}, [&]() { return request_done; });
        }
    }};

    const auto ok = fn();
    {
        std::lock_guard<std::mutex> lock{request_mutex};
        request_done = true;
    }
    request_cv.notify_one();
    watchdog.join();

    if (request_canceled) {
        error_message = "operation canceled";
        return false;
    }
    return ok;
}
#endif

} // namespace

std::string build_websocket_url(const WebSocketRequest& request) {
    return std::string{request.secure ? "wss://" : "ws://"} + request.host + ":" +
           std::to_string(request.port) + request.path;
}

bool is_valid_websocket_request(const WebSocketRequest& request) {
    return !request.host.empty() && request.port != 0 && !request.path.empty() &&
           request.path.front() == '/';
}

std::pair<WebSocketProbeResult, std::unique_ptr<WebSocketConnection>>
WebSocketClient::connect(const WebSocketRequest& request) const {
    if (!is_valid_websocket_request(request)) {
        return std::make_pair(WebSocketProbeResult{false, "invalid websocket request"},
                              std::unique_ptr<WebSocketConnection>{});
    }
    if (is_request_canceled(request)) {
        return std::make_pair(WebSocketProbeResult{false, "operation canceled"},
                              std::unique_ptr<WebSocketConnection>{});
    }

#ifdef _WIN32
    WinHttpHandle session{WinHttpOpen(L"CAuth/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) {
        return std::make_pair(WebSocketProbeResult{false, "WinHttpOpen failed"},
                              std::unique_ptr<WebSocketConnection>{});
    }

    WinHttpHandle connection{
        WinHttpConnect(session.get(), widen_ascii(request.host).c_str(), request.port, 0)};
    if (!connection) {
        return std::make_pair(WebSocketProbeResult{false, "WinHttpConnect failed"},
                              std::unique_ptr<WebSocketConnection>{});
    }

    const auto path = widen_ascii(request.path);
    WinHttpHandle http_request{WinHttpOpenRequest(
        connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, request.secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!http_request) {
        return std::make_pair(WebSocketProbeResult{false, "WinHttpOpenRequest failed"},
                              std::unique_ptr<WebSocketConnection>{});
    }

    WinHttpSetTimeouts(http_request.get(), (std::max)(request.connect_timeout_ms, 1),
                       (std::max)(request.connect_timeout_ms, 1),
                       (std::max)(request.receive_timeout_ms, 1),
                       (std::max)(request.receive_timeout_ms, 1));

    if (WinHttpSetOption(http_request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ==
        FALSE) {
        return std::make_pair(WebSocketProbeResult{
                                  false, "WinHttpSetOption websocket upgrade failed"},
                              std::unique_ptr<WebSocketConnection>{});
    }

    std::string request_error;
    const auto upgraded = run_winhttp_call_with_cancel(
        http_request.get(),
        request,
        [&]() {
            return WinHttpSendRequest(http_request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
                   WinHttpReceiveResponse(http_request.get(), nullptr) != FALSE;
        },
        request_error);
    if (!upgraded) {
        if (request_error == "operation canceled") {
            http_request.release();
            return std::make_pair(WebSocketProbeResult{false, "operation canceled"},
                                  std::unique_ptr<WebSocketConnection>{});
        }
        return std::make_pair(WebSocketProbeResult{
                                  false, "WinHTTP websocket upgrade request failed"},
                              std::unique_ptr<WebSocketConnection>{});
    }

    HINTERNET raw_websocket = WinHttpWebSocketCompleteUpgrade(http_request.get(), 0);
    if (raw_websocket == nullptr) {
        return std::make_pair(WebSocketProbeResult{
                                  false, "WinHttpWebSocketCompleteUpgrade failed"},
                              std::unique_ptr<WebSocketConnection>{});
    }

    DWORD receive_timeout = static_cast<DWORD>((std::max)(request.receive_timeout_ms, 1));
    WinHttpSetOption(raw_websocket, WINHTTP_OPTION_RECEIVE_TIMEOUT, &receive_timeout,
                     sizeof(receive_timeout));

    std::unique_ptr<WebSocketConnection> websocket =
        std::make_unique<WinHttpWebSocketConnection>(
            raw_websocket, request.receive_timeout_ms, request.cancel_context);
    return std::make_pair(WebSocketProbeResult{true, ""}, std::move(websocket));
#else
    if (cauth::core::runtime::is_android_platform_bridge_available()) {
        if (is_request_canceled(request)) {
            return std::make_pair(WebSocketProbeResult{false, "operation canceled"},
                                  std::unique_ptr<WebSocketConnection>{});
        }
        const auto connect_result = cauth::core::runtime::android_bridge_websocket_connect(
            build_websocket_url(request), request.connect_timeout_ms);
        if (!connect_result.ok) {
            return std::make_pair(WebSocketProbeResult{false, connect_result.error_message},
                                  std::unique_ptr<WebSocketConnection>{});
        }

        std::unique_ptr<WebSocketConnection> websocket =
            std::make_unique<AndroidWebSocketConnection>(
                connect_result.handle, request.receive_timeout_ms, request.cancel_context);
        return std::make_pair(WebSocketProbeResult{true, ""}, std::move(websocket));
    }
    return std::make_pair(
        WebSocketProbeResult{false, "websocket transport is not implemented on this platform yet"},
        std::unique_ptr<WebSocketConnection>{});
#endif
}

WebSocketProbeResult WebSocketClient::probe(const WebSocketRequest& request) const {
    auto [result, connection] = connect(request);
    if (connection) {
        connection->close();
    }

    return result;
}

} // namespace cauth::core::platform
