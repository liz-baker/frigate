#pragma once

#include "level/StanData.h"
#include "ship/resource/Resource.h"

// A stage's stan (collision) file ("GEST"), from the packer's
// `obseg/stan/<name>` resource. Payload: docs/formats/goldeneye-stan-resource.md.
namespace Frigate {

class StanResource final : public Ship::Resource<Level::StanData> {
  public:
    using Resource::Resource;

    StanResource();

    Level::StanData* GetPointer() override;
    size_t GetPointerSize() override;

    Level::StanData Data;
};

} // namespace Frigate
