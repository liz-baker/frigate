#include "level/GunDraw.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "level/Matrix.h"
#include "level/ModelHandle.h"
#include "model/RenderState.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

// GUN_SPRING_DAMP and GUN_SPRING_SCALE (US).
constexpr float kSpringDamp = 0.95f;
constexpr float kSpringScale = 0.050000012f;
// IDO_POINT_ONE: the scale the gun models are drawn at.
constexpr float kModelScale = 0.10000001f;
constexpr float kTau = 6.2831855f;
constexpr float kU32Max = 4294967296.0f;
// The sight or hinge angle a watch item reaches when the trigger is held, and
// any other item's (get_value_if_watch_is_on_hand_or_not).
constexpr float kWatchHingeAngle = 0.08726647f;
constexpr float kGunHingeAngle = 0.17453294f;
// The rotations (radians about X, Y, Z) that turn the watch, the taser and the
// fist holding the sniper rifle into place (D_80035C70, D_80035C7C and
// D_80035C88).
constexpr Vec3 kWatchRotation = { 6.2536321f, 6.2592888f, 0.204238f };
constexpr Vec3 kTaserRotation = { 0.25044999f, 0.90482301f, 0.28716999f };
constexpr Vec3 kSniperFistRotation = { 1.715736f, 0.37460899f, 0.92193699f };

// The switch-array slots the gun code addresses by number.
namespace Slot {
constexpr size_t Flash = 1;
constexpr size_t FlashPosition = 2;
constexpr size_t FlashOrigin = 3;
constexpr size_t Kf7Flash = 4;
constexpr size_t Cylinder = 4;
constexpr size_t Hammer = 5;
constexpr size_t Hinge = 6;
constexpr size_t Slide = 7;
constexpr size_t FirstToggle = 8;
constexpr size_t LastToggle = 13;
constexpr size_t SideToggleA = 14;
constexpr size_t SideToggleB = 15;
constexpr size_t Screen = 16;
constexpr size_t ScreenToggle = 17;
constexpr size_t Shells = 18;
constexpr size_t ShellsB = 23;
constexpr size_t HingeAxis = 28;
constexpr size_t Cuffs = 29;
constexpr size_t LastToggle35 = 35;
} // namespace Slot

// CUFF_TYPES.
enum Cuff : int32_t {
    CuffBlue,
    CuffBrosnan,
    CuffJungle,
    CuffBoiler,
    CuffSnow,
    CuffConnery,
    CuffDalton,
    CuffMoore,
    CuffFolder,
};
// BOND_BROSNAN: the folder's Bond (fileGetBondForFolder returns it whatever
// the folder, as the game is built).
constexpr int32_t kFolderBond = 0;
constexpr int32_t kBondConnery = 1;

Mat4 ToMat4(const Mtxf& matrix) {
    Mat4 out{};
    std::copy(&matrix.m[0][0], &matrix.m[0][0] + 16, &out.m[0][0]);
    return out;
}

Mtxf ToMtxf(const Gu::MatrixF& matrix) {
    Mtxf out{};
    std::copy(&matrix[0][0], &matrix[0][0] + 16, &out.m[0][0]);
    return out;
}

Vec3 Origin(const ModelNode& node) {
    return { node.origin[0], node.origin[1], node.origin[2] };
}

void SetPosition(Mtxf* matrix, const Vec3& position) {
    matrix->m[3][0] = position[0];
    matrix->m[3][1] = position[1];
    matrix->m[3][2] = position[2];
}

// matrix_4x4_set_rotation_around_z with the position set.
Mtxf RotationZAt(float angle, const Vec3& position) {
    Mtxf out = RotationZ(angle);
    SetPosition(&out, position);
    return out;
}

// matrix_4x4_align: `angle` is in radians.
Mtxf Align(float angle, float x, float y, float z) {
    Gu::MatrixF mf;
    Gu::AlignF(mf, angle * (360.0f / kTau), x, y, z);
    Mtxf out{};
    std::copy(&mf[0][0], &mf[0][0] + 16, &out.m[0][0]);
    return out;
}

// matrix_4x4_set_rotation_axis_angle.
Mtxf RotationAxisAngle(float angle, float x, float y, float z) {
    const float length = std::sqrt((x * x) + (y * y) + (z * z));
    x /= length;
    y /= length;
    z /= length;
    const float sine = GameSinf(angle);
    const float cosine = GameCosf(angle);
    const float norm = std::sqrt((x * x) + (z * z));
    Mtxf out = IdentityMtxf();
    if (norm == 0.0f) {
        return out;
    }
    const float cosX = x * cosine;
    const float sinX = x * sine;
    const float cosZ = z * cosine;
    const float sinZ = z * sine;
    const float invNorm = 1.0f / norm;
    out.m[0][0] = (-cosZ - (y * sinX)) * invNorm;
    out.m[1][0] = sine * norm;
    out.m[2][0] = (cosX - (y * sinZ)) * invNorm;
    out.m[3][0] = 0.0f;
    out.m[0][1] = (sinZ - (y * cosX)) * invNorm;
    out.m[1][1] = cosine * norm;
    out.m[2][1] = (-sinX - (y * cosZ)) * invNorm;
    out.m[3][1] = 0.0f;
    out.m[0][2] = -x;
    out.m[1][2] = -y;
    out.m[2][2] = -z;
    out.m[3][2] = 0.0f;
    return out;
}

