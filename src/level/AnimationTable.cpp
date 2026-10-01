#include "level/AnimationTable.h"

#include <cstdlib>

namespace Frigate::Level {

std::optional<AnimationTable> AnimationTable::Parse(std::string_view text) {
    AnimationTable table;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty()) {
            continue;
        }

        const size_t firstTab = line.find('\t');
        const size_t secondTab = firstTab == std::string_view::npos ? firstTab : line.find('\t', firstTab + 1);
        if (secondTab == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string which(line.substr(0, firstTab));
        const std::string index(line.substr(firstTab + 1, secondTab - firstTab - 1));
        std::string name(line.substr(secondTab + 1));
        char* parsedEnd = nullptr;
        const long id = std::strtol(index.c_str(), &parsedEnd, 10);
        if (parsedEnd == index.c_str() || *parsedEnd != '\0' || id < 0) {
            return std::nullopt;
        }
        if (name == "-") {
            name.clear();
        }

        std::vector<std::string>* names = which == "1" ? &table.characters_ : which == "2" ? &table.aircraft_ : nullptr;
        if (names == nullptr || static_cast<size_t>(id) != names->size()) {
            return std::nullopt;
        }
        names->push_back(std::move(name));
    }
    if (table.characters_.empty()) {
        return std::nullopt;
    }
    return table;
}

const std::string& AnimationTable::Lookup(const std::vector<std::string>& names, int id) {
    static const std::string kNone;
    return id >= 0 && static_cast<size_t>(id) < names.size() ? names[static_cast<size_t>(id)] : kNone;
}

} // namespace Frigate::Level
