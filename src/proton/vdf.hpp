#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace proton_inject {

struct VdfObject : std::map<std::string, std::variant<std::string, VdfObject>, std::less<>> {};

using VdfMap = VdfObject;
using VdfValue = std::variant<std::string, VdfObject>;

[[nodiscard]] VdfMap parse_vdf_file(const std::string& path);
[[nodiscard]] const VdfMap* vdf_block(const VdfMap& root, const std::vector<std::string>& keys);
[[nodiscard]] const VdfValue* vdf_lookup(const VdfMap& map, std::string_view key);
[[nodiscard]] std::string vdf_string(const VdfMap& map, std::string_view key);

}  // namespace proton_inject