// coord3dCatmullRomInterp.
Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float fraction) {
    const float squared = fraction * fraction;
    const float cubed = fraction * fraction * fraction;
    const float mult0 = squared - 0.5f * (fraction + cubed);
    const float mult1 = 1.5f * cubed - 2.5f * squared + 1.0f;
    const float mult2 = -1.5f * cubed + 2.0f * squared + 0.5f * fraction;
    const float mult3 = 0.5f * (cubed - squared);
    return { mult0 * p0[0] + mult1 * p1[0] + mult2 * p2[0] + mult3 * p3[0],
             mult0 * p0[1] + mult1 * p1[1] + mult2 * p2[1] + mult3 * p3[1],
             mult0 * p0[2] + mult1 * p1[2] + mult2 * p2[2] + mult3 * p3[2] };
}

// randomGetNext() as a fraction of one, the way the muzzle flash code takes it.
float RandomUnit(const PlayerWeapons::RandomFn& random) {
    return static_cast<float>(random()) * (1.0f / kU32Max);
}

// get_value_if_watch_is_on_hand_or_not.
float HingeLimit(const PlayerWeapons& weapons, Hand hand) {
    const int32_t item = weapons.WeaponId(hand);
    return item == kItemTrigger || item == kItemWatchLaser ? kWatchHingeAngle : kGunHingeAngle;
}

// sub_GAME_7F05E6B4: opens the hinge while the trigger is held and closes it
// after.
void TickHinge(const PlayerWeapons& weapons, Hand handNum, HandState& hand, float globalTimerDelta) {
    const float limit = HingeLimit(weapons, handNum);
    if (hand.holdTime != 0) {
        if (hand.hingeAngle < limit) {
            hand.hingeAngle += 0.029088823f * globalTimerDelta;
        }
        if (hand.hingeAngle > limit) {
            hand.hingeAngle = limit;
        }
    } else {
        if (hand.hingeAngle > 0.0f) {
            hand.hingeAngle -= 0.017453294f * globalTimerDelta;
        }
        if (hand.hingeAngle < 0.0f) {
            hand.hingeAngle = 0.0f;
        }
    }
}

// sub_GAME_7F05E83C: the slide is thrown back by a shot and returns as the
// magazine is loaded.
void TickSlide(const PlayerWeapons& weapons, Hand handNum, HandState& hand, float globalTimerDelta) {
    const float recoilBack = weapons.Table().Item(weapons.ItemInHandOrWatchMenu(handNum)).boltRecoilBack;
    if (hand.slideRecoiling) {
        if (hand.slideOffset < recoilBack) {
            hand.slideOffset = hand.slideOffset + (recoilBack * 0.25f * globalTimerDelta);
        }
        if (recoilBack <= hand.slideOffset) {
            hand.slideOffset = recoilBack;
            hand.slideRecoiling = false;
        }
    } else if (hand.ammoInMagazine > 0) {
        if (hand.slideOffset > 0.0f) {
            hand.slideOffset = hand.slideOffset - (recoilBack * 0.16666667f * globalTimerDelta);
        }
        if (hand.slideOffset < 0.0f) {
            hand.slideOffset = 0.0f;
        }
    }
}

} // namespace

// bondviewSelectCuff: shows the sleeve cuff of the Bond the player is.
void SelectCuff(Model::ModelInstance& model, int32_t bondType, size_t firstSlot) {
    const bool folder = bondType == CuffFolder;
    const bool folderIsConnery = kFolderBond == kBondConnery;
    if (model.SwitchNode(firstSlot) != nullptr) {
        model.SetSwitchVisible(firstSlot, bondType == CuffBoiler);
    }
    if (model.SwitchNode(firstSlot + 1) != nullptr) {
        const bool visible =
            bondType == CuffBrosnan || bondType == CuffDalton || bondType == CuffMoore || (folder && !folderIsConnery);
        model.SetSwitchVisible(firstSlot + 1, visible);
    }
    if (model.SwitchNode(firstSlot + 2) != nullptr) {
        model.SetSwitchVisible(firstSlot + 2, bondType == CuffConnery || (folder && folderIsConnery));
    }
    if (model.SwitchNode(firstSlot + 3) != nullptr) {
        model.SetSwitchVisible(firstSlot + 3, bondType == CuffBlue);
    }
    if (model.SwitchNode(firstSlot + 4) != nullptr) {
        model.SetSwitchVisible(firstSlot + 4, bondType == CuffJungle);
    }
    if (model.SwitchNode(firstSlot + 5) != nullptr) {
        model.SetSwitchVisible(firstSlot + 5, bondType == CuffSnow);
    }
}

