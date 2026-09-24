#pragma once

#include <memory>

#include "level/BgLevel.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Level {

// Reads a level's header and rooms from the packed archives
// (`bg/<bgName>/header`, `bg/<bgName>/roomNN/...`).
class ResourceBgSource final : public BgSource {
  public:
    explicit ResourceBgSource(std::shared_ptr<Ship::ResourceManager> resourceManager);

    std::optional<std::vector<uint8_t>> LoadHeader(const LevelEntry& level) override;
    std::optional<RoomGeometry> LoadRoom(const LevelEntry& level, int room, const BgRoomEntry& entry) override;
    std::optional<std::vector<uint8_t>> LoadSetup(const std::string& name) override;

  private:
    std::shared_ptr<Ship::ResourceManager> resourceManager_;
};

} // namespace Frigate::Level
