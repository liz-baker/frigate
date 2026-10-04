#include <algorithm>

#include "gfx/GbiF3dex.h"
#include "level/GunDraw.h"
#include "level/ModelHandle.h"
#include "level/WatchMenu.h"
#include "model/RenderState.h"

namespace Frigate::Level {

namespace {

// The switch-array slots the gun models are addressed by (the same slots the
// first-person gun uses).
namespace Slot {
constexpr size_t Flash = 1;
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
constexpr size_t LastToggle35 = 35;
} // namespace Slot

Mat4 ToMat4(const Mtxf& matrix) {
    Mat4 out{};
    std::copy(&matrix.m[0][0], &matrix.m[0][0] + 16, &out.m[0][0]);
    return out;
}

bool IsEnvironmentMapped(int32_t item) {
    return item == kItemGoldenGun || item == kItemRuger || item == kItemKnife || item == kItemThrowKnife ||
           item == kItemSilverPp7 || item == kItemGoldPp7;
}

} // namespace

Gfx* WatchMenu::DrawItemModel(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, int32_t item,
                              const Mtxf& matrix, int32_t envColour, uint32_t fogColour, const LookAt* lookAt) {
    // set_enviro_fog_for_items_in_solo_watch_menu.
    if (item == kItemTrigger || item == kItemWatchLaser) {
        item = kItemWatchMagnetAttract;
    }

    PlayerWeapons& weapons = *links_.weapons;
    weapons.SetWatchMenuItem(HandRight, item);

    if (!weapons.HandWithoutItem(HandRight) || weapons.HandItem(HandRight) == kItemUnarmed) {
        return dl;
    }
    const WeaponTable& table = weapons.Table();
    if (!weapons.ItemHasModel(item) || table.HasFlag(item, WeaponFlag::HideInMenu)) {
        return dl;
    }

    const ItemStats& stats = table.Item(item == kItemFist ? weapons.FistModel() : item);
    std::shared_ptr<Model::ModelInstance>& cached = models_[item];
    if (cached == nullptr && links_.models != nullptr) {
        cached = links_.models->CreateGun(stats.model);
    }
    if (cached == nullptr) {
        return dl;
    }
    Model::ModelInstance& model = *cached;
    const size_t switchCount = model.SwitchCount();

    std::vector<Mat4> matrices(model.MatrixCount(), IdentityMat4());
    const auto store = [&matrices](size_t index, const Mtxf& value) {
        if (index < matrices.size()) {
            matrices[index] = ToMat4(value);
        }
    };
    const auto origin = [](const ModelNode& node) { return Vec3{ node.origin[0], node.origin[1], node.origin[2] }; };

    // modelInit, then sub_GAME_7F05E978(0) hides the hand and arm toggles (the
    // first-person gun shows them) and sub_GAME_7F05EA94(1) shows the side toggles.
    model.ResetNodeState();
    for (size_t slot = Slot::FirstToggle; slot <= Slot::LastToggle; slot++) {
        model.SetSwitchVisible(slot, false);
    }
    if (switchCount >= Slot::LastToggle35 + 1) {
        model.SetSwitchVisible(Slot::LastToggle35, false);
    }
    if (switchCount >= Slot::SideToggleB + 1) {
        model.SetSwitchVisible(Slot::SideToggleA, true);
        model.SetSwitchVisible(Slot::SideToggleB, true);
    }
    if (model.SwitchNode(Slot::Flash) != nullptr) {
        model.SetSwitchVisible(Slot::Flash, false);
    }

    store(0, matrix);

    if (stats.skeleton == "gun_revolver") {
        if (const ModelNode* cylinder = model.SwitchNode(Slot::Cylinder)) {
            store(3, Multiply(matrix, TranslationMtxf(origin(*cylinder))));
        }
        if (const ModelNode* hammer = model.SwitchNode(Slot::Hammer)) {
            store(4, Multiply(matrix, TranslationMtxf(origin(*hammer))));
        }
    }
    for (const size_t slot : { Slot::Hinge, Slot::Slide }) {
        if (const ModelNode* node = model.SwitchNode(slot)) {
            const int index = model.MatrixIndexOfSwitch(slot);
            if (index >= 0) {
                store(static_cast<size_t>(index), Multiply(matrix, TranslationMtxf(origin(*node))));
            }
        }
    }

    // The shells of the shotguns' holder all show.
    if (switchCount >= Slot::Shells + 1) {
        for (size_t i = 0; i < 5; i++) {
            if (model.SwitchNode(Slot::Shells + i) != nullptr) {
                model.SetSwitchVisible(Slot::Shells + i, true);
            }
            if (model.SwitchNode(Slot::ShellsB + i) != nullptr) {
                model.SetSwitchVisible(Slot::ShellsB + i, true);
            }
        }
    }
    model.PoseWithMatrices(std::move(matrices));

    if (IsEnvironmentMapped(item)) {
        gSPNumLights(dl++, NUMLIGHTS_1);
        gSPLight(dl++, &envmapLight_.l[0], LIGHT_1);
        gSPLight(dl++, &envmapLight_.a, LIGHT_1 + 1);
        gSPLookAt(dl++, lookAt);
    }

    // The screen toggle is cleared when the screen node exists (the game tests
    // slot 16 and sets slot 17).
    if (switchCount >= Slot::ScreenToggle && model.SwitchNode(Slot::Screen) != nullptr &&
        model.SwitchNode(Slot::ScreenToggle) != nullptr) {
        model.SetSwitchVisible(Slot::ScreenToggle, false);
    }

    ModelRenderParams params;
    params.zbufferEnabled = false;
    params.passes = 3;
    params.sortJoints = false;
    if (envColour >= 0xFF) {
        // PROP_TYPE_WEAPON.
        params.propType = kRenderPropChr;
        params.envColour = fogColour;
    } else {
        // PROP_TYPE_PLAYER.
        params.propType = kRenderPropWeapon;
        params.envColour = static_cast<uint32_t>(envColour);
        params.fogColour = fogColour;
    }

    auto draw = Model::BuildJointDraw(rm, { cached }, params);
    if (draw) {
        Model::JointDraw& kept = Frame().draws.emplace_back(std::move(*draw));
        for (const Gfx& command : kept.commands) {
            *dl++ = command;
        }
    }
    return dl;
}

} // namespace Frigate::Level
