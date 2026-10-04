#include "menu/CVarStore.h"

#include "ship/config/ConsoleVariable.h"
#include "ship/window/gui/Gui.h"

namespace Frigate::ConfigMenu {

CVarStore::CVarStore(std::shared_ptr<Ship::ConsoleVariable> cvars, std::shared_ptr<Ship::Gui> gui)
    : mCvars(std::move(cvars)), mGui(std::move(gui)) {
}

int CVarStore::GetInt(const std::string& name, int fallback) {
    return mCvars->GetInteger(name.c_str(), fallback);
}

void CVarStore::SetInt(const std::string& name, int value) {
    mCvars->SetInteger(name.c_str(), value);
    mGui->SaveConsoleVariablesNextFrame();
}

float CVarStore::GetFloat(const std::string& name, float fallback) {
    return mCvars->GetFloat(name.c_str(), fallback);
}

void CVarStore::SetFloat(const std::string& name, float value) {
    mCvars->SetFloat(name.c_str(), value);
    mGui->SaveConsoleVariablesNextFrame();
}

} // namespace Frigate::ConfigMenu
