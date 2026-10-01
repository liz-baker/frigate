#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "level/Matrix.h"

namespace Frigate::Level {

// One ejected cartridge casing (CasingRecord).
struct Casing {
    // header: the resource base path of the casing's model; empty for a free
    // slot.
    std::string model;
    // floor_y_pos: the casing is gone when it falls below this.
    float floorY = 0.0f;
    Vec3 position{};
    Vec3 velocity{};
    // rot_mtx and rot_velocity_mtx: how the casing is turned and how it spins.
    Mtxf rotation = IdentityMtxf();
    Mtxf spin = IdentityMtxf();

    bool InUse() const {
        return !model.empty();
    }
};

// What the ejection of one casing reads (sub_GAME_7F068508).
struct CasingEject {
    // The casing model the weapon ejects.
    std::string model;
    // The weapon is one of the pistols, whose casings fly lower and slower.
    bool pistol = false;
    // The origin of the first switch node of the hand's model, where the casing
    // leaves from, if the model has one.
    const Vec3* ejectOrigin = nullptr;
    // throw_item_pos_related and throw_item_pos_related_prev.
    Mtxf gunMatrix = IdentityMtxf();
    Mtxf previousGunMatrix = IdentityMtxf();
    // The height of the floor the player stands on.
    float floorY = 0.0f;
    int32_t clockTimer = 1;
    float globalTimerDelta = 1.0f;
};

// The spent cartridges Bond's guns eject (g_Casings).
class Casings {
  public:
    static constexpr size_t kMax = 20;
    using RandomFn = std::function<uint32_t()>;

    // Every slot free (init_ejected_cartridges).
    void Clear();

    // sub_GAME_7F068508: ejects a casing from the gun. Single player only: a
    // multiplayer game ejects none.
    void Eject(const CasingEject& eject, const RandomFn& random);

    // update_bullet_casings.
    void Update(int32_t clockTimer, float globalTimerDelta);

    const std::array<Casing, kMax>& Slots() const {
        return slots_;
    }

  private:
    // casingCreate: claims the first free slot, taking the matrix's position and
    // turn; null if all are used.
    Casing* Create(const std::string& model, const Mtxf& matrix);

    std::array<Casing, kMax> slots_;
};

} // namespace Frigate::Level
