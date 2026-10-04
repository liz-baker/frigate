#include "platform/InputScript.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <map>

namespace Frigate::Platform::Input {

namespace {

const std::map<std::string, uint16_t>& ButtonNames() {
    static const std::map<std::string, uint16_t> names = {
        { "CR", 0x0001 },    { "CL", 0x0002 }, { "CD", 0x0004 }, { "CU", 0x0008 }, { "R", 0x0010 },
        { "L", 0x0020 },     { "DR", 0x0100 }, { "DL", 0x0200 }, { "DD", 0x0400 }, { "DU", 0x0800 },
        { "START", 0x1000 }, { "Z", 0x2000 },  { "B", 0x4000 },  { "A", 0x8000 },
    };
    return names;
}

bool ParseNumber(const std::string& text, long& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    out = std::strtol(text.c_str(), &end, 10);
    return *end == '\0';
}

bool ParseState(const std::string& text, PadSample& out, std::string& error) {
    out = PadSample{};
    size_t start = 0;
    while (!text.empty()) {
        const size_t plus = text.find('+', start);
        const std::string token = text.substr(start, plus == std::string::npos ? plus : plus - start);
        if (token.size() > 2 && (token[0] == 'X' || token[0] == 'Y') && token[1] == '=') {
            long value = 0;
            if (!ParseNumber(token.substr(2), value) || value < -128 || value > 127) {
                error = "bad stick value '" + token + "'";
                return false;
            }
            (token[0] == 'X' ? out.stickX : out.stickY) = static_cast<int8_t>(value);
        } else if (const auto it = ButtonNames().find(token); it != ButtonNames().end()) {
            out.buttons = static_cast<uint16_t>(out.buttons | it->second);
        } else {
            error = "unknown button '" + token + "'";
            return false;
        }
        if (plus == std::string::npos) {
            break;
        }
        start = plus + 1;
    }
    return true;
}

} // namespace

std::optional<InputScript> InputScript::Parse(const std::string& text, std::string* error) {
    std::string message;
    InputScript script;
    // Entries on one frame: a later one replaces an earlier one.
    std::map<uint32_t, PadSample> byFrame;
    size_t start = 0;
    while (start < text.size()) {
        const size_t comma = text.find(',', start);
        const std::string entry = text.substr(start, comma == std::string::npos ? comma : comma - start);
        start = comma == std::string::npos ? text.size() : comma + 1;
        if (entry.empty()) {
            continue;
        }
        const size_t colon = entry.find(':');
        if (colon == std::string::npos) {
            message = "entry '" + entry + "' has no ':'";
            break;
        }
        const std::string frames = entry.substr(0, colon);
        const size_t dash = frames.find('-');
        long first = 0;
        long last = -1;
        if (!ParseNumber(frames.substr(0, dash), first) || first < 0 ||
            (dash != std::string::npos && (!ParseNumber(frames.substr(dash + 1), last) || last < first))) {
            message = "bad frames '" + frames + "'";
            break;
        }
        PadSample sample;
        if (!ParseState(entry.substr(colon + 1), sample, message)) {
            break;
        }
        byFrame[static_cast<uint32_t>(first)] = sample;
        if (dash != std::string::npos) {
            // The release after a range doesn't displace an entry written there.
            byFrame.emplace(static_cast<uint32_t>(last) + 1, PadSample{});
        }
    }
    if (!message.empty()) {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    }
    for (const auto& [frame, sample] : byFrame) {
        script.steps_.push_back({ frame, sample });
    }
    return script;
}

PadSample InputScript::At(uint32_t frame) const {
    const auto next = std::upper_bound(steps_.begin(), steps_.end(), frame,
                                       [](uint32_t value, const Step& step) { return value < step.frame; });
    return next == steps_.begin() ? PadSample{} : std::prev(next)->sample;
}

namespace {
std::optional<InputScript>& ScriptStorage() {
    static std::optional<InputScript> script;
    return script;
}
} // namespace

void SetScript(std::optional<InputScript> script) {
    ScriptStorage() = std::move(script);
}

const std::optional<InputScript>& Script() {
    return ScriptStorage();
}

} // namespace Frigate::Platform::Input
