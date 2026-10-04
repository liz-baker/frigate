#include "game/menu/WalletBond.h"

#include <utility>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/DisplayList.h"
#include "game/save/SaveData.h"
#include "level/BgApply.h"
#include "model/ModelInstance.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Game::Menu {

namespace {

constexpr const char* kModelPath = "models/prop/PwalletbondZ";
// GFXHIT0_PICS: the switch-array slot of the list holding the pictures.
constexpr size_t kPicturesSlot = 21;
constexpr uint8_t kEndDisplayList = 0xB8;

} // namespace

void DisableAllSwitches(Model::ModelInstance& model) {
    for (size_t slot = 0; slot < model.SwitchCount(); slot++) {
        const ModelNode* node = model.SwitchNode(slot);
        if (node != nullptr && node->type == kSwitchNodeType) {
            model.SetSwitchVisible(slot, false);
        }
    }
}

void SelectBondPicture(Model::ModelInstance& model, int32_t bond) {
    using namespace WalletSwitch;
    if constexpr (Save::kAllBonds) {
        model.SetSwitchVisible(kBrosnan, bond == Save::kBondBrosnan);
        model.SetSwitchVisible(kConnery, bond == Save::kBondConnery);
        model.SetSwitchVisible(kDalton, false);
        model.SetSwitchVisible(kMoore, false);
        model.SetSwitchVisible(kBrosnanCover, bond == Save::kBondBrosnan);
        model.SetSwitchVisible(kConneryCover, bond == Save::kBondConnery);
        model.SetSwitchVisible(kDaltonCover, false);
        model.SetSwitchVisible(kMooreCover, false);
    } else {
        (void)bond;
        model.SetSwitchVisible(kBrosnan, true);
        model.SetSwitchVisible(kConnery, false);
        model.SetSwitchVisible(kDalton, false);
        model.SetSwitchVisible(kMoore, false);
        model.SetSwitchVisible(kBrosnanCover, true);
        model.SetSwitchVisible(kConneryCover, false);
        model.SetSwitchVisible(kDaltonCover, false);
        model.SetSwitchVisible(kMooreCover, false);
    }
}

WalletBond::WalletBond(std::shared_ptr<Ship::ResourceManager> resourceManager)
    : resourceManager_(std::move(resourceManager)) {
}

WalletBond::~WalletBond() = default;

bool WalletBond::Load() {
    if (Loaded()) {
        return true;
    }
    for (auto& instance : instances_) {
        instance = Model::ModelInstance::Create(resourceManager_, kModelPath);
        if (instance == nullptr) {
            CleanUp();
            return false;
        }
        // The folder screens set the wallet's one matrix directly and never run
        // the matrix calculation that applies the root node's origin.
        instance->SetRootOrigin({ 0.0f, 0.0f, 0.0f });
    }

    // bgApplyDynamicCCRMLUT over the picture list, up to its end. The list is
    // shared by all four wallets and by every draw of it.
    const ModelTree& tree = instances_[0]->Tree();
    const int32_t picturesNode = kPicturesSlot < tree.Switches.size() ? tree.Switches[kPicturesSlot] : kNoNode;
    if (picturesNode != kNoNode && tree.Nodes[static_cast<size_t>(picturesNode)].primaryList != kNoList) {
        const ModelNode& node = tree.Nodes[static_cast<size_t>(picturesNode)];
        const std::string path = fmt::format("{}/dl/{:06x}/{:06x}", kModelPath, node.offset, node.primaryList);
        pictures_ = resourceManager_->LoadResource(CRC64(path.c_str()));
        auto list = std::dynamic_pointer_cast<Fast::DisplayList>(pictures_);
        if (list == nullptr) {
            spdlog::error("wallet: no picture list at '{}'", path);
        } else {
            size_t count = 0;
            while (count < list->Instructions.size() && (list->Instructions[count].words.w0 >> 24) != kEndDisplayList) {
                count++;
            }
            Level::ApplyDynamicCCRMLUT(list->Instructions.data(), count, Level::RoomLut::WalletBond);
        }
    }
    return true;
}

void WalletBond::CleanUp() {
    pictures_.reset();
    for (auto& instance : instances_) {
        instance.reset();
    }
}

} // namespace Frigate::Game::Menu
