#pragma once

#include "menu/System.h"

// The Controls tab's binding editor, drawn with ImGui over the binding store
// (docs/bindings.md): four port tabs, each with the port's Connected checkbox,
// device, mode, bindings and device settings. What each shows is decided by
// ControlsModel.h and the capture sequencing by BindingFlow.h; this file only
// draws them. Uses System for everything platform-specific, so it builds on
// every platform, the Wii U included.
namespace Frigate::ConfigMenu {

void DrawControlsEditor(System& system);

} // namespace Frigate::ConfigMenu
