#pragma once

#include <cstdint>

namespace Frigate::Level {

// The colour the player's view is washed with (colourscreenred, ..., and the
// fade between two such washes) and the fade of the player's own body
// (bondfadetime60 and its fraction).
class ColourScreen {
  public:
    // The reset a life starts with (init_player_BONDdata's colour screen).
    void Reset();

    // currentPlayerSetFadeColour.
    void SetColour(int r, int g, int b, float frac);
    // currentPlayerAdjustFade and currentPlayerSetFadeFrac: a fade over
    // `maxFadeTime` ticks to the colour and fraction.
    void Adjust(float maxFadeTime, int r, int g, int b, float frac);
    void SetFrac(float maxFadeTime, float frac);
    // currentPlayerIsFadeComplete.
    bool IsComplete() const {
        return fadeTimeMax_ < 0.0f;
    }
    // currentPlayerUpdateColourScreenProperties.
    void Update(float globalTimerDelta);

    // currentPlayerStartChrFade and currentPlayerTickChrFade: the body fade
    // starts from `currentAlpha` (the player's chr fadealpha) and `Tick` returns
    // the alpha to give it, or -1 when no fade is running.
    void StartBodyFade(float duration, float targetFrac, uint8_t currentAlpha);
    int TickBodyFade(float globalTimerDelta);

    // colourfadefracnew and colourfadetime60: the fraction the running (or last)
    // fade goes to, and the ticks it has run.
    float FracNew() const {
        return fracNew_;
    }
    float FadeTime() const {
        return fadeTime_;
    }

    int Red() const {
        return red_;
    }
    int Green() const {
        return green_;
    }
    int Blue() const {
        return blue_;
    }
    float Frac() const {
        return frac_;
    }

  private:
    int red_ = 0xFF;
    int green_ = 0xFF;
    int blue_ = 0xFF;
    float frac_ = 0.0f;
    float fadeTime_ = -1.0f;
    float fadeTimeMax_ = -1.0f;
    int redOld_ = 0xFF;
    int redNew_ = 0xFF;
    int greenOld_ = 0xFF;
    int greenNew_ = 0xFF;
    int blueOld_ = 0xFF;
    int blueNew_ = 0xFF;
    float fracOld_ = 0.0f;
    float fracNew_ = 0.0f;

    float bodyTime_ = 0.0f;
    float bodyTimeMax_ = -1.0f;
    float bodyFracOld_ = 0.0f;
    float bodyFracNew_ = 0.0f;
};

} // namespace Frigate::Level
