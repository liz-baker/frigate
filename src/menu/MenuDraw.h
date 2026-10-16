#pragma once

#include "menu/MenuModel.h"

// ImGui drawing for the model in MenuModel.h.
namespace Frigate::ConfigMenu {

// Draws one widget (and its children, for a container): its control, a
// tooltip, and the disabled state with the reason as the tooltip.
void DrawWidget(Widget& widget, ValueStore& store);

// Draws a sidebar's widgets in its columns.
void DrawSidebar(Sidebar& sidebar, ValueStore& store);

// Shows `text` as the hovered item's tooltip, wrapped to a readable width instead of one line that runs
// off the window.
void ShowTooltip(const char* text);

} // namespace Frigate::ConfigMenu
