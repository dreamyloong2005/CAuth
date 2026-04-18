#include "steam/cm/cm_message.hpp"
#include "steam/depot/app_info.hpp"
#include "steam/depot/depot_resolver.hpp"
#include "steam/depot/pics.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void append_c_string(std::vector<std::uint8_t>& out, std::string_view value) {
    out.insert(out.end(), value.begin(), value.end());
    out.push_back(0);
}

void append_kv_object_begin(std::vector<std::uint8_t>& out, std::string_view name) {
    out.push_back(0);
    append_c_string(out, name);
}

void append_kv_end(std::vector<std::uint8_t>& out) {
    out.push_back(11);
}

void append_legacy_kv_end(std::vector<std::uint8_t>& out) {
    out.push_back(8);
}

void append_kv_compiled_int1(std::vector<std::uint8_t>& out, std::string_view name) {
    out.push_back(10);
    append_c_string(out, name);
}

void append_kv_string(std::vector<std::uint8_t>& out, std::string_view name,
                      std::string_view value) {
    out.push_back(1);
    append_c_string(out, name);
    append_c_string(out, value);
}

void append_kv_int32(std::vector<std::uint8_t>& out, std::string_view name,
                     std::uint32_t value) {
    out.push_back(2);
    append_c_string(out, name);
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

} // namespace

