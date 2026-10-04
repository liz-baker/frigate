#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Ship {
class IResource;
class ResourceManager;
} // namespace Ship

namespace Frigate::Model {
class ModelInstance;
}

namespace Frigate::Game::Menu {

// WALLETBOND_SWITCHNAMES: the wallet model's switch-array slots the folder
// screens show and hide.
namespace WalletSwitch {
constexpr size_t kTabs = 0;
constexpr size_t kPaper = 1;
constexpr size_t kEyesOnly = 2;
constexpr size_t kOhmss = 3;
constexpr size_t kConfidential = 4;
constexpr size_t kClassified = 6;
constexpr size_t kPhotoBond = 7;
constexpr size_t kBlank = 42;
constexpr size_t kPhotoBrief = 12;
constexpr size_t kBrosnan = 8;
constexpr size_t kConnery = 9;
constexpr size_t kDalton = 10;
constexpr size_t kMoore = 11;
constexpr size_t kCover = 13;
constexpr size_t kSlides = 0x13;
constexpr size_t kPics = 0x14;
// The display-list node GFXHIT0_PICS, whose vertices colour the mission select's icons.
constexpr size_t kPicsNode = 0x15;
// SW_BRIEF1, the first of the stages' briefing photographs (LEVEL_SOLO_SEQUENCE order).
constexpr size_t kBrief1 = 22;
constexpr size_t kPhotoCover = 14;
constexpr size_t kBrosnanCover = 15;
constexpr size_t kConneryCover = 16;
constexpr size_t kDaltonCover = 17;
constexpr size_t kMooreCover = 18;
} // namespace WalletSwitch

// disable_all_switches: hides every switch node of the model.
void DisableAllSwitches(Model::ModelInstance& model);

// select_load_bond_picture: shows the photograph of `bond` on the wallet and
// its cover.
void SelectBondPicture(Model::ModelInstance& model, int32_t bond);

// The four wallet models (walletinst) the folder, mode and mission screens
// share. They stay loaded from the file select until the title cycle restarts.
class WalletBond {
  public:
    static constexpr int32_t kCount = 4;

    explicit WalletBond(std::shared_ptr<Ship::ResourceManager> resourceManager);
    ~WalletBond();

    // load_walletbond: instances the models if they are not loaded. False
    // (after logging why) if the model is not in the loaded archives.
    bool Load();
    // frontCleanUpWalletBond.
    void CleanUp();
    bool Loaded() const {
        return instances_[0] != nullptr;
    }
    const std::shared_ptr<Model::ModelInstance>& Instance(int32_t folder) const {
        return instances_[static_cast<size_t>(folder)];
    }

  private:
    std::shared_ptr<Ship::ResourceManager> resourceManager_;
    std::array<std::shared_ptr<Model::ModelInstance>, kCount> instances_;
    // The picture list the combiner patch was applied to, kept loaded so the
    // patch is not lost to a reload.
    std::shared_ptr<Ship::IResource> pictures_;
};

} // namespace Frigate::Game::Menu
