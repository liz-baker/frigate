#pragma once

#include <cstdint>

namespace Frigate::Game {
struct FrontState;
}

namespace Frigate::Game::Menu {

// cheat_buttons_mp_related while the title stage runs: the first controller's
// buttons this frame go into the menus' code buffer, and each menu code the
// buffer completes is switched on. `inTitle` is false while a stage runs; the
// buffer is then emptied, as the title's load empties it on the way back.
void TickMenuCheatInput(FrontState& front, uint16_t held, uint16_t pressed, bool inTitle);

// cheatButtonHandleCheatsTurnedOn for the cheats whose codes are typed in the
// menus: the extra multiplayer characters, a cheat unlock and a stage unlock.
void TurnOnMenuCheat(FrontState& front, int32_t cheat);

} // namespace Frigate::Game::Menu