int main() {
    const auto request = cauth::core::depot::make_pics_product_info_request(
        cauth::core::depot::PicsProductInfoRequest{
            {cauth::core::depot::PicsProductInfoAppRequest{440, 0, true}},
            true,
            true,
        });

    if (request.emsg != cauth::core::cm::EMsg::ClientPICSProductInfoRequest ||
        !request.protobuf || request.body.empty()) {
        std::cerr << "PICS product info request should be a protobuf CM message\n";
        return 1;
    }

    const auto encoded = cauth::core::cm::encode_cm_message(request);
    const auto decoded = cauth::core::cm::decode_cm_message(encoded);
    if (!decoded.has_value() || decoded->emsg != request.emsg || decoded->body != request.body) {
        std::cerr << "PICS product info request should round-trip through CM framing\n";
        return 1;
    }
    if (request.body.empty() || request.body[0] != 0x12) {
        std::cerr << "PICS product info request should encode apps on protobuf field 2\n";
        return 1;
    }

    const std::vector<std::uint8_t> app_info{
        0x08, 0xb8, 0x03,
        0x10, 0x2a,
        0x2a, 0x03, 0x01, 0x02, 0x03,
    };
    std::vector<std::uint8_t> response_body{
        0x0a, static_cast<std::uint8_t>(app_info.size()),
    };
    response_body.insert(response_body.end(), app_info.begin(), app_info.end());
    response_body.push_back(0x30);
    response_body.push_back(0x01);

    const auto response =
        cauth::core::depot::parse_pics_product_info_response_body(response_body);
    if (!response.has_value() || response->apps.size() != 1 || response->apps[0].app_id != 440 ||
        response->apps[0].change_number != 42 ||
        response->apps[0].buffer != std::vector<std::uint8_t>{1, 2, 3} ||
        !response->response_pending) {
        std::cerr << "PICS product info response should parse basic app info\n";
        return 1;
    }

    std::vector<std::uint8_t> app_info_buffer;
    append_kv_int32(app_info_buffer, "appid", 440);
    append_kv_object_begin(app_info_buffer, "depots");
    append_kv_object_begin(app_info_buffer, "branches");
    append_kv_object_begin(app_info_buffer, "public");
    append_kv_string(app_info_buffer, "buildid", "1234");
    append_kv_string(app_info_buffer, "timeupdated", "55");
    append_kv_end(app_info_buffer);
    append_kv_object_begin(app_info_buffer, "beta");
    append_kv_string(app_info_buffer, "buildid", "5678");
    append_kv_compiled_int1(app_info_buffer, "pwdrequired");
    append_kv_end(app_info_buffer);
    append_kv_end(app_info_buffer);
    append_kv_object_begin(app_info_buffer, "228981");
    append_kv_string(app_info_buffer, "depotfromapp", "440");
    append_kv_object_begin(app_info_buffer, "config");
    append_kv_string(app_info_buffer, "oslist", "windows,linux");
    append_kv_string(app_info_buffer, "osarch", "64");
    append_kv_compiled_int1(app_info_buffer, "sharedinstall");
    append_kv_end(app_info_buffer);
    append_kv_object_begin(app_info_buffer, "manifests");
    append_kv_string(app_info_buffer, "public", "111111111111");
    append_kv_object_begin(app_info_buffer, "beta");
    append_kv_string(app_info_buffer, "gid", "222222222222");
    append_kv_string(app_info_buffer, "size", "333");
    append_kv_string(app_info_buffer, "download", "444");
    append_kv_end(app_info_buffer);
    append_kv_end(app_info_buffer);
    append_kv_end(app_info_buffer);
    append_kv_end(app_info_buffer);
    append_kv_end(app_info_buffer);

    const auto parsed_app_info =
        cauth::core::depot::parse_app_info_buffer(app_info_buffer);
    if (!parsed_app_info.has_value() || parsed_app_info->app_id != 440 ||
        parsed_app_info->branches.size() != 2 || parsed_app_info->depots.size() != 1 ||
        parsed_app_info->depots[0].depot_id != 228981 ||
        parsed_app_info->depots[0].os_list != "windows,linux" ||
        parsed_app_info->depots[0].os_arch != "64" ||
        parsed_app_info->depots[0].depot_from_app != "440" ||
        !parsed_app_info->depots[0].shared_install ||
        parsed_app_info->depots[0].manifests.size() != 2 ||
        parsed_app_info->depots[0].manifests[0].branch != "public" ||
        parsed_app_info->depots[0].manifests[0].manifest_gid != 111111111111ULL ||
        parsed_app_info->depots[0].manifests[1].branch != "beta" ||
        parsed_app_info->depots[0].manifests[1].manifest_gid != 222222222222ULL ||
        parsed_app_info->depots[0].manifests[1].size != 333 ||
        parsed_app_info->depots[0].manifests[1].download_size != 444 ||
        !parsed_app_info->branches[1].password_required) {
        std::cerr << "appinfo buffer should expose branches and depot manifests\n";
        return 1;
    }

    std::vector<std::uint8_t> legacy_app_info_buffer;
    append_kv_int32(legacy_app_info_buffer, "appid", 440);
    append_kv_object_begin(legacy_app_info_buffer, "depots");
    append_kv_object_begin(legacy_app_info_buffer, "branches");
    append_kv_object_begin(legacy_app_info_buffer, "public");
    append_kv_string(legacy_app_info_buffer, "buildid", "1234");
    append_legacy_kv_end(legacy_app_info_buffer);
    append_legacy_kv_end(legacy_app_info_buffer);
    append_kv_object_begin(legacy_app_info_buffer, "228981");
    append_kv_object_begin(legacy_app_info_buffer, "manifests");
    append_kv_string(legacy_app_info_buffer, "public", "111111111111");
    append_legacy_kv_end(legacy_app_info_buffer);
    append_legacy_kv_end(legacy_app_info_buffer);
    append_legacy_kv_end(legacy_app_info_buffer);
    append_legacy_kv_end(legacy_app_info_buffer);

    const auto parsed_legacy_app_info =
        cauth::core::depot::parse_app_info_buffer(legacy_app_info_buffer);
    if (!parsed_legacy_app_info.has_value() ||
        parsed_legacy_app_info->branches.size() != 1 ||
        parsed_legacy_app_info->depots.size() != 1 ||
        parsed_legacy_app_info->depots[0].manifests.size() != 1) {
        std::cerr << "appinfo buffer should accept legacy binary VDF end marker\n";
        return 1;
    }

    const std::string text_app_info =
        "\"appinfo\"\n"
        "{\n"
        "    \"appid\"        \"440\"\n"
        "    \"depots\"\n"
        "    {\n"
        "        \"branches\"\n"
        "        {\n"
        "            \"public\"\n"
        "            {\n"
        "                \"buildid\"        \"1234\"\n"
        "            }\n"
        "        }\n"
        "        \"228981\"\n"
        "        {\n"
        "            \"depotfromapp\"    \"123\"\n"
        "            \"config\"\n"
        "            {\n"
        "                \"oslist\"      \"macos\"\n"
        "                \"osarch\"      \"arm64\"\n"
        "            }\n"
        "            \"manifests\"\n"
        "            {\n"
        "                \"public\"        \"111111111111\"\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n"
        "// trailing comment\n";
    const std::vector<std::uint8_t> text_app_info_buffer{text_app_info.begin(),
                                                         text_app_info.end()};
    const auto parsed_text_app_info =
        cauth::core::depot::parse_app_info_buffer(text_app_info_buffer);
    if (!parsed_text_app_info.has_value() || parsed_text_app_info->app_id != 440 ||
        parsed_text_app_info->branches.size() != 1 ||
        parsed_text_app_info->depots.size() != 1 ||
        parsed_text_app_info->depots[0].manifests.size() != 1 ||
        parsed_text_app_info->depots[0].os_list != "macos" ||
        parsed_text_app_info->depots[0].os_arch != "arm64" ||
        parsed_text_app_info->depots[0].depot_from_app != "123" ||
        parsed_text_app_info->depots[0].manifests[0].manifest_gid != 111111111111ULL) {
        std::cerr << "appinfo buffer should accept text VDF appinfo\n";
        return 1;
    }

    const auto branches = cauth::core::depot::available_branches(*parsed_app_info);
    const auto beta_branch = cauth::core::depot::find_branch(*parsed_app_info, "beta");
    const auto missing_branch = cauth::core::depot::find_branch(*parsed_app_info, "missing");
    const auto beta_selection =
        cauth::core::depot::resolve_depot_manifests(*parsed_app_info, "beta");
    const auto missing_selection =
        cauth::core::depot::resolve_depot_manifests(*parsed_app_info, "missing");
    if (branches.size() != 2 || !beta_branch.has_value() || missing_branch.has_value() ||
        !beta_selection.has_value() || beta_selection->branch != "beta" ||
        beta_selection->manifests.size() != 1 ||
        beta_selection->manifests[0].depot_id != 228981 ||
        beta_selection->manifests[0].manifest_gid != 222222222222ULL ||
        beta_selection->manifests[0].size != 333 ||
        beta_selection->manifests[0].download_size != 444 ||
        beta_selection->manifests[0].platform_label != "windows+linux/64" ||
        beta_selection->manifests[0].depot_from_app != "440" ||
        !beta_selection->manifests[0].shared_install ||
        missing_selection.has_value()) {
        std::cerr << "depot resolver should select branch-specific manifests\n";
        return 1;
    }

    const auto normalized_platforms =
        cauth::core::depot::depot_platform_tags("windows;linux osx");
    if (normalized_platforms.size() != 3 || normalized_platforms[0] != "windows" ||
        normalized_platforms[1] != "linux" || normalized_platforms[2] != "macos" ||
        cauth::core::depot::depot_platform_label({}, {}) != "shared" ||
        cauth::core::depot::depot_platform_label("windows", "x64") != "windows/x64") {
        std::cerr << "depot platform helpers should normalize oslist and arch\n";
        return 1;
    }

    auto text_app_info_with_nul = text_app_info_buffer;
    text_app_info_with_nul.push_back(0);
    text_app_info_with_nul.push_back(0);
    if (!cauth::core::depot::parse_app_info_buffer(text_app_info_with_nul).has_value()) {
        std::cerr << "appinfo buffer should allow trailing NUL bytes\n";
        return 1;
    }

    if (cauth::core::depot::parse_pics_product_info_response_body({0x1a, 0xff}).has_value()) {
        std::cerr << "truncated PICS response should be rejected\n";
        return 1;
    }

    return 0;
}
