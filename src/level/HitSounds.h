#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "game/SoundHandle.h"
#include "level/Matrix.h"

namespace Frigate::Level {

// The sounds of a shot or a blow landing (gunfire.c's
// recall_joy2_hits_edit_detail_edit_flag, recall_joy2_hits_edit_flag and
// sub_GAME_7F064720). They share a few handles (g_ImpactSfxStates), so only so
// many play at once.
class HitSounds {
  public:
    using RandomFn = std::function<uint32_t()>;
    // NUM_IMPACT_SFX_STATES.
    static constexpr size_t kStates = 4;

    // recall_joy2_hits_edit_detail_edit_flag: `item` hits a prop at `position`,
    // a character or the viewer when `fleshy`. `surface` is the hit sound type
    // (HitType) of the texture hit, -1 for none.
    void PropHit(int32_t item, const Vec3& position, bool fleshy, int surface, const RandomFn& random);
    // recall_joy2_hits_edit_flag: `item` hits the world at `position`.
    void WorldHit(int32_t item, const Vec3& position, int surface, const RandomFn& random);
    // sub_GAME_7F064720: a bullet breaks glass.
    void GlassHit(const Vec3& position);

  private:
    // gunGetFreeSfxState: the first handle with no sound on it.
    Game::Sound::Handle* FreeState();

    std::array<Game::Sound::Handle, kStates> states_;
};

} // namespace Frigate::Level
