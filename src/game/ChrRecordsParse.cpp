#include "game/ChrRecords.h"

#include <charconv>
#include <cstdlib>

namespace Frigate::Game {

namespace {

constexpr size_t kFieldCount = 8;
constexpr size_t kHatFieldCount = 7;

std::vector<std::string_view> Split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

bool ParseNumber(std::string_view text, size_t& out) {
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc() && end == text.data() + text.size();
}

// std::from_chars has no floating-point overload in every standard library the
// port builds with.
bool ParseFloat(std::string_view text, float& out) {
    const std::string copy(text);
    char* end = nullptr;
    out = std::strtof(copy.c_str(), &end);
    return !copy.empty() && end == copy.c_str() + copy.size();
}

} // namespace

std::shared_ptr<ChrRecords> ChrRecords::Parse(std::string_view text) {
    std::shared_ptr<ChrRecords> table(new ChrRecords());
    size_t expected = 0;
    for (std::string_view line : Split(text, '\n')) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string_view> fields = Split(line, '\t');
        size_t slot = 0;
        size_t isMale = 0;
        size_t hasHead = 0;
        if (fields.size() != kFieldCount || !ParseNumber(fields[0], slot) || slot != expected ||
            !ParseNumber(fields[3], isMale) || !ParseNumber(fields[4], hasHead)) {
            return nullptr;
        }
        ChrRecord record;
        if (!ParseFloat(fields[5], record.scale) || !ParseFloat(fields[6], record.pov) ||
            !ParseFloat(fields[7], record.radius)) {
            return nullptr;
        }
        for (std::string_view name : Split(fields[1], ',')) {
            record.identifiers.emplace_back(name);
        }
        record.modelPath = "models/chr/" + std::string(fields[2]);
        record.isMale = isMale != 0;
        record.hasHead = hasHead != 0;
        table->records_.push_back(std::move(record));
        expected++;
    }
    return table;
}

std::shared_ptr<ChrHats> ChrHats::Parse(std::string_view text) {
    std::shared_ptr<ChrHats> table(new ChrHats());
    size_t expected = 0;
    for (std::string_view line : Split(text, '\n')) {
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string_view> fields = Split(line, '\t');
        size_t index = 0;
        HatPlacement hat;
        if (fields.size() != kHatFieldCount || !ParseNumber(fields[0], index) || index != expected ||
            !ParseFloat(fields[1], hat.xoffset) || !ParseFloat(fields[2], hat.yoffset) ||
            !ParseFloat(fields[3], hat.zoffset) || !ParseFloat(fields[4], hat.xsize) ||
            !ParseFloat(fields[5], hat.ysize) || !ParseFloat(fields[6], hat.zsize)) {
            return nullptr;
        }
        table->entries_.push_back(hat);
        expected++;
    }
    return table;
}

int ChrRecords::Slot(std::string_view identifier) const {
    for (size_t slot = 0; slot < records_.size(); slot++) {
        for (const std::string& name : records_[slot].identifiers) {
            if (name == identifier) {
                return static_cast<int>(slot);
            }
        }
    }
    return -1;
}

const ChrRecord* ChrRecords::Find(std::string_view identifier) const {
    return At(Slot(identifier));
}

} // namespace Frigate::Game