namespace {

// The items whose materials reflect the environment.
bool IsEnvironmentMapped(int32_t item) {
    return item == kItemGoldenGun || item == kItemRuger || item == kItemKnife || item == kItemThrowKnife ||
           item == kItemSilverPp7 || item == kItemGoldPp7;
}

} // namespace

void GunDraw::AttachMonitors(std::shared_ptr<const MonitorLists> lists,
                             std::function<EffectImageSize(int offset)> imageSize, SelectImageFn selectImage) {
    monitorLists_ = std::move(lists);
    imageSize_ = std::move(imageSize);
    selectImage_ = std::move(selectImage);
    launcherScreen_ = MonitorScreen{};
    taserScreen_ = MonitorScreen{};
    if (monitorLists_) {
        launcherScreen_.StartList(monitorLists_->ListNamed("monAnim34"));
        taserScreen_.StartList(monitorLists_->ListNamed("monAnim35Taser"));
    }
}

void GunDraw::BeginFrame() {
    generation_ ^= 1;
    draws_[generation_].clear();
}

void GunDraw::Update(PlayerWeapons& weapons, const GunFrame& frame) {
    // gunUpdateAndFireBothHands.
    UpdateHand(weapons, HandRight, frame);
    UpdateHand(weapons, HandLeft, frame);
}

