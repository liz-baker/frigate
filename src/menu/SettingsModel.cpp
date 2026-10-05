#include "menu/SettingsModel.h"

#include <cstdio>

namespace Frigate::ConfigMenu {

std::vector<std::string> ChoiceLabels(const std::vector<Choice>& choices) {
    std::vector<std::string> labels;
    labels.reserve(choices.size());
    for (const auto& choice : choices) {
        labels.push_back(choice.label);
    }
    return labels;
}

int ChoiceIndex(const std::vector<Choice>& choices, int id) {
    for (size_t i = 0; i < choices.size(); i++) {
        if (choices[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

int ChoiceId(const std::vector<Choice>& choices, int index, int fallback) {
    if (index < 0 || index >= static_cast<int>(choices.size())) {
        return fallback;
    }
    return choices[static_cast<size_t>(index)].id;
}

std::string FileUrlFromPath(const std::string& path) {
    std::string url = "file://";
    // A Windows drive path has no leading slash; the URL needs one.
    if (path.empty() || (path[0] != '/' && path[0] != '\\')) {
        url += '/';
    }
    for (const char c : path) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool safe = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '-' ||
                          u == '_' || u == '.' || u == '~' || u == '/' || u == ':';
        if (safe) {
            url += c;
        } else if (c == '\\') {
            url += '/';
        } else {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", u);
            url += hex;
        }
    }
    return url;
}

std::string VersionLabel(const std::string& describe) {
    return describe.empty() ? "Version unknown" : "Version " + describe;
}

} // namespace Frigate::ConfigMenu
