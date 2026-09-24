#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "level/BgHeader.h"
#include "level/Stan.h"

namespace Frigate::Level {

// The values the game uses for the setup file's intro record types
// (INTROTYPE).
enum class IntroType : int32_t {
    Spawn,
    Item,
    Ammo,
    Swirl,
    Anim,
    Cuff,
    Camera,
    Watch,
    Credits,
    End,
};

// PadRecord. On-disk layout: docs/formats/goldeneye-stage-setup.md.
struct Pad {
    Vec3 position{};
    Vec3 up{};
    Vec3 look{};
    // plink: the name of the stand tile the pad sits on.
    std::string tileName;
    // stan: the tile the pad stands on, once StageSetup::ResolveStanTiles has
    // run; kNoTile when no tile holds it.
    int stanTile = kNoTile;
};

// BoundPadRecord: a pad with the box a prop assigned to it fills.
struct BoundPad {
    Pad pad;
    // xmin, xmax, ymin, ymax, zmin, zmax.
    std::array<float, 6> box{};
};

// The intro records. The `demoSlot` of a spawn, item or ammo record says which
// demo it applies to; 0 is normal play.
struct IntroSpawn {
    int32_t pad = 0;
    int32_t demoSlot = 0;
};
struct IntroItem {
    int32_t itemRight = 0;
    // -1 when the item is not a dual pair.
    int32_t itemLeft = -1;
    int32_t demoSlot = 0;
};
struct IntroAmmo {
    int32_t ammoType = 0;
    int32_t amount = 0;
    int32_t demoSlot = 0;
};
struct IntroSwirl {
    int32_t flags = 0;
    // Offset from Bond in centimetres.
    Vec3 offset{};
    float scale = 0;
    float duration = 0;
    // The pad the swirl looks at.
    int32_t pad = 0;
};
struct IntroAnim {
    int32_t animation = 0;
};
struct IntroCuff {
    int32_t bondType = 0;
};
struct IntroCamera {
    Vec3 position{};
    // Radians.
    float yaw = 0;
    float pitch = 0;
    int32_t pad = 0;
    // The caption's text ids (lang1c.lang_index; the game looks up the
    // second) and the optional second caption (lang20; 0 = none).
    std::array<uint16_t, 2> caption{};
    int32_t secondCaption = 0;
};
struct IntroWatch {
    int32_t hours = 0;
    int32_t minutes = 0;
};
struct IntroCredits {
    // Offset of the credits table in the file.
    uint32_t tableOffset = 0;
};

using IntroRecord = std::variant<IntroSpawn, IntroItem, IntroAmmo, IntroSwirl, IntroAnim, IntroCuff, IntroCamera,
                                 IntroWatch, IntroCredits>;

// A stage's setup file, after what proplvreset2 does to it while loading.
class StageSetup {
  public:
    // `file` is the decompressed setup file and `roomScale` the stage's
    // get_room_data_float2(), 1 / level scale. Empty when the file is
    // truncated or its sections run off the end; `error` says why.
    static std::optional<StageSetup> Parse(const std::vector<uint8_t>& file, float roomScale, std::string* error);

    // How init_pathtable_something resolved each pad and bound pad.
    struct StanResolution {
        // The named tile held the pad.
        int named = 0;
        // Another tile was found (the game's debug build prints "changed stan").
        int moved = 0;
        // No tile was found ("has no stan").
        int missing = 0;
    };
    // proplvreset2's init_pathtable_something pass: finds the stan tile of
    // every pad and bound pad.
    StanResolution ResolveStanTiles(Stan& stan);

    // The section offsets the header holds, in header order.
    const std::array<uint32_t, 10>& Header() const {
        return header_;
    }
    const std::vector<Pad>& Pads() const {
        return pads_;
    }
    const std::vector<BoundPad>& BoundPads() const {
        return boundPads_;
    }
    // Empty when the file has no name list.
    const std::vector<std::string>& PadNames() const {
        return padNames_;
    }
    const std::vector<std::string>& BoundPadNames() const {
        return boundPadNames_;
    }
    // The intro section's records in file order, up to the end record.
    const std::vector<IntroRecord>& Intro() const {
        return intro_;
    }

  private:
    std::array<uint32_t, 10> header_{};
    std::vector<Pad> pads_;
    std::vector<BoundPad> boundPads_;
    std::vector<std::string> padNames_;
    std::vector<std::string> boundPadNames_;
    std::vector<IntroRecord> intro_;
};

} // namespace Frigate::Level
