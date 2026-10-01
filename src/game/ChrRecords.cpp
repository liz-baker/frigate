#include "game/ChrRecords.h"

#include <spdlog/spdlog.h>

#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"

namespace Frigate::Game {

namespace {

constexpr const char* kResourcePath = "chr/records";
constexpr const char* kHatsPath = "chr/hats";

// The blob's storage carries trailing NULs past the text.
std::string_view BlobText(const Ship::Blob& blob) {
    std::string_view text(reinterpret_cast<const char*>(blob.Data.data()), blob.Data.size());
    return text.substr(0, text.find_last_not_of('\0') + 1);
}

} // namespace

std::shared_ptr<ChrRecords> ChrRecords::Load(const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    auto blob = resourceManager->LoadResource<Ship::Blob>(kResourcePath);
    if (blob == nullptr) {
        spdlog::error("chr records: no '{}' in the loaded archives", kResourcePath);
        return nullptr;
    }
    auto records = Parse(BlobText(*blob));
    if (records == nullptr) {
        spdlog::error("chr records: '{}' is malformed", kResourcePath);
    }
    return records;
}

std::shared_ptr<ChrHats> ChrHats::Load(const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    auto blob = resourceManager->LoadResource<Ship::Blob>(kHatsPath);
    if (blob == nullptr) {
        spdlog::error("chr hats: no '{}' in the loaded archives", kHatsPath);
        return nullptr;
    }
    auto hats = Parse(BlobText(*blob));
    if (hats == nullptr) {
        spdlog::error("chr hats: '{}' is malformed", kHatsPath);
    }
    return hats;
}

} // namespace Frigate::Game
