#include "steam/cloud/steam_cloud_upload_service.hpp"

#include "core/hash/sha1.hpp"
#include "steam/auth/steam_web_api_auth_transport.hpp"

#include <charconv>
#include <cctype>
#include <initializer_list>
#include <sstream>
#include <string_view>

namespace cauth::steam::cloud {
namespace {

constexpr std::string_view kSteamApiBaseUrl = "https://api.steampowered.com";
constexpr std::string_view kSteamCommunityBaseUrl = "https://steamcommunity.com";
constexpr std::string_view kSteamStoreBaseUrl = "https://store.steampowered.com";

struct ServiceCallResult {
    bool ok = false;
    std::string message;
    std::string body;
};

struct BeginBatchResponse {
    std::uint64_t batch_id = 0;
};

struct BeginHttpUploadResponse {
    std::string url;
    std::vector<cauth::core::platform::HttpHeader> headers;
};

struct HttpUploadProgressContext {
    std::string_view filename;
    const SteamCloudUploadCallbacks* callbacks = nullptr;
};

std::string json_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const auto ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string build_json_string_array(const std::vector<std::string>& values) {
    std::ostringstream json;
    json << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            json << ',';
        }
        json << '"' << json_escape(values[index]) << '"';
    }
    json << ']';
    return json.str();
}

std::string build_service_form_body(const SteamCloudWebAuthContext& auth,
                                    std::string_view input_json) {
    auto body = std::string{"input_json="} +
                cauth::steam::auth::steam_web_api_url_encode(input_json);
    if (!auth.access_token.empty()) {
        body.insert(
            0, "access_token=" +
                   cauth::steam::auth::steam_web_api_url_encode(auth.access_token) + "&");
    }
    return body;
}

bool route_selection_applies_to_any_role(
    const cauth::core::platform::RouteSelection& selection,
    std::initializer_list<std::string_view> roles) {
    if (selection.empty() || selection.role.empty()) {
        return true;
    }
    for (const auto role : roles) {
        if (selection.role == role) {
            return true;
        }
    }
    return false;
}

std::string rewrite_url_for_route_selection(
    std::string_view url,
    const cauth::core::platform::RouteSelection& selection,
    std::initializer_list<std::string_view> roles) {
    if (selection.empty() || !route_selection_applies_to_any_role(selection, roles)) {
        return std::string{url};
    }

    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::string{url};
    }
    const auto authority_begin = scheme_end + 3;
    auto authority_end = url.find('/', authority_begin);
    if (authority_end == std::string_view::npos) {
        authority_end = url.size();
    }
    if (authority_end <= authority_begin) {
        return std::string{url};
    }

    const auto protocol = selection.protocol.empty()
                              ? std::string{url.substr(0, scheme_end)}
                              : selection.protocol;
    const auto endpoint =
        selection.endpoint.empty()
            ? std::string{url.substr(authority_begin, authority_end - authority_begin)}
            : selection.endpoint;
    return protocol + "://" + endpoint + std::string{url.substr(authority_end)};
}

ServiceCallResult post_service_method(std::string_view method_path,
                                      const SteamCloudWebAuthContext& auth,
                                      std::string_view input_json) {
    const auto use_access_token = !auth.access_token.empty();
    const auto has_cookie_session =
        !use_access_token &&
        (!auth.web_cookie_header.empty() || !auth.store_cookie_header.empty());
    const auto use_store_cookie =
        !use_access_token && auth.web_cookie_header.empty() && !auth.store_cookie_header.empty();

    cauth::core::platform::HttpRequest request;
    request.method = cauth::core::platform::HttpMethod::Post;
    const auto base_url = rewrite_url_for_route_selection(
        use_access_token ? std::string{kSteamApiBaseUrl}
                         : (has_cookie_session ? std::string{use_store_cookie ? kSteamStoreBaseUrl
                                                                               : kSteamCommunityBaseUrl}
                                               : std::string{kSteamApiBaseUrl}),
        auth.route_selection,
        {"control", "upload-control"});
    request.url = base_url + std::string{method_path};
    request.content_type = "application/x-www-form-urlencoded";
    const auto body = build_service_form_body(auth, input_json);
    request.body.assign(body.begin(), body.end());
    if (has_cookie_session) {
        const auto& cookie_header = use_store_cookie ? auth.store_cookie_header : auth.web_cookie_header;
        request.headers.push_back({"Cookie", cookie_header});
        request.headers.push_back({"Origin", base_url});
        request.headers.push_back({"Referer", base_url + "/"});
    }

    const auto response = cauth::core::platform::perform_platform_http_request(request);
    if (!response.ok) {
        return {false, response.error_message, {}};
    }

    const auto decoded = cauth::core::platform::http_body_as_string(response);
    return {true, decoded.value_or(""), decoded.value_or("")};
}

