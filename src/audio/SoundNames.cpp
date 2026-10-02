#include "SoundNames.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Frigate::Audio {

namespace {

std::string Upper(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::toupper(c); });
    return out;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

} // namespace

SoundNames::SoundNames(std::string_view text) {
    // The blob's storage may carry trailing NULs past the text.
    text = text.substr(0, text.find_last_not_of('\0') + 1);
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        mNames.emplace_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
}

const std::string& SoundNames::Name(int index) const {
    static const std::string kNone;
    return index >= 0 && index < Count() ? mNames[static_cast<size_t>(index)] : kNone;
}

std::optional<int> SoundNames::Find(std::string_view query) const {
    if (StartsWith(query, "audio/sfx/")) {
        query.remove_prefix(std::string_view("audio/sfx/").size());
    }
    if (query.empty()) {
        return std::nullopt;
    }

    if (std::isdigit(static_cast<unsigned char>(query[0]))) {
        const std::string text(query);
        char* end = nullptr;
        const long value = std::strtol(text.c_str(), &end, 0);
        if (*end != '\0' || value < 0) {
            return std::nullopt;
        }
        return static_cast<int>(value);
    }

    const std::string wanted = Upper(query);
    const std::string withSuffix = wanted + "_SFX";
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < Count(); i++) {
            if (!mNames[static_cast<size_t>(i)].empty() &&
                Upper(mNames[static_cast<size_t>(i)]) == (pass ? withSuffix : wanted)) {
                return i;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> SoundNames::Suggest(std::string_view query, size_t limit) const {
    std::vector<std::string> out;
    const std::string wanted = Upper(query);
    for (const std::string& name : mNames) {
        if (out.size() >= limit) {
            break;
        }
        if (!name.empty() && Upper(name).find(wanted) != std::string::npos) {
            out.push_back(name);
        }
    }
    return out;
}

} // namespace Frigate::Audio
