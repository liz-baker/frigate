#include "level/ResourceDemoSource.h"

#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"

namespace Frigate::Level {

ResourceDemoSource::ResourceDemoSource(std::shared_ptr<Ship::ResourceManager> resourceManager)
    : resourceManager_(std::move(resourceManager)) {
}

std::optional<std::string> ResourceDemoSource::LoadTable() {
    auto blob = resourceManager_->LoadResource<Ship::Blob>("ramrom/table");
    if (blob == nullptr) {
        return std::nullopt;
    }
    return std::string(blob->Data.begin(), blob->Data.end());
}

std::optional<std::vector<uint8_t>> ResourceDemoSource::LoadDemo(const std::string& name) {
    auto blob = resourceManager_->LoadResource<Ship::Blob>("ramrom/" + name);
    if (blob == nullptr) {
        return std::nullopt;
    }
    return blob->Data;
}

} // namespace Frigate::Level
