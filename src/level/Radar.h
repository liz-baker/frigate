#pragma once

#include <array>
#include <cstdint>

#include "level/BgView.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Level {

// One player as the radar sees them.
struct RadarPlayer {
    // The player's prop position, on the floor plane.
    float x = 0.0f;
    float z = 0.0f;
    // bonddead != 0.
    bool dead = false;
    // have_token_or_goldengun: whether the player holds the flag or the golden
    // gun, or which team the player is on.
    int32_t haveTokenOrGoldenGun = 0;
};

// What display_red_blue_on_radar reads.
struct RadarFrame {
    int32_t playerCount = 1;
    // get_cur_playernum().
    int32_t player = 0;
    // The current player's vv_theta, in degrees.
    float theta = 0.0f;
    // mpmenuon, and CHEAT_NO_RADAR_MP.
    bool menuOn = false;
    bool disabled = false;
    // The scenario is one of the team games, Flag Tag or the Golden Gun, which
    // colour the radar by team or by who holds the item.
    bool teamColours = false;
    std::array<RadarPlayer, 4> players{};
};

// A dot on the radar: a 3 by 3 square centred on (x, y) of `colour`
// (0xRRGGBBAA) with a 5 by 5 shadow behind it.
struct RadarBlip {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t colour = 0;
};

// Where the radar's circle and dots fall in the player's view.
struct RadarLayout {
    // False for a solo game, an open menu, a dead player or the cheat.
    bool shown = false;
    // The circle's centre; the current player's own dot sits there.
    int32_t left = 0;
    int32_t top = 0;
    uint32_t selfColour = 0;
    // One dot for each other living player, in player order.
    std::array<RadarBlip, 3> blips{};
    int32_t blipCount = 0;
};

// The circle's centre in a view, which three or more players move for the
// players in the left column.
int32_t RadarLeft(const ViewMetrics& vi, int32_t playerCount, int32_t player);
int32_t RadarTop(const ViewMetrics& vi);

RadarLayout LayoutRadar(const ViewMetrics& vi, const RadarFrame& frame);

// display_red_blue_on_radar.
Gfx* RenderRadar(Gfx* dl, const ViewMetrics& vi, const RadarFrame& frame);

} // namespace Frigate::Level
