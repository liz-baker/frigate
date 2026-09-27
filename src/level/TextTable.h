#pragma once

#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace Frigate::Level {

// Helpers for the tab-separated text resources the packers write.

inline std::vector<std::string_view> Split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

template <typename T> bool ParseNumber(std::string_view text, T& out, int base = 10) {
    if (base == 16 && text.size() > 2 && text[0] == '0' && text[1] == 'x') {
        text.remove_prefix(2);
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out, base);
    return error == std::errc() && end == text.data() + text.size();
}

// std::from_chars has no floating-point overload in every standard library the
// port builds with.
inline bool ParseFloat(std::string_view text, float& out) {
    const std::string copy(text);
    char* end = nullptr;
    out = std::strtof(copy.c_str(), &end);
    return !copy.empty() && end == copy.c_str() + copy.size();
}

} // namespace Frigate::Level
