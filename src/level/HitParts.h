#pragma once

namespace Frigate::Level {

// HITTARGET: the body parts a shot can hit, and the values a hit node of a
// model names its part by.
namespace HitPart {
constexpr int NullPart = 0;
constexpr int LeftFoot = 1;
constexpr int LeftLeg = 2;
constexpr int LeftThigh = 3;
constexpr int RightFoot = 4;
constexpr int RightLeg = 5;
constexpr int RightThigh = 6;
constexpr int Pelvis = 7;
constexpr int Head = 8;
constexpr int LeftHand = 9;
constexpr int LeftArm = 10;
constexpr int LeftShoulder = 11;
constexpr int RightHand = 12;
constexpr int RightArm = 13;
constexpr int RightShoulder = 14;
constexpr int Chest = 15;
constexpr int Gun = 0x64;
constexpr int Hat = 0x6E;
constexpr int General = 0xC8;
constexpr int GeneralHalf = 0xC9;
} // namespace HitPart

} // namespace Frigate::Level
