#pragma once

#include <memory>

#include "level/BgLevel.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Level {

// Reads a level's header, rooms and stan file from the packed archives
// (`bg/<bgName>/header`, `bg/<bgName>/roomNN/...`, `LevelEntry::stanName`).
class ResourceBgSource final : public BgSource {
  public:
    explicit ResourceBgSource(std::shared_ptr<Ship::ResourceManager> resourceManager);

    std::optional<std::vector<uint8_t>> LoadHeader(const LevelEntry& level) override;
    std::optional<RoomGeometry> LoadRoom(const LevelEntry& level, int room, const BgRoomEntry& entry) override;
    std::optional<std::vector<uint8_t>> LoadSetup(const std::string& name) override;
    std::shared_ptr<const StanData> LoadStan(const LevelEntry& level) override;
    std::unique_ptr<HeadMotion> CreateHead() override;

  private:
    std::shared_ptr<Ship::ResourceManager> resourceManager_;
};

} // namespace Frigate::Level
