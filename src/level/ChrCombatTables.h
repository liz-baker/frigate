#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Frigate::Level {

// StruckAnim: an animation a hit character plays.
struct StruckAnim {
    std::string anim;
    int flip = 0;
    // A negative end frame plays to the clip's last frame.
    float endFrame = -1.0f;
    float speed = 0.5f;
    // Whether a dying character is thrown back by the shot's force of impact.
    int knockback = 0;
    float thudFrame1 = -1.0f;
    float thudFrame2 = -1.0f;
};

// ChrHitReaction: what a shot on one body part does.
struct HitReaction {
    int hitPart = 0;
    int impactPuffCount = 0;
    float impactPuffSize = 0.0f;
    int backPuffCount = 0;
    float backPuffSize = 0.0f;
    // The part's death and flinch animations; null for none.
    const std::vector<StruckAnim>* death = nullptr;
    const std::vector<StruckAnim>* flinch = nullptr;
};

// weapon_firing_animation_table: one animation a character fires in, with the
// frames of its phases and the limits of its aim.
struct FiringAnim {
    std::string anim;
    // unk04: the frame the turn towards the target is done by.
    float turnEndFrame = 0.0f;
    float turnAnglePerFrame = 0.0f;
    float angleOffset = 0.0f;
    float startFrame = 0.0f;
    float endFrame = -1.0f;
    float shootStartFrame = 0.0f;
    float shootEndFrame = 0.0f;
    float recoilStartFrame = -1.0f;
    float recoilEndFrame = -1.0f;
    float aimStartFrame = 0.0f;
    float aimEndFrame = 0.0f;
    float maxUp = 0.0f;
    float maxDown = 0.0f;
    float maxLeft = 0.0f;
    float maxRight = 0.0f;
    float freeArmFracUp = 0.0f;
    float freeArmFracDown = 0.0f;
};

// firing_anim_struct: what a player's character plays for the others in one
// group and stance. `entry` is the firing table entry (null for an animation
// alone, named by `anim`).
struct MultiFiring {
    const FiringAnim* entry = nullptr;
    std::string anim;
    float speedScale = 0.0f;
    float loopFrame = 0.0f;
    float endFrame = 0.0f;
};

// explosion_death_animation.
struct ExplosionDeath {
    std::string anim;
    int flip = 0;
    float speed = 0.5f;
    float startFrame = 0.0f;
    float unused = 0.0f;
    float thudFrame = 0.0f;
    float endFrame = -1.0f;
};

// The firing animation sets, by stance and by what is held.
enum class FiringSet : int {
    Rifle,
    Pistol,
    Doubles,
    CrouchedRifle,
    CrouchedPistol,
    CrouchedDoubles,
};

// The characters' combat tables (`chr/combat`, docs/formats/goldeneye-chr-combat.md).
class ChrCombatTables {
  public:
    static constexpr int kGroupsPerSet = 32;
    static constexpr int kExplosionDirections = 8;

    // Null when the text is malformed.
    static std::shared_ptr<const ChrCombatTables> Parse(std::string_view text);

    // g_HitReactionTable up to its terminator.
    const std::vector<HitReaction>& Reactions() const {
        return reactions_;
    }
    // The row of the table for a hit part number (the first row when the table
    // has none).
    const HitReaction& ReactionFor(int hitPart) const;
    // The index of that row.
    int ReactionIndexFor(int hitPart) const;
    // death_stagger.
    const std::vector<StruckAnim>& DeathStagger() const;

    // The table of firing animations the group for 1/32 of a turn picks from.
    const std::vector<FiringAnim>& Group(FiringSet set, int slice) const;
    // D_80030078 (the rolls and the one-handed and dual wield firing stances
    // after them) and D_80030660 (the firing walks and runs).
    const std::vector<FiringAnim>& Rolls() const;
    const std::vector<FiringAnim>& Walks() const;
    // The index of `anim` in the table, or -1 when it is not an entry of it.
    static int IndexIn(const std::vector<FiringAnim>& table, const FiringAnim* anim);

    // firing_animation_groups[group][stance]: null when the tables carry none.
    static constexpr int kMultiGroups = 4;
    static constexpr int kMultiStances = 6;
    const MultiFiring* Multi(int group, int stance) const;

    // explosion_animation_table: the explosion deaths a direction picks among.
    const std::vector<int>& ExplosionGroup(int direction) const;
    const ExplosionDeath& ExplosionDeathAt(int index) const;

    // bondwalkItemGetForceOfImpact.
    float ForceOfImpact(int item) const;

  private:
    std::map<std::string, std::vector<StruckAnim>> struck_;
    std::vector<HitReaction> reactions_;
    std::map<std::string, std::vector<FiringAnim>> firing_;
    std::array<std::array<const std::vector<FiringAnim>*, kGroupsPerSet>, 6> groups_{};
    std::array<MultiFiring, kMultiGroups * kMultiStances> multi_{};
    bool hasMulti_ = false;
    std::vector<ExplosionDeath> explosionDeaths_;
    std::vector<std::vector<int>> explosionGroups_;
    std::vector<float> forces_;
};

} // namespace Frigate::Level
