#include "game/ChrRecords.h"

#include <algorithm>
#include <charconv>

#include <spdlog/spdlog.h>

#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"

namespace Frigate::Game {

namespace {

constexpr const char* kResourcePath = "chr/records";
constexpr size_t kFieldCount = 5;

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

} // namespace

std::shared_ptr<ChrRecords> ChrRecords::Load(const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    auto blob = resourceManager->LoadResource<Ship::Blob>(kResourcePath);
    if (blob == nullptr) {
        spdlog::error("chr records: no '{}' in the loaded archives", kResourcePath);
        return nullptr;
    }
    // The blob's storage carries trailing NULs past the text.
    std::string_view text(reinterpret_cast<const char*>(blob->Data.data()), blob->Data.size());
    text = text.substr(0, text.find_last_not_of('\0') + 1);
    auto records = Parse(text);
    if (records == nullptr) {
        spdlog::error("chr records: '{}' is malformed", kResourcePath);
    }
    return records;
}

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

const ChrRecord* ChrRecords::Find(std::string_view identifier) const {
    for (const ChrRecord& record : records_) {
        for (const std::string& name : record.identifiers) {
            if (name == identifier) {
                return &record;
            }
        }
    }
    return nullptr;
}

} // namespace Frigate::Game