void GunDraw::UpdateHand(PlayerWeapons& weapons, Hand handNum, const GunFrame& frame) {
    HandState& hand = weapons.MutableHand(handNum);
    const WeaponTable& table = weapons.Table();
    const int32_t item = weapons.ItemInHandOrWatchMenu(handNum);
    const ItemStats& stats = table.Item(item);
    const float gtd = frame.globalTimerDelta;

    // When switching from a single weapon to dual wielding, both guns move a
    // little to the sides of the screen over four seconds, and back when
    // switching to a single weapon.
    if (handNum == HandRight) {
        if (table.HasFlag(weapons.ItemInHandOrWatchMenu(HandLeft), WeaponFlag::ShowFirstPerson)) {
            hand.dualShift += (2.0f * gtd) / 240.0f;
            if (hand.dualShift > 2.0f) {
                hand.dualShift = 2.0f;
            }
        } else {
            hand.dualShift -= (2.0f * gtd) / 240.0f;
            if (hand.dualShift < 0.0f) {
                hand.dualShift = 0.0f;
            }
        }
    } else if (table.HasFlag(weapons.ItemInHandOrWatchMenu(HandRight), WeaponFlag::ShowFirstPerson)) {
        hand.dualShift -= (2.0f * gtd) / 240.0f;
        if (hand.dualShift < -2.0f) {
            hand.dualShift = -2.0f;
        }
    } else {
        hand.dualShift += (2.0f * gtd) / 240.0f;
        if (hand.dualShift > 0.0f) {
            hand.dualShift = 0.0f;
        }
    }

    // The gun sway: the gun moves in a figure eight that grows with the
    // player's speed.
    const auto blend = static_cast<size_t>(hand.curBlendPos);
    Vec3 blendedPos = CatmullRom(hand.blendPos[(blend + 3) % 4], hand.blendPos[blend], hand.blendPos[(blend + 1) % 4],
                                 hand.blendPos[(blend + 2) % 4], hand.dampt);
    const Vec3 blendedLook = CatmullRom(hand.blendLook[(blend + 3) % 4], hand.blendLook[blend],
                                        hand.blendLook[(blend + 1) % 4], hand.blendLook[(blend + 2) % 4], hand.dampt);
    const Vec3 blendedUp = CatmullRom(hand.blendUp[(blend + 3) % 4], hand.blendUp[blend], hand.blendUp[(blend + 1) % 4],
                                      hand.blendUp[(blend + 2) % 4], hand.dampt);

    blendedPos[0] *= weapons.SwayAmplitude();
    blendedPos[1] *= weapons.SwayAmplitude();
    blendedPos[2] *= weapons.SwayAmplitude();
    blendedPos[0] += hand.thetaDisplacement;
    blendedPos[1] += hand.vertaDisplacement;
    blendedPos[0] += hand.dualShift;

    for (int32_t i = 0; i < frame.clockTimer; i++) {
        for (size_t axis = 0; axis < 3; axis++) {
            hand.springPos[axis] = (kSpringDamp * hand.springPos[axis]) + blendedPos[axis];
            hand.springLook[axis] = (kSpringDamp * hand.springLook[axis]) + blendedLook[axis];
            hand.springUp[axis] = (kSpringDamp * hand.springUp[axis]) + blendedUp[axis];
        }
    }
    for (size_t axis = 0; axis < 3; axis++) {
        hand.swayPos[axis] = hand.springPos[axis] * kSpringScale;
        hand.swayLook[axis] = hand.springLook[axis] * kSpringScale;
        hand.swayUp[axis] = hand.springUp[axis] * kSpringScale;
    }

    // Offset the gun to the right or left of the screen, whichever hand holds it.
    Vec3 gunOffset = { 0.0f, 0.0f, 0.0f };
    const float horizontal = handNum == HandRight ? table.Item(weapons.ItemInHandOrWatchMenu(HandRight)).posX
                                                  : -table.Item(weapons.ItemInHandOrWatchMenu(HandLeft)).posX;
    if (handNum == HandRight) {
        gunOffset[0] = (horizontal + hand.swayPos[0]) + hand.offset2[0];
    } else {
        gunOffset[0] = (horizontal + hand.swayPos[0]) - hand.offset2[0];
    }
    gunOffset[1] = hand.offset2[1] + (stats.posY + hand.swayPos[1]);
    gunOffset[2] = hand.offset2[2] + (stats.posZ + hand.swayPos[2]);

    const float ducking = frame.duckingHeightOffset;
    if (item == kItemRocketLauncher || item == kItemTrigger || item == kItemWatchLaser) {
        gunOffset[1] += ducking / (-100.0f);
        gunOffset[2] += (3.0f * ducking) / (-100.0f);
        if (item == kItemRocketLauncher && frame.wideScreen) {
            gunOffset[1] -= 3.0f;
        }
    } else if (item == kItemTaser) {
        gunOffset[1] += (2.5f * ducking) / (-100.0f);
        gunOffset[2] += (7.5f * ducking) / (-100.0f);
    } else {
        gunOffset[1] += (5.0f * ducking) / (-100.0f);
        gunOffset[2] += (15.0f * ducking) / (-100.0f);
    }

    // The gun shakes while it fires.
    if (hand.firingStatus != 0 && table.HasFlag(item, WeaponFlag::FiringShake)) {
        if (table.HasFlag(item, WeaponFlag::FiringShakeSideways)) {
            gunOffset[0] += 0.3f - (RandomUnit(frame.random) * 0.6f);
        }
        gunOffset[1] += 0.3f - (RandomUnit(frame.random) * 0.6f);
        gunOffset[2] += 0.3f - (RandomUnit(frame.random) * 0.6f);
    }

    // The gun follows the crosshair.
    const GunView& view = frame.view;
    const GunAimState& aim = weapons.Aim();
    const float halfWidth = view.screenWidth * 0.5f;
    const float halfHeight = view.screenHeight * 0.5f;
    gunOffset[0] += (((aim.aimScreenX - view.screenLeft) - halfWidth) * stats.playZ) / halfWidth;
    if ((aim.aimScreenY - view.screenTop) > halfHeight) {
        gunOffset[1] -= (((aim.aimScreenY - view.screenTop) - halfHeight) * stats.playY) / halfHeight;
    } else {
        gunOffset[1] -= (((aim.aimScreenY - view.screenTop) - halfHeight) * stats.playX) / halfHeight;
    }

    Mtxf rotation = IdentityMtxf();
    if (item == kItemTrigger || item == kItemWatchLaser) {
        rotation = MultiplyHomogeneous(RotationXYZ(kWatchRotation), rotation);
    } else if (item == kItemTaser) {
        rotation = MultiplyHomogeneous(RotationXYZ(kTaserRotation), rotation);
    } else if (item == kItemFist && weapons.FistModel() == kItemSniperRifle) {
        rotation = MultiplyHomogeneous(RotationXYZ(kSniperFistRotation), rotation);
        gunOffset[0] += -2.5f;
        gunOffset[1] += 27.8f;
        gunOffset[2] += 2.0f;
    }

    if (hand.hasTransform) {
        gunOffset[0] += hand.transform.m[3][0];
        gunOffset[1] += hand.transform.m[3][1];
        gunOffset[2] += hand.transform.m[3][2];
        rotation = MultiplyHomogeneous(hand.transform, rotation);
        rotation.m[3][0] = 0.0f;
        rotation.m[3][1] = 0.0f;
        rotation.m[3][2] = 0.0f;
    } else {
        // The recoil starts from nothing when no animation offsets the gun.
        hand.recoilAngle = 0.0f;
        hand.recoilOffset = { 0.0f, 0.0f, 0.0f };
    }

    Mtxf tilt = BasisToward(hand.swayLook, hand.swayUp);
    rotation = MultiplyHomogeneous(tilt, rotation);
    tilt = Align(0.0f, gunOffset[0] - hand.aim[0], gunOffset[1] - hand.aim[1], gunOffset[2] - hand.aim[2]);
    rotation = MultiplyHomogeneous(tilt, rotation);
    Mtxf gunMtx = rotation;
    SetPosition(&gunMtx, gunOffset);
    hand.previousWorldMatrix = hand.worldMatrix;
    hand.worldMatrix = MultiplyHomogeneous(frame.viewToWorld, gunMtx);

    // The hand's model is drawn only when the item has a first-person model
    // ready in a hand that is not swapping.
    hand.modelShown = true;
    if (!weapons.ItemHasModel(item) || !table.HasFlag(item, WeaponFlag::ShowFirstPerson) ||
        table.HasFlag(item, WeaponFlag::HideFirstPersonHand) || hand.actionState == GunState::SwitchSwap ||
        hand.actionState == GunState::SwitchHold || !weapons.HandWithoutItem(handNum) ||
        weapons.HandItem(handNum) == kItemUnarmed) {
        hand.modelShown = false;
    }
    if (hand.ammoInMagazine <= 0 && table.HasFlag(item, WeaponFlag::SingleUseReload)) {
        hand.modelShown = false;
    }

    if (hand.modelShown) {
        const std::shared_ptr<Model::ModelInstance>& instance = weapons.HandModel(handNum);
        if (instance == nullptr) {
            hand.modelShown = false;
        } else {
            Model::ModelInstance& model = *instance;
            const size_t switchCount = model.SwitchCount();
            std::vector<Mat4> matrices(model.MatrixCount(), IdentityMat4());
            const auto store = [&matrices](size_t index, const Mtxf& matrix) {
                if (index < matrices.size()) {
                    matrices[index] = ToMat4(matrix);
                }
            };

            // modelInit, sub_GAME_7F05E978 and sub_GAME_7F05EA94.
            model.ResetNodeState();
            for (size_t slot = Slot::FirstToggle; slot <= Slot::LastToggle; slot++) {
                model.SetSwitchVisible(slot, true);
            }
            if (switchCount >= Slot::LastToggle35 + 1) {
                model.SetSwitchVisible(Slot::LastToggle35, true);
            }
            if (switchCount >= Slot::SideToggleB + 1) {
                model.SetSwitchVisible(Slot::SideToggleA, hand.switchesOn);
                model.SetSwitchVisible(Slot::SideToggleB, hand.switchesOn);
            }
            model.lod.zScale = frame.lodScaleZ;

            if (table.HasFlag(item, WeaponFlag::MirrorDual) && handNum == HandLeft) {
                ScaleRowBasis(&gunMtx, 0, -1.0f);
            }
            ScaleRows(&gunMtx, kModelScale);
            store(0, gunMtx);

            const std::string& skeleton = table.Item(item == kItemFist ? weapons.FistModel() : item).skeleton;
            if (skeleton == "gun_revolver") {
                if (const ModelNode* cylinder = model.SwitchNode(Slot::Cylinder)) {
                    float angle = 0.0f;
                    if (item == kItemRuger) {
                        if (hand.actionState == 1) {
                            angle =
                                (static_cast<float>(hand.stateTime - (hand.ammoInMagazine * 6) + 0x1E) * kTau) / 36.0f;
                        } else {
                            angle = (static_cast<float>(6 - hand.ammoInMagazine) * kTau) / 6.0f;
                        }
                    } else if (hand.actionState == 1) {
                        if (hand.stateTime < 6) {
                            angle = (static_cast<float>(hand.stateTime) * kTau) / 36.0f;
                        }
                    }
                    store(3, Multiply(gunMtx, RotationZAt(angle, Origin(*cylinder))));
                }
                if (const ModelNode* hammer = model.SwitchNode(Slot::Hammer)) {
                    float angle = 0.0f;
                    Mtxf local{};
                    if (hand.actionState == 1) {
                        const float thirty = 30.0f * kTau / 360.0f;
                        if (hand.stateTime < 3) {
                            angle = (2.0f * ((-static_cast<float>(hand.stateTime)) * thirty)) / 6.0f;
                        } else {
                            angle = (2.0f * ((-static_cast<float>(6 - hand.stateTime)) * thirty)) / 6.0f;
                        }
                        local = RotationX(angle);
                        SetPosition(&local, Origin(*hammer));
                    } else {
                        local = TranslationMtxf(Origin(*hammer));
                    }
                    store(4, Multiply(gunMtx, local));
                }
            }

            const ModelNode* flashSwitch = model.SwitchNode(Slot::Flash);
            if (flashSwitch != nullptr) {
                model.SetSwitchVisible(Slot::Flash, false);
            }
            if (const ModelNode* flashOrigin = model.SwitchNode(Slot::FlashOrigin)) {
                const float flashScale = (RandomUnit(frame.random) * 0.25f) + 1.0f;
                const float flashExtension = stats.muzzleFlashExtension;
                Mtxf flash;
                if (table.HasFlag(item, WeaponFlag::RotatedFlash)) {
                    flash = RotationZAt(RandomUnit(frame.random) * kTau, Origin(*flashOrigin));
                } else {
                    flash = TranslationMtxf(Origin(*flashOrigin));
                }
                ScaleRows(&flash, flashScale);
                ScaleRowBasis(&flash, 2, flashExtension);
                flash = Multiply(gunMtx, flash);
                store(1, flash);
                // The bullet beam starts at the flash.
                hand.muzzlePosition =
                    TransformVector(frame.viewToWorld, { flash.m[3][0], flash.m[3][1], flash.m[3][2] });
                hand.muzzleDepth = -flash.m[3][2];

                if (hand.flashVisible) {
                    model.SetSwitchVisible(Slot::Flash, true);
                    // The flash's glow at the end of the barrel, and the KF7's
                    // second one.
                    const auto glow = [&](const ModelNode& node, size_t matrixIndex, float scale) {
                        const Vec3 origin = Origin(node);
                        const Vec3 position = {
                            (origin[0] * flash.m[0][0] + origin[1] * flash.m[1][0] + origin[2] * flash.m[2][0]) +
                                flash.m[3][0],
                            (origin[0] * flash.m[0][1] + origin[1] * flash.m[1][1] + origin[2] * flash.m[2][1]) +
                                flash.m[3][1],
                            (origin[0] * flash.m[0][2] + origin[1] * flash.m[1][2] + origin[2] * flash.m[2][2]) +
                                flash.m[3][2]
                        };
                        Mtxf glowMtx = Align(RandomUnit(frame.random) * kTau, -position[0], -position[1], -position[2]);
                        ScaleRows(&glowMtx, scale);
                        Mtxf aimMtx = RotationAxisAngle(0.0f, gunOffset[0] - hand.aim[0], gunOffset[1] - hand.aim[1],
                                                        gunOffset[2] - hand.aim[2]);
                        glowMtx = Multiply(aimMtx, glowMtx);
                        ScaleColumn2(&glowMtx, flashExtension);
                        glowMtx = Multiply(rotation, glowMtx);
                        SetPosition(&glowMtx, position);
                        store(matrixIndex, glowMtx);
                    };
                    if (const ModelNode* position = model.SwitchNode(Slot::FlashPosition)) {
                        glow(*position, 2, kModelScale * flashScale);
                    }
                    if (skeleton == "gun_kf7") {
                        if (const ModelNode* second = model.SwitchNode(Slot::Kf7Flash)) {
                            glow(*second, 3, kModelScale * flashScale);
                        }
                    }
                }
            }

            else {
                // A gun with no flash starts its beam at the gun.
                hand.muzzlePosition = { hand.worldMatrix.m[3][0], hand.worldMatrix.m[3][1], hand.worldMatrix.m[3][2] };
                hand.muzzleDepth = -gunMtx.m[3][2];
            }

            if (const ModelNode* hinge = model.SwitchNode(Slot::Hinge)) {
                const int matrixIndex = model.MatrixIndexOfSwitch(Slot::Hinge);
                TickHinge(weapons, handNum, hand, gtd);
                Mtxf local;
                if (const ModelNode* axis =
                        switchCount >= Slot::HingeAxis + 1 ? model.SwitchNode(Slot::HingeAxis) : nullptr) {
                    // The axis is the interlink node's two points.
                    Gu::MatrixF mf;
                    Gu::RotateF(mf, (((hand.hingeAngle + kTau) - HingeLimit(weapons, handNum)) * 360.0f) / kTau,
                                axis->sortPoint[0] - axis->sortPoint2[0], axis->sortPoint[1] - axis->sortPoint2[1],
                                axis->sortPoint[2] - axis->sortPoint2[2]);
                    std::copy(&mf[0][0], &mf[0][0] + 16, &local.m[0][0]);
                    SetPosition(&local, Origin(*hinge));
                } else {
                    local = RotationY(hand.hingeAngle);
                    SetPosition(&local, Origin(*hinge));
                }
                if (matrixIndex >= 0) {
                    store(static_cast<size_t>(matrixIndex), MultiplyHomogeneous(gunMtx, local));
                }
            }

            if (switchCount >= Slot::Cuffs + 1) {
                SelectCuff(model, frame.bondType, Slot::Cuffs);
            }

            if (const ModelNode* slide = model.SwitchNode(Slot::Slide)) {
                const int matrixIndex = model.MatrixIndexOfSwitch(Slot::Slide);
                TickSlide(weapons, handNum, hand, gtd);
                Mtxf local = TranslationMtxf(Origin(*slide));
                local.m[3][2] -= hand.slideOffset;
                if (matrixIndex >= 0) {
                    store(static_cast<size_t>(matrixIndex), Multiply(gunMtx, local));
                }
            }

            // For the shotguns, the shells in the holder show as many as the
            // player has ammo for.
            if (switchCount >= Slot::Shells + 1) {
                for (size_t i = 0; i < 5; i++) {
                    const bool shown = hand.visibleShells >= static_cast<int32_t>(5 - i);
                    if (model.SwitchNode(Slot::Shells + i) != nullptr) {
                        model.SetSwitchVisible(Slot::Shells + i, shown);
                    }
                    if (model.SwitchNode(Slot::ShellsB + i) != nullptr) {
                        model.SetSwitchVisible(Slot::ShellsB + i, shown);
                    }
                }
            }

            // modelUpdateNodeRelations.
            model.PoseWithMatrices(std::move(matrices));

            if (hand.firingStatus != 0) {
                switch (item) {
                    case kItemPp7:
                    case kItemPp7Silenced:
                    case kItemDd44:
                    case kItemKlobb:
                    case kItemKf7:
                    case kItemZmg:
                    case kItemD5k:
                    case kItemD5kSilenced:
                    case kItemPhantom:
                    case kItemAr33:
                    case kItemRcp90:
                    case kItemSniperRifle:
                    case kItemRuger:
                    case kItemGoldenGun:
                    case kItemSilverPp7:
                    case kItemGoldPp7:
                    case kItemLaser:
                    case kItemWatchLaser:
                        CreateBeam(weapons, handNum, frame);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (item == kItemRocketLauncher && frame.launcher != nullptr) {
        frame.launcher->UpdateAttachedRocket(weapons, handNum, frame);
    }

    if (hand.firingStatus != 0) {
        EjectCasing(weapons, handNum, frame);
        if (frame.launcher != nullptr) {
            frame.launcher->Fire(weapons, handNum, frame);
        }
    }
}

void GunDraw::CreateBeam(PlayerWeapons& weapons, Hand handNum, const GunFrame& frame) const {
    HandState& hand = weapons.MutableHand(handNum);
    const Mtxf& worldToView = frame.worldToView;

    const float depth =
        -((((hand.tracerTarget[0] * worldToView.m[0][2]) + (hand.tracerTarget[1] * worldToView.m[1][2])) +
           (hand.tracerTarget[2] * worldToView.m[2][2])) +
          worldToView.m[3][2]);
    if (depth < hand.muzzleDepth) {
        return;
    }

    StartBeam(&hand.beam, weapons.WeaponId(handNum), hand.muzzlePosition, hand.tracerTarget, frame.random);
    // The beam a second player's character shows is the other player's
    // (getPlayerCount() < 2 ends the function).
}

void GunDraw::EjectCasing(const PlayerWeapons& weapons, Hand handNum, const GunFrame& frame) const {
    const int32_t weapon = weapons.WeaponId(handNum);
    const ItemStats& stats = weapons.Table().Item(weapon);
    if (stats.cartridge.empty() || frame.casings == nullptr) {
        return;
    }
    const HandState& hand = weapons.Hands(handNum);

    CasingEject eject;
    eject.model = stats.cartridge;
    eject.pistol = weapon == kItemPp7 || weapon == kItemPp7Silenced || weapon == kItemDd44 ||
                   weapon == kItemSilverPp7 || weapon == kItemGoldPp7;
    Vec3 origin{};
    if (const auto& instance = weapons.HandModel(handNum)) {
        if (const ModelNode* node = instance->SwitchNode(0)) {
            origin = Origin(*node);
            eject.ejectOrigin = &origin;
        }
    }
    eject.gunMatrix = hand.worldMatrix;
    eject.previousGunMatrix = hand.previousWorldMatrix;
    eject.floorY = frame.floorHeight;
    eject.clockTimer = frame.clockTimer;
    eject.globalTimerDelta = frame.globalTimerDelta;
    frame.casings->Eject(eject, frame.random);
}

Gfx* GunDraw::RenderCasings(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                            const Casings& casings, const Gu::MatrixF& worldToScreen, uint32_t envColour,
                            ModelFactory& models) {
    // The matrix translation range outside which a casing is not drawn.
    constexpr float kMinTranslation = -30000.0f;
    constexpr float kMaxTranslation = 30000.0f;

    for (const Casing& casing : casings.Slots()) {
        if (!casing.InUse()) {
            continue;
        }
        std::shared_ptr<Model::ModelInstance>& model = casingModels_[casing.model];
        if (model == nullptr) {
            model = models.CreateGun(casing.model);
            if (model == nullptr) {
                continue;
            }
        }

        Mtxf placed = casing.rotation;
        ScaleRows(&placed, kModelScale);
        SetPosition(&placed, casing.position);
        const Mtxf view = MultiplyHomogeneous(ToMtxf(worldToScreen), placed);

        bool inRange = true;
        for (size_t axis = 0; axis < 3; axis++) {
            if (view.m[3][axis] > kMaxTranslation || view.m[3][axis] < kMinTranslation) {
                inRange = false;
            }
        }
        if (!inRange) {
            continue;
        }

        std::vector<Mat4> matrices(model->MatrixCount(), IdentityMat4());
        if (!matrices.empty()) {
            matrices[0] = ToMat4(view);
        }
        model->PoseWithMatrices(std::move(matrices));

        ModelRenderParams params;
        params.zbufferEnabled = false;
        params.propType = kRenderPropWeapon;
        params.envColour = envColour;
        params.fogColour = 0;
        params.passes = 3;
        params.sortJoints = false;
        auto draw = Model::BuildJointDraw(resourceManager, { model }, params);
        if (draw) {
            Model::JointDraw& kept = draws_[generation_].emplace_back(std::move(*draw));
            for (const Gfx& command : kept.commands) {
                *dl++ = command;
            }
        }
    }
    return dl;
}

Gfx* GunDraw::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, PlayerWeapons& weapons,
                     const GunFrame& frame) {
    for (const Hand handNum : { HandRight, HandLeft }) {
        HandState& hand = weapons.MutableHand(handNum);
        const int32_t item = weapons.ItemInHandOrWatchMenu(handNum);
        if (!hand.modelShown || weapons.HandModel(handNum) == nullptr) {
            continue;
        }
        const WeaponTable& table = weapons.Table();

        if (item != kItemWatchLaser && beams_ != nullptr) {
            dl = beams_->Beam(dl, hand.beam, 0, frame.random);
        }

        if (IsEnvironmentMapped(item)) {
            gSPNumLights(dl++, NUMLIGHTS_1);
            gSPLight(dl++, &envmapLight_.l[0], LIGHT_1);
            gSPLight(dl++, &envmapLight_.a, LIGHT_1 + 1);
            gSPLookAt(dl++, frame.lookAt);
        }
        // gSPPerspNormalize has no counterpart: the depth normalization is the
        // interpreter's own.

        // The screen of the launcher's sight or the taser, on the model's
        // switch 16 (the launcher's showing the crosshair).
        std::vector<Model::ModelScreen> screens;
        const std::shared_ptr<Model::ModelInstance> held = weapons.HandModel(handNum);
        const ModelTree& tree = held->Tree();
        if (monitorLists_ && imageSize_ && selectImage_ && tree.Switches.size() > Slot::Screen && tree.Switches[Slot::Screen] != kNoNode) {
            held->SetSwitchVisible(Slot::ScreenToggle, true);
            const bool launcher = item == kItemRocketLauncher;
            MonitorScreen& screen = launcher ? launcherScreen_ : taserScreen_;
            if (launcher) {
                screen.image = 0;
                screen.imageOffset = EffectImage::kCrosshair;
            }
            screen.Advance(*monitorLists_, frame.clockTimer, frame.globalTimerDelta, frame.random);
            const int offset =
                screen.imageOffset >= 0 ? screen.imageOffset : EffectImage::kMonitors + (screen.image * EffectImage::kEntrySize);
            const EffectImageSize size = imageSize_(offset);
            if (size.width != 0) {
                const MonitorScreen::Quad quad = screen.MakeQuad(size.width, size.height);
                Model::ModelScreen drawn;
                drawn.screen.slot = Slot::Screen;
                drawn.screen.s = quad.s;
                drawn.screen.t = quad.t;
                drawn.screen.rgba = quad.rgba;
                const int mode = launcher ? 4 : 1;
                drawn.screen.select = [this, offset, mode, intensity = size.format == EffectImage::kFormatIntensity](
                                          std::vector<Gfx>& commands) {
                    std::array<Gfx, 16> buffer;
                    Gfx* end = selectImage_(buffer.data(), offset, mode, 0, intensity);
                    commands.insert(commands.end(), buffer.data(), end);
                };
                screens.push_back(std::move(drawn));
            }
        }

        ModelRenderParams params;
        params.zbufferEnabled = false;
        params.propType = kRenderPropChr;
        params.envColour = weapons.TileColour();
        params.fogColour = 0;
        params.passes = 3;
        params.sortJoints = false;

        // The launcher's rocket is drawn before the launcher. A rocket that was
        // fired is let go of once it has been drawn.
        if (item == kItemRocketLauncher && hand.rocketProp >= 0 && hand.rocketModel) {
            if (auto rocket = Model::BuildJointDraw(resourceManager, { hand.rocketModel }, params)) {
                Model::JointDraw& kept = draws_[generation_].emplace_back(std::move(*rocket));
                for (const Gfx& command : kept.commands) {
                    *dl++ = command;
                }
            }
            if (hand.firedRocket) {
                hand.rocketProp = -1;
                hand.rocketModel.reset();
            }
        }

        const bool mirrored = table.HasFlag(item, WeaponFlag::MirrorDual);
        if (mirrored) {
            *dl++ = F3dex::ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
            params.cullMode = handNum == HandRight ? kCullBack : kCullFront;
        }

        auto draw = Model::BuildJointDraw(resourceManager, { weapons.HandModel(handNum) }, params, screens);
        if (draw) {
            Model::JointDraw& kept = draws_[generation_].emplace_back(std::move(*draw));
            for (const Gfx& command : kept.commands) {
                *dl++ = command;
            }
        }

        if (mirrored) {
            *dl++ = F3dex::ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
        }
        if (item == kItemWatchLaser && beams_ != nullptr) {
            dl = beams_->Beam(dl, hand.beam, 0, frame.random);
        }
    }
    return dl;
}

} // namespace Frigate::Level
