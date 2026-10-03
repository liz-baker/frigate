#pragma once

#include <memory>
#include <optional>

#include "gfx/FixedMtx.h"
#include "libultraship/libultra/gbi.h"
#include "model/JointDraw.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game::Menu {

class WalletBond;

// frontSetupMenuBackground: the open wallet filling the screen behind the mode,
// mission, difficulty and briefing screens (walletinst[0] seen from 700 units
// at a quarter of its folder-screen size).
class WalletBackground {
  public:
    explicit WalletBackground(std::shared_ptr<Ship::ResourceManager> resourceManager);

    // The view and projection the screen is drawn through
    // (viSetupCurrentPlayerView).
    void Init();
    // Lets go of the draw built for the last frame.
    void Release();

    // Poses the wallet at `folder`'s place and appends its draw to `dl`; the
    // caller has already chosen which switches show. Does nothing if the wallet
    // model is not loaded.
    Gfx* Draw(Gfx* dl, WalletBond& wallets, int32_t folder);

  private:
    std::shared_ptr<Ship::ResourceManager> resourceManager_;
    // Read by address while the interpreter draws the frame.
    Vp_t viewport_{};
    Mtx::FixedMtx view_{};
    Mtx::FixedMtx projection_{};
    std::optional<Model::JointDraw> draw_;
};

} // namespace Frigate::Game::Menu