std::optional<std::uint64_t> find_json_u64(std::string_view json, std::string_view key) {
    const auto needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    ++position;
    while (position < json.size() &&
           (std::isspace(static_cast<unsigned char>(json[position])) != 0 ||
            json[position] == '"')) {
        ++position;
    }
    const auto start = position;
    while (position < json.size() &&
           std::isdigit(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
    if (position == start) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(json.data() + start, json.data() + position, value);
    if (parsed.ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> find_json_bool(std::string_view json, std::string_view key) {
    const auto needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    ++position;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
    if (json.substr(position, 4) == "true" || json.substr(position, 3) == "\"1") {
        return true;
    }
    if (json.substr(position, 5) == "false" || json.substr(position, 3) == "\"0") {
        return false;
    }
    if (position < json.size() && json[position] == '1') {
        return true;
    }
    if (position < json.size() && json[position] == '0') {
        return false;
    }
    return std::nullopt;
}

std::optional<std::string> find_json_string(std::string_view json, std::string_view key) {
    const auto needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position = json.find('"', position + 1);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    const auto start = position + 1;
    auto end = start;
    while (end < json.size()) {
        if (json[end] == '\\') {
            end += 2;
            continue;
        }
        if (json[end] == '"') {
            return std::string{json.substr(start, end - start)};
        }
        ++end;
    }
    return std::nullopt;
}

std::size_t skip_json_string(std::string_view json, std::size_t offset) {
    for (std::size_t index = offset + 1; index < json.size(); ++index) {
        if (json[index] == '\\') {
            ++index;
            continue;
        }
        if (json[index] == '"') {
            return index;
        }
    }
    return std::string_view::npos;
}

std::size_t find_matching_brace(std::string_view json, std::size_t start,
                                char open_char, char close_char) {
    std::size_t depth = 0;
    for (std::size_t index = start; index < json.size(); ++index) {
        if (json[index] == '"') {
            index = skip_json_string(json, index);
            if (index == std::string_view::npos) {
                return std::string_view::npos;
            }
            continue;
        }
        if (json[index] == open_char) {
            ++depth;
        } else if (json[index] == close_char) {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return std::string_view::npos;
}

std::vector<std::string_view> find_object_array(std::string_view json, std::string_view key) {
    std::vector<std::string_view> objects;
    const auto key_position = json.find("\"" + std::string{key} + "\"");
    if (key_position == std::string_view::npos) {
        return objects;
    }
    const auto array_start = json.find('[', key_position);
    if (array_start == std::string_view::npos) {
        return objects;
    }
    const auto array_end = find_matching_brace(json, array_start, '[', ']');
    if (array_end == std::string_view::npos) {
        return objects;
    }
    std::size_t cursor = array_start + 1;
    while (cursor < array_end) {
        const auto object_start = json.find('{', cursor);
        if (object_start == std::string_view::npos || object_start >= array_end) {
            break;
        }
        const auto object_end = find_matching_brace(json, object_start, '{', '}');
        if (object_end == std::string_view::npos || object_end > array_end) {
            break;
        }
        objects.push_back(json.substr(object_start, object_end - object_start + 1));
        cursor = object_end + 1;
    }
    return objects;
}

std::optional<BeginBatchResponse> parse_begin_batch_response(std::string_view json) {
    const auto batch_id = find_json_u64(json, "batch_id");
    if (!batch_id.has_value()) {
        return std::nullopt;
    }
    return BeginBatchResponse{*batch_id};
}

std::optional<BeginHttpUploadResponse> parse_begin_http_upload_response(std::string_view json) {
    const auto url_host = find_json_string(json, "url_host");
    const auto url_path = find_json_string(json, "url_path");
    if (!url_host.has_value() || !url_path.has_value()) {
        return std::nullopt;
    }

    BeginHttpUploadResponse response;
    const auto use_https = find_json_bool(json, "use_https").value_or(true);
    response.url = std::string{use_https ? "https://" : "http://"} + *url_host + *url_path;
    for (const auto object : find_object_array(json, "request_headers")) {
        const auto name = find_json_string(object, "name");
        const auto value = find_json_string(object, "value");
        if (name.has_value() && value.has_value()) {
            response.headers.push_back({*name, *value});
        }
    }
    return response;
}

bool parse_commit_http_upload_response(std::string_view json) {
    return find_json_bool(json, "file_committed").value_or(false);
}

ServiceCallResult begin_app_upload_batch(const SteamCloudWebAuthContext& auth,
                                         std::uint32_t app_id,
                                         std::string_view machine_name,
                                         const std::vector<std::string>& files_to_upload,
                                         const std::vector<std::string>& files_to_delete,
                                         BeginBatchResponse& out_response) {
    const auto json = std::string{"{\"appid\":\""} + std::to_string(app_id) +
                      "\",\"machine_name\":\"" + json_escape(machine_name) +
                      "\",\"files_to_upload\":" + build_json_string_array(files_to_upload) +
                      ",\"files_to_delete\":" + build_json_string_array(files_to_delete) + "}";
    const auto result = post_service_method(
        "/ICloudService/BeginAppUploadBatch/v1/", auth, json);
    if (!result.ok) {
        return result;
    }
    const auto parsed = parse_begin_batch_response(result.body);
    if (!parsed.has_value()) {
        return {false, "failed to parse BeginAppUploadBatch response", result.body};
    }
    out_response = *parsed;
    return {true, "ok", result.body};
}

ServiceCallResult begin_http_upload(const SteamCloudWebAuthContext& auth,
                                    std::uint32_t app_id,
                                    std::uint64_t batch_id,
                                    const SteamCloudUploadFile& file,
                                    BeginHttpUploadResponse& out_response) {
    const auto json = std::string{"{\"appid\":\""} + std::to_string(app_id) +
                      "\",\"file_size\":\"" + std::to_string(file.file_size) +
                      "\",\"filename\":\"" + json_escape(file.filename) +
                      "\",\"file_sha\":\"" + json_escape(file.file_sha) +
                      "\",\"is_public\":\"0\",\"platforms_to_sync\":" +
                      build_json_string_array(file.platforms_to_sync) +
                      ",\"upload_batch_id\":\"" + std::to_string(batch_id) + "\"}";
    const auto result =
        post_service_method("/ICloudService/BeginHTTPUpload/v1/", auth, json);
    if (!result.ok) {
        return result;
    }
    const auto parsed = parse_begin_http_upload_response(result.body);
    if (!parsed.has_value()) {
        return {false, "failed to parse BeginHTTPUpload response", result.body};
    }
    out_response = *parsed;
    return {true, "ok", result.body};
}

ServiceCallResult commit_http_upload(const SteamCloudWebAuthContext& auth,
                                     std::uint32_t app_id,
                                     std::string_view filename,
                                     std::string_view file_sha,
                                     bool transfer_succeeded) {
    const auto json = std::string{"{\"appid\":\""} + std::to_string(app_id) +
                      "\",\"transfer_succeeded\":\"" + (transfer_succeeded ? "1" : "0") +
                      "\",\"filename\":\"" + json_escape(filename) +
                      "\",\"file_sha\":\"" + json_escape(file_sha) + "\"}";
    const auto result =
        post_service_method("/ICloudService/CommitHTTPUpload/v1/", auth, json);
    if (!result.ok) {
        return result;
    }
    if (!parse_commit_http_upload_response(result.body)) {
        return {false, "Steam did not commit the uploaded file", result.body};
    }
    return {true, "ok", result.body};
}

ServiceCallResult complete_app_upload_batch(const SteamCloudWebAuthContext& auth,
                                            std::uint32_t app_id,
                                            std::uint64_t batch_id,
                                            bool batch_succeeded) {
    const auto json = std::string{"{\"appid\":\""} + std::to_string(app_id) +
                      "\",\"batch_id\":\"" + std::to_string(batch_id) +
                      "\",\"batch_eresult\":\"" + (batch_succeeded ? "1" : "2") + "\"}";
    return post_service_method("/ICloudService/CompleteAppUploadBatch/v1/", auth, json);
}

ServiceCallResult upload_file_bytes(std::string_view url,
                                    const std::vector<cauth::core::platform::HttpHeader>& headers,
                                    const cauth::core::platform::RouteSelection* route_selection,
                                    std::string_view filename,
                                    const std::vector<std::uint8_t>& bytes,
                                    const SteamCloudUploadCallbacks& callbacks) {
    HttpUploadProgressContext progress_context{filename, &callbacks};
    cauth::core::platform::HttpRequest request;
    request.method = cauth::core::platform::HttpMethod::Put;
    request.url = route_selection == nullptr
                      ? std::string{url}
                      : rewrite_url_for_route_selection(
                            url,
                            *route_selection,
                            {"upload", "content"});
    request.content_type = "application/octet-stream";
    request.headers = headers;
    request.body_view = &bytes;
    request.callbacks.progress_hook =
        [](const cauth::core::platform::HttpTransferProgress& progress, void* user_data) {
            const auto* context = static_cast<const HttpUploadProgressContext*>(user_data);
            if (context == nullptr || context->callbacks == nullptr ||
                context->callbacks->progress_hook == nullptr ||
                progress.direction != cauth::core::platform::HttpTransferDirection::Upload) {
                return;
            }
            context->callbacks->progress_hook(
                context->filename,
                progress.bytes_transferred,
                progress.total_bytes,
                context->callbacks->user_data);
        };
    request.callbacks.cancel_hook =
        [](void* user_data) -> bool {
            const auto* context = static_cast<const HttpUploadProgressContext*>(user_data);
            return context != nullptr && context->callbacks != nullptr &&
                   ((context->callbacks->pause_hook != nullptr &&
                     context->callbacks->pause_hook(context->callbacks->user_data)) ||
                    (context->callbacks->cancel_hook != nullptr &&
                     context->callbacks->cancel_hook(context->callbacks->user_data)));
        };
    request.callbacks.user_data = &progress_context;
    const auto response = cauth::core::platform::perform_platform_http_request(request);
    if (!response.ok) {
        return {false, response.error_message, {}};
    }
    return {true, "ok", {}};
}

} // namespace

std::string build_begin_app_upload_batch_form_body(
    std::string_view access_token,
    std::uint32_t app_id,
    std::string_view machine_name,
    const std::vector<std::string>& files_to_upload,
    const std::vector<std::string>& files_to_delete) {
    return build_begin_app_upload_batch_form_body(
        SteamCloudWebAuthContext{std::string{access_token}, {}, {}, {}},
        app_id,
        machine_name,
        files_to_upload,
        files_to_delete);
}

std::string build_begin_app_upload_batch_form_body(
    const SteamCloudWebAuthContext& auth,
    std::uint32_t app_id,
    std::string_view machine_name,
    const std::vector<std::string>& files_to_upload,
    const std::vector<std::string>& files_to_delete) {
    const auto json = std::string{"{\"appid\":\""} + std::to_string(app_id) +
                      "\",\"machine_name\":\"" + json_escape(machine_name) +
                      "\",\"files_to_upload\":" + build_json_string_array(files_to_upload) +
                      ",\"files_to_delete\":" + build_json_string_array(files_to_delete) + "}";
    return build_service_form_body(auth, json);
}

SteamCloudUploadResult upload_cloud_files(std::string_view access_token,
                                          std::uint32_t app_id,
                                          std::string_view machine_name,
                                          const std::vector<SteamCloudUploadFile>& files,
                                          const std::vector<std::string>& files_to_delete,
                                          const SteamCloudUploadCallbacks& callbacks) {
    return upload_cloud_files(
        SteamCloudWebAuthContext{std::string{access_token}, {}, {}, {}},
        app_id,
        machine_name,
        files,
        files_to_delete,
        callbacks);
}

SteamCloudUploadResult upload_cloud_files(const SteamCloudWebAuthContext& auth,
                                          std::uint32_t app_id,
                                          std::string_view machine_name,
                                          const std::vector<SteamCloudUploadFile>& files,
                                          const std::vector<std::string>& files_to_delete,
                                          const SteamCloudUploadCallbacks& callbacks) {
    if (auth.access_token.empty() && auth.web_cookie_header.empty() &&
        auth.store_cookie_header.empty()) {
        return {false, "access token with write_cloud scope or web cookie session is required"};
    }

    if (callbacks.state_hook != nullptr) {
        callbacks.state_hook(false, false, 0, callbacks.user_data);
    }

    std::vector<std::string> filenames;
    filenames.reserve(files.size());
    for (const auto& file : files) {
        filenames.push_back(file.filename);
    }

    BeginBatchResponse batch{};
    const auto begin_batch =
        begin_app_upload_batch(auth, app_id, machine_name, filenames, files_to_delete, batch);
    if (!begin_batch.ok) {
        return {false, begin_batch.message};
    }

    bool batch_succeeded = true;
    std::string failure_message;
    for (const auto& file : files) {
        BeginHttpUploadResponse upload{};
        const auto begin_upload = begin_http_upload(auth, app_id, batch.batch_id, file, upload);
        if (!begin_upload.ok) {
            batch_succeeded = false;
            failure_message = begin_upload.message;
            break;
        }

        const auto put_result = upload_file_bytes(
            upload.url,
            upload.headers,
            &auth.route_selection,
            file.filename,
            file.bytes,
            callbacks);
        const auto commit_result =
            commit_http_upload(auth, app_id, file.filename, file.file_sha, put_result.ok);
        if (!put_result.ok) {
            batch_succeeded = false;
            failure_message = put_result.message;
            if (!commit_result.ok && failure_message.empty()) {
                failure_message = commit_result.message;
            }
            break;
        }
        if (!commit_result.ok) {
            batch_succeeded = false;
            failure_message = commit_result.message;
            break;
        }
    }

    const auto complete =
        complete_app_upload_batch(auth, app_id, batch.batch_id, batch_succeeded);
    if (!complete.ok) {
        return {false, complete.message};
    }
    if (!batch_succeeded) {
        return {false, failure_message.empty() ? "Steam Cloud upload failed" : failure_message};
    }
    return {true, "ok"};
}

} // namespace cauth::steam::cloud
