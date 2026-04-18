#include "steam/depot/app_info.hpp"

#include <charconv>
#include <cctype>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace cauth::core::depot {
namespace {

enum class KvType : std::uint8_t {
    Object = 0,
    String = 1,
    Int32 = 2,
    Float32 = 3,
    Pointer = 4,
    WideString = 5,
    Color = 6,
    UInt64 = 7,
    CompiledIntByte = 8,
    CompiledInt0 = 9,
    CompiledInt1 = 10,
    End = 11,
};

struct KvNode {
    std::string name;
    std::string value;
    std::vector<KvNode> children;
};

std::string hex_prefix(const std::vector<std::uint8_t>& bytes, std::size_t max_bytes) {
    std::ostringstream out;
    const auto count = bytes.size() < max_bytes ? bytes.size() : max_bytes;
    for (std::size_t index = 0; index < count; ++index) {
        constexpr char digits[] = "0123456789abcdef";
        if (index != 0) {
            out << ' ';
        }
        out << digits[(bytes[index] >> 4) & 0x0f] << digits[bytes[index] & 0x0f];
    }
    return out.str();
}

std::string lowercase_ascii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const auto ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered;
}

bool read_c_string(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                   std::string& value) {
    const auto start = offset;
    while (offset < bytes.size() && bytes[offset] != 0) {
        ++offset;
    }
    if (offset >= bytes.size()) {
        return false;
    }

    value.assign(reinterpret_cast<const char*>(bytes.data() + start), offset - start);
    ++offset;
    return true;
}

