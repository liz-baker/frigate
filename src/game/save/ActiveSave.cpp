#include "game/save/ActiveSave.h"

namespace Frigate::Game::Save {

namespace {
std::weak_ptr<SaveManager> gActive;
}

std::shared_ptr<SaveManager> ActiveSaveManager() {
    return gActive.lock();
}

void SetActiveSaveManager(const std::shared_ptr<SaveManager>& manager) {
    gActive = manager;
}

} // namespace Frigate::Game::Save
