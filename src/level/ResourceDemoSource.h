#pragma once

#include <memory>

#include "level/AttractDemo.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Level {

// Reads the attract-mode demos from the packed archives (`ramrom/table`,
// `ramrom/<name>`).
class ResourceDemoSource final : public DemoSource {
  public:
    explicit ResourceDemoSource(std::shared_ptr<Ship::ResourceManager> resourceManager);

    std::optional<std::string> LoadTable() override;
    std::optional<std::vector<uint8_t>> LoadDemo(const std::string& name) override;

  private:
    std::shared_ptr<Ship::ResourceManager> resourceManager_;
};

} // namespace Frigate::Level
