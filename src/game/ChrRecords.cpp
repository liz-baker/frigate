#include "game/ChrRecords.h"

#include <spdlog/spdlog.h>

#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"

namespace Frigate::Game {

namespace {

constexpr const char* kResourcePath = "chr/records";

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

} // namespace Frigate::Game
