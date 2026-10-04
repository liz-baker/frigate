#pragma once

#include <memory>

#include "menu/MenuModel.h"

namespace Ship {
class ConsoleVariable;
class Gui;
} // namespace Ship

namespace Frigate::ConfigMenu {

// Menu values kept in libultraship's console variables, so they persist in
// the config file. Changes schedule a save at the end of the frame.
class CVarStore : public ValueStore {
  public:
    CVarStore(std::shared_ptr<Ship::ConsoleVariable> cvars, std::shared_ptr<Ship::Gui> gui);

    int GetInt(const std::string& name, int fallback) override;
    void SetInt(const std::string& name, int value) override;
    float GetFloat(const std::string& name, float fallback) override;
    void SetFloat(const std::string& name, float value) override;

  private:
    std::shared_ptr<Ship::ConsoleVariable> mCvars;
    std::shared_ptr<Ship::Gui> mGui;
};

} // namespace Frigate::ConfigMenu
