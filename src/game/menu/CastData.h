#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "model/ModelWalk.h"

namespace Frigate::Game::Menu::Cast {

// The cast screen's choices and camera, apart from anything that draws.
// Bodies, heads and text slots are named as the decomp names them; the packed
// `chr/records` table (game/ChrRecords.h) maps a body or head to its model.

using Random = std::function<uint32_t()>;

// What the player's saves say they have completed, as the cast asks it.
// TODO(https://github.com/liz-baker/frigate/issues/115): loaded from the save
// files; until then nothing is completed.
struct Unlocks {
    bool aztecOnSecretAgentOr00 = false;
    bool egyptOn00 = false;
    bool cradle = false;
};

enum class HeadKind {
    Fixed,  // HEAD_FIXED: the body's model carries its head
    Random, // HEAD_RANDOM
    Named,
};

// intro_char_table's entries. `text1` and `text3` are LtitleE slots; slot 227
// is a lone line feed, which draws nothing.
struct Member {
    std::string_view body;
    HeadKind headKind;
    std::string_view head; // only for HeadKind::Named
    int text1;
    int text2;
    int text3;
    // Shown only by the extended cast that starts the game's opening
    // (front.fullActorIntro).
    bool extendedOnly;
};

std::span<const Member> Members();

enum class WeaponType { None, Pistol, Rifle };

// intro_animation_table's entries: `clip` names `animations/<clip>`.
struct Animation {
    std::string_view clip;
    float startFrame;
    float speed;
    WeaponType weapon;
};

std::span<const Animation> Animations();

// Where the character in `index` goes next: the entry after it that the
// screen shows, skipping extended-only entries in the short cast and
// characters whose unlock hasn't been met (the four guest stars and the
// Moonraker Elite). Returns Members().size(), the table's end, if there is
// none. The unlockable guest stars also have a 1 in 10000 chance of showing
// regardless.
size_t NextMember(size_t index, bool extendedCast, const Unlocks& unlocks, const Random& random);

struct BodyAndHead {
    std::string body;
    std::string head; // empty if the body carries its head
};

// The body and head to draw for `member`. Bond, Natalya and Trevelyan (006)
// take a random outfit, and a random head comes from the male or female list
// by `isMale(body)`.
BodyAndHead ChooseBodyAndHead(const Member& member, const std::function<bool(std::string_view)>& isMale,
                              const Random& random);

// The model path of a random weapon of `type`, with the PP7 standing in for
// one the player hasn't unlocked.
std::string_view ChooseWeapon(WeaponType type, const Unlocks& unlocks, const Random& random);

// The camera's start and end distance, angle (radians) and height for one
// character's turn; the camera moves in a straight interpolation between them.
struct CameraPath {
    float distStart = 0.0f;
    float distEnd = 0.0f;
    float angleStart = 0.0f;
    float angleEnd = 0.0f;
    float heightStart = 0.0f;
    float heightEnd = 0.0f;
};

CameraPath ChooseCameraPath(const Random& random);

// The frame length of the NTSC cast, in ticks, and the tick the screen ends.
constexpr int kLength = 60 * 3;
constexpr int kFadeLength = 60 / 2;
constexpr int kEndTimer = 181;

// The alpha, 0 to 1, of the character and text at `timer`: it fades in over
// the first kFadeLength ticks and out over the last.
float Fade(int timer);

using Vec3 = std::array<float, 3>;

// The camera before it follows the character: `pos` and `target` at
// `menuFraction` (the timer over kLength) of the way along `path`.
struct Orbit {
    Vec3 pos;
    Vec3 target;
};

Orbit OrbitCamera(const CameraPath& path, float menuFraction);

// The damped following of the character's root: the camera's position and
// target each trail it through an accumulator that decays a little each tick.
struct RootFollow {
    // The state a new character starts from.
    bool reset = true;
    Vec3 rootPos = { 0.0f, 0.0f, 0.0f };
    Vec3 rootVelAccumulator = { 0.0f, 0.0f, 0.0f };
    Vec3 targetAccumulator = { 0.0f, 0.0f, 0.0f };
};

// What Step needs of the pose: the root's offset, and the model's first
// matrix, posed against an identity base, that the target is carried through.
struct FollowInput {
    Vec3 rootOffset;
    const Frigate::Mat4& firstMatrix;
    int ticks;
    float timerDelta;
};

// One frame's update. Adds the smoothed root position to `orbit.pos`, and the
// smoothed target to `orbit.target`.
void Step(RootFollow& follow, const FollowInput& input, Orbit& orbit);

} // namespace Frigate::Game::Menu::Cast