bool read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                 std::uint32_t& value) {
    if (bytes.size() - offset < 4) {
        return false;
    }

    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool read_u64_le(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                 std::uint64_t& value) {
    if (bytes.size() - offset < 8) {
        return false;
    }

    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool read_wide_string_as_ascii(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                               std::string& value) {
    std::uint32_t character_count = 0;
    if (!read_u32_le(bytes, offset, character_count)) {
        return false;
    }

    if (character_count > (bytes.size() - offset) / 2) {
        return false;
    }

    value.clear();
    for (std::uint32_t index = 0; index < character_count; ++index) {
        const auto low = bytes[offset];
        const auto high = bytes[offset + 1];
        offset += 2;
        if (high == 0 && low != 0) {
            value.push_back(static_cast<char>(low));
        }
    }
    return true;
}

bool parse_kv_object(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                     std::vector<KvNode>& children, std::uint8_t end_marker) {
    while (offset < bytes.size()) {
        const auto raw_type = bytes[offset++];
        if (raw_type == end_marker) {
            return true;
        }

        const auto type = static_cast<KvType>(raw_type);
        KvNode node;
        if (!read_c_string(bytes, offset, node.name)) {
            return false;
        }

        switch (type) {
        case KvType::Object:
            if (!parse_kv_object(bytes, offset, node.children, end_marker)) {
                return false;
            }
            break;
        case KvType::String:
            if (!read_c_string(bytes, offset, node.value)) {
                return false;
            }
            break;
        case KvType::Int32: {
            std::uint32_t value = 0;
            if (!read_u32_le(bytes, offset, value)) {
                return false;
            }
            node.value = std::to_string(value);
            break;
        }
        case KvType::Float32:
        case KvType::Pointer:
        case KvType::Color: {
            std::uint32_t ignored = 0;
            if (!read_u32_le(bytes, offset, ignored)) {
                return false;
            }
            node.value = std::to_string(ignored);
            break;
        }
        case KvType::WideString:
            if (!read_wide_string_as_ascii(bytes, offset, node.value)) {
                return false;
            }
            break;
        case KvType::UInt64: {
            std::uint64_t value = 0;
            if (!read_u64_le(bytes, offset, value)) {
                return false;
            }
            node.value = std::to_string(value);
            break;
        }
        case KvType::CompiledIntByte:
            if (end_marker == 8) {
                return false;
            }
            if (offset >= bytes.size()) {
                return false;
            }
            node.value = std::to_string(bytes[offset++]);
            break;
        case KvType::CompiledInt0:
            node.value = "0";
            break;
        case KvType::CompiledInt1:
            node.value = "1";
            break;
        case KvType::End:
            return true;
        default:
            return false;
        }

        children.push_back(std::move(node));
    }

    return true;
}

std::optional<std::vector<KvNode>> try_parse_kv_root_at(const std::vector<std::uint8_t>& bytes,
                                                        std::size_t start_offset,
                                                        std::uint8_t end_marker) {
    std::size_t offset = start_offset;
    std::vector<KvNode> root;
    if (!parse_kv_object(bytes, offset, root, end_marker)) {
        return std::nullopt;
    }

    return root;
}

void skip_text_trivia(std::string_view text, std::size_t& offset) {
    while (offset < text.size()) {
        if (text[offset] == 0 ||
            std::isspace(static_cast<unsigned char>(text[offset])) != 0) {
            ++offset;
            continue;
        }

        if (text[offset] == '/' && offset + 1 < text.size() && text[offset + 1] == '/') {
            offset += 2;
            while (offset < text.size() && text[offset] != '\n') {
                ++offset;
            }
            continue;
        }

        break;
    }
}

bool read_text_token(std::string_view text, std::size_t& offset, std::string& value) {
    skip_text_trivia(text, offset);
    if (offset >= text.size()) {
        return false;
    }

    if (text[offset] == '"') {
        ++offset;
        value.clear();
        while (offset < text.size()) {
            const auto ch = text[offset++];
            if (ch == '"') {
                return true;
            }
            if (ch == '\\' && offset < text.size()) {
                value.push_back(text[offset++]);
                continue;
            }
            value.push_back(ch);
        }
        return false;
    }

    const auto start = offset;
    while (offset < text.size() &&
           std::isspace(static_cast<unsigned char>(text[offset])) == 0 && text[offset] != '{' &&
           text[offset] != '}' && text[offset] != 0) {
        ++offset;
    }
    value = std::string{text.substr(start, offset - start)};
    return !value.empty();
}

bool parse_text_kv_object(std::string_view text, std::size_t& offset,
                          std::vector<KvNode>& children) {
    skip_text_trivia(text, offset);
    if (offset >= text.size() || text[offset] != '{') {
        return false;
    }
    ++offset;

    while (true) {
        skip_text_trivia(text, offset);
        if (offset >= text.size()) {
            return false;
        }
        if (text[offset] == '}') {
            ++offset;
            return true;
        }

        KvNode node;
        if (!read_text_token(text, offset, node.name)) {
            return false;
        }

        skip_text_trivia(text, offset);
        if (offset < text.size() && text[offset] == '{') {
            if (!parse_text_kv_object(text, offset, node.children)) {
                return false;
            }
        } else if (!read_text_token(text, offset, node.value)) {
            return false;
        }

        children.push_back(std::move(node));
    }
}

std::optional<std::vector<KvNode>> parse_text_kv_root(const std::vector<std::uint8_t>& bytes) {
    const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    std::size_t offset = 0;
    std::vector<KvNode> root;

    while (true) {
        skip_text_trivia(text, offset);
        if (offset >= text.size()) {
            break;
        }

        KvNode node;
        if (!read_text_token(text, offset, node.name)) {
            return std::nullopt;
        }

        if (!parse_text_kv_object(text, offset, node.children)) {
            return std::nullopt;
        }
        root.push_back(std::move(node));
    }

    return root;
}

std::optional<std::uint64_t> parse_u64(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

const KvNode* find_child(const std::vector<KvNode>& nodes, std::string_view name) {
    for (const auto& node : nodes) {
        if (node.name == name) {
            return &node;
        }
    }
    return nullptr;
}

const KvNode* find_descendant(const std::vector<KvNode>& nodes, std::string_view name) {
    for (const auto& node : nodes) {
        if (node.name == name) {
            return &node;
        }
        if (const auto* child = find_descendant(node.children, name)) {
            return child;
        }
    }
    return nullptr;
}

bool is_decimal_string(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    for (const auto ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

std::string value_of(const KvNode& object, std::string_view name) {
    if (const auto* child = find_child(object.children, name)) {
        return child->value;
    }
    return {};
}

std::uint32_t u32_value_of(const KvNode& object, std::string_view name) {
    const auto parsed = parse_u64(value_of(object, name));
    return parsed.has_value() ? static_cast<std::uint32_t>(*parsed) : 0;
}

std::uint64_t u64_value_of(const KvNode& object, std::string_view name) {
    const auto parsed = parse_u64(value_of(object, name));
    return parsed.value_or(0);
}

std::vector<AppBranchInfo> parse_branches(const KvNode& branches_node) {
    std::vector<AppBranchInfo> branches;
    for (const auto& branch_node : branches_node.children) {
        if (branch_node.children.empty()) {
            continue;
        }

        AppBranchInfo branch;
        branch.name = branch_node.name;
        branch.build_id = value_of(branch_node, "buildid");
        branch.description = value_of(branch_node, "description");
        branch.time_updated = u32_value_of(branch_node, "timeupdated");
        branch.password_required = value_of(branch_node, "pwdrequired") == "1";
        branches.push_back(std::move(branch));
    }
    return branches;
}

DepotManifestInfo parse_manifest_node(const KvNode& manifest_node, bool encrypted) {
    DepotManifestInfo manifest;
    manifest.branch = manifest_node.name;
    manifest.encrypted = encrypted;

    if (!manifest_node.children.empty()) {
        manifest.manifest_gid = u64_value_of(manifest_node, "gid");
        manifest.size = u64_value_of(manifest_node, "size");
        manifest.download_size = u64_value_of(manifest_node, "download");
        return manifest;
    }

    manifest.manifest_gid = parse_u64(manifest_node.value).value_or(0);
    return manifest;
}

void append_manifest_nodes(const KvNode& manifests_node, bool encrypted,
                           std::vector<DepotManifestInfo>& manifests) {
    for (const auto& manifest_node : manifests_node.children) {
        auto manifest = parse_manifest_node(manifest_node, encrypted);
        if (!manifest.branch.empty() && manifest.manifest_gid != 0) {
            manifests.push_back(std::move(manifest));
        }
    }
}

std::vector<DepotInfo> parse_depots(const KvNode& depots_node) {
    std::vector<DepotInfo> depots;
    for (const auto& depot_node : depots_node.children) {
        if (!is_decimal_string(depot_node.name)) {
            continue;
        }

        const auto depot_id = parse_u64(depot_node.name);
        if (!depot_id.has_value()) {
            continue;
        }

        DepotInfo depot;
        depot.depot_id = static_cast<std::uint32_t>(*depot_id);
        depot.depot_from_app = value_of(depot_node, "depotfromapp");
        if (const auto* config = find_child(depot_node.children, "config")) {
            depot.os_list = value_of(*config, "oslist");
            depot.os_arch = value_of(*config, "osarch");
            depot.languages = value_of(*config, "language");
            depot.shared_install = value_of(*config, "sharedinstall") == "1";
        }
        if (const auto* manifests = find_child(depot_node.children, "manifests")) {
            append_manifest_nodes(*manifests, false, depot.manifests);
        }
        if (const auto* encrypted = find_child(depot_node.children, "encryptedmanifests")) {
            append_manifest_nodes(*encrypted, true, depot.manifests);
        }
        depots.push_back(std::move(depot));
    }
    return depots;
}

std::size_t count_nodes(const std::vector<KvNode>& nodes) {
    std::size_t count = nodes.size();
    for (const auto& node : nodes) {
        count += count_nodes(node.children);
    }
    return count;
}

std::size_t app_info_candidate_score(const std::vector<KvNode>& nodes) {
    std::size_t score = 0;
    if (find_descendant(nodes, "common") != nullptr) {
        score += 1;
    }
    if (find_descendant(nodes, "depots") != nullptr) {
        score += 4;
    }
    if (find_descendant(nodes, "branches") != nullptr) {
        score += 4;
    }
    if (find_descendant(nodes, "appid") != nullptr) {
        score += 1;
    }
    return score;
}

} // namespace

std::optional<AppInfo> parse_app_info_buffer(const std::vector<std::uint8_t>& bytes) {
    return parse_app_info_buffer(bytes, nullptr);
}

std::optional<AppInfo> parse_app_info_buffer(const std::vector<std::uint8_t>& bytes,
                                             AppInfoParseDebug* debug) {
    if (debug != nullptr) {
        *debug = AppInfoParseDebug{};
        debug->buffer_size = bytes.size();
        debug->prefix_hex = hex_prefix(bytes, 128);
    }

    std::optional<std::vector<KvNode>> root;
    std::size_t root_score = 0;
    if (!bytes.empty() && (bytes[0] == '"' || bytes[0] == '{' ||
                           std::isspace(static_cast<unsigned char>(bytes[0])) != 0)) {
        auto text_candidate = parse_text_kv_root(bytes);
        if (text_candidate.has_value()) {
            root_score = app_info_candidate_score(*text_candidate);
            if (debug != nullptr) {
                debug->best_offset = 0;
                debug->best_end_marker = 0;
                debug->best_nodes = count_nodes(*text_candidate);
                debug->best_score = root_score;
            }
            if (root_score > 0) {
                root = std::move(text_candidate);
            }
        }
    }

    const auto max_scan_offset = bytes.size() < 96 ? bytes.size() : std::size_t{96};
    if (!root.has_value()) {
        for (const auto end_marker : {std::uint8_t{8}, std::uint8_t{11}}) {
            for (std::size_t offset = 0; offset <= max_scan_offset; ++offset) {
                auto candidate = try_parse_kv_root_at(bytes, offset, end_marker);
                if (!candidate.has_value()) {
                    continue;
                }

                const auto candidate_score = app_info_candidate_score(*candidate);
                const auto candidate_nodes = count_nodes(*candidate);
                if (debug != nullptr && candidate_score >= debug->best_score) {
                    debug->best_score = candidate_score;
                    debug->best_offset = offset;
                    debug->best_end_marker = end_marker;
                    debug->best_nodes = candidate_nodes;
                }

                if (candidate_score > root_score) {
                    root_score = candidate_score;
                    root = std::move(candidate);
                }
            }
            if (root_score >= 4) {
                break;
            }
        }
    }

    if (!root.has_value()) {
        return std::nullopt;
    }

    AppInfo app;
    if (const auto* app_id = find_descendant(*root, "appid")) {
        app.app_id = static_cast<std::uint32_t>(parse_u64(app_id->value).value_or(0));
    }
    if (const auto* branches = find_descendant(*root, "branches")) {
        app.branches = parse_branches(*branches);
    }
    if (const auto* depots = find_descendant(*root, "depots")) {
        app.depots = parse_depots(*depots);
    }

    if (debug != nullptr) {
        debug->ok = true;
    }
    return app;
}

std::vector<std::string> depot_platform_tags(std::string_view os_list) {
    std::vector<std::string> tags;
    std::string token;
    token.reserve(os_list.size());

    const auto append_token = [&]() {
        if (token.empty()) {
            return;
        }
        auto normalized = lowercase_ascii(token);
        token.clear();
        if (normalized.empty()) {
            return;
        }
        if (normalized.find("windows") != std::string::npos || normalized == "win32" ||
            normalized == "win64" || normalized == "win") {
            normalized = "windows";
        } else if (normalized.find("linux") != std::string::npos) {
            normalized = "linux";
        } else if (normalized.find("mac") != std::string::npos ||
                   normalized.find("osx") != std::string::npos) {
            normalized = "macos";
        } else if (normalized.find("android") != std::string::npos) {
            normalized = "android";
        } else if (normalized.find("ios") != std::string::npos ||
                   normalized.find("iphone") != std::string::npos ||
                   normalized.find("ipad") != std::string::npos) {
            normalized = "ios";
        }
        for (const auto& existing : tags) {
            if (existing == normalized) {
                return;
            }
        }
        tags.push_back(std::move(normalized));
    };

    for (const auto ch : os_list) {
        if (ch == ',' || ch == ';' || ch == '|' ||
            std::isspace(static_cast<unsigned char>(ch)) != 0) {
            append_token();
            continue;
        }
        token.push_back(static_cast<char>(ch));
    }
    append_token();
    return tags;
}

std::string depot_platform_label(std::string_view os_list, std::string_view os_arch) {
    const auto tags = depot_platform_tags(os_list);
    std::string label;
    if (tags.empty()) {
        label = "shared";
    } else {
        for (std::size_t index = 0; index < tags.size(); ++index) {
            if (index != 0) {
                label += '+';
            }
            label += tags[index];
        }
    }

    const auto arch = lowercase_ascii(os_arch);
    if (!arch.empty()) {
        label += '/';
        label += arch;
    }
    return label;
}

} // namespace cauth::core::depot
