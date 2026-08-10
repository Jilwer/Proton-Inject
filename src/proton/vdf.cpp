#include "proton/vdf.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace proton_inject {

namespace {

class VdfParser {
public:
    explicit VdfParser(std::string source) : source_(std::move(source)) {}

    VdfMap parse_block(const bool top_level) {
        VdfMap block;
        while (true) {
            auto [key, key_quoted] = next_token();
            if (key.empty() && !key_quoted) {
                return block;
            }
            if (key == "}") {
                if (top_level) {
                    continue;
                }
                return block;
            }
            if (key == "{") {
                continue;
            }

            auto [value, value_quoted] = next_token();
            if (value == "{") {
                block[key] = parse_block(false);
            } else if (value.empty() && !value_quoted) {
                return block;
            } else {
                block[key] = value;
            }
        }
    }

private:
    void skip_filler() {
        while (pos_ < source_.size()) {
            const char c = source_[pos_];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++pos_;
                continue;
            }
            if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
                while (pos_ < source_.size() && source_[pos_] != '\n') {
                    ++pos_;
                }
                continue;
            }
            if (c == '[') {
                while (pos_ < source_.size() && source_[pos_] != ']') {
                    ++pos_;
                }
                ++pos_;
                continue;
            }
            return;
        }
    }

    std::pair<std::string, bool> next_token() {
        skip_filler();
        if (pos_ >= source_.size()) {
            return {"", false};
        }

        const char c = source_[pos_];
        if (c == '{' || c == '}') {
            ++pos_;
            return {std::string(1, c), false};
        }

        if (c == '"') {
            ++pos_;
            std::string value;
            while (pos_ < source_.size() && source_[pos_] != '"') {
                if (source_[pos_] == '\\' && pos_ + 1 < source_.size()) {
                    ++pos_;
                    switch (source_[pos_]) {
                        case 'n':
                            value.push_back('\n');
                            break;
                        case 't':
                            value.push_back('\t');
                            break;
                        default:
                            value.push_back(source_[pos_]);
                            break;
                    }
                } else {
                    value.push_back(source_[pos_]);
                }
                ++pos_;
            }
            ++pos_;
            return {value, true};
        }

        const std::size_t start = pos_;
        while (pos_ < source_.size()) {
            const char ch = source_[pos_];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '{' || ch == '}' ||
                ch == '"') {
                break;
            }
            ++pos_;
        }
        return {source_.substr(start, pos_ - start), true};
    }

    std::string source_;
    std::size_t pos_ = 0;
};

bool iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

VdfMap parse_vdf_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    VdfParser parser(buffer.str());
    return parser.parse_block(true);
}

const VdfMap* vdf_block(const VdfMap& root, const std::vector<std::string>& keys) {
    const VdfMap* current = &root;
    for (const auto& key : keys) {
        if (current == nullptr) {
            return nullptr;
        }
        const auto* value = vdf_lookup(*current, key);
        if (value == nullptr) {
            return nullptr;
        }
        if (const auto* block = std::get_if<VdfObject>(value)) {
            current = block;
        } else {
            return nullptr;
        }
    }
    return current;
}

const VdfValue* vdf_lookup(const VdfMap& map, const std::string_view key) {
    if (const auto it = map.find(key); it != map.end()) {
        return &it->second;
    }
    for (const auto& [entry_key, value] : map) {
        if (iequals(entry_key, key)) {
            return &value;
        }
    }
    return nullptr;
}

std::string vdf_string(const VdfMap& map, const std::string_view key) {
    const auto* value = vdf_lookup(map, key);
    if (value == nullptr) {
        return {};
    }
    if (const auto* text = std::get_if<std::string>(value)) {
        return *text;
    }
    return {};
}

}  // namespace proton_inject
