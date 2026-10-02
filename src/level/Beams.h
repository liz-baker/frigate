#pragma once

#include <cstdint>
#include <functional>

#include "level/Matrix.h"

// libultraship's display list command.
union Gfx;

namespace Frigate::Level {

// BeamRecord: the bullet beam a gun shows for a shot, from the gun to where the
// bullet ended.
struct BeamRecord {
    // unk00: negative for no beam, else the ticks the beam has been alive.
    int8_t state = -1;
    // item_id.
    int8_t item = 0;
    // pos and delta: where the beam starts and its direction (a unit vector).
    Vec3 position{};
    Vec3 delta{};
    // unk1c: how far the beam reaches.
    float length = 0.0f;
    // unk20 and unk24: the length of the beam's streak and the longest it is drawn.
    float streak = 0.0f;
    float drawLength = 0.0f;
    // unk28: how far along the beam the streak's tail is; the streak starts to
    // draw when it passes 0.
    float tail = 0.0f;

    bool Visible() const {
        return state >= 0;
    }
};

using BeamRandomFn = std::function<uint32_t()>;

// Draws a bullet beam (sub_GAME_7F061E18). `zbufferMode` is the z-buffer mode
// its image is selected with.
class BeamDraw {
  public:
    virtual ~BeamDraw() = default;
    virtual Gfx* Beam(Gfx* dl, const BeamRecord& beam, int zbufferMode, const BeamRandomFn& random) = 0;
};

// CapBeamLengthAndDecideIfRendered: aims the beam from `from` to `to` for a
// shot of `item`, and decides from a random number whether this shot shows one.
void StartBeam(BeamRecord* beam, int32_t item, const Vec3& from, const Vec3& to, const BeamRandomFn& random);

// gunAdvanceBeamTimer: moves the streak along the beam, ending the beam when it
// passes the end.
void AdvanceBeam(BeamRecord* beam, int32_t clockTimer, float globalTimerDelta, const BeamRandomFn& random);

} // namespace Frigate::Level
