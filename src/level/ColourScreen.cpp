#include "level/ColourScreen.h"

namespace Frigate::Level {

void ColourScreen::Reset() {
    *this = ColourScreen{};
}

void ColourScreen::SetColour(int r, int g, int b, float frac) {
    red_ = r;
    green_ = g;
    blue_ = b;
    frac_ = frac;
}

void ColourScreen::Adjust(float maxFadeTime, int r, int g, int b, float frac) {
    fadeTime_ = 0.0f;
    fadeTimeMax_ = maxFadeTime;
    redOld_ = red_;
    redNew_ = r;
    greenOld_ = green_;
    greenNew_ = g;
    blueOld_ = blue_;
    blueNew_ = b;
    fracOld_ = frac_;
    fracNew_ = frac;
}

void ColourScreen::SetFrac(float maxFadeTime, float frac) {
    Adjust(maxFadeTime, red_, green_, blue_, frac);
}

void ColourScreen::Update(float globalTimerDelta) {
    if (fadeTimeMax_ < 0.0f) {
        return;
    }
    fadeTime_ += globalTimerDelta;

    if (fadeTime_ < fadeTimeMax_) {
        const float mult = fadeTime_ / fadeTimeMax_;
        frac_ = fracOld_ + (fracNew_ - fracOld_) * mult;
        red_ = redOld_ + static_cast<int>(static_cast<float>(redNew_ - redOld_) * mult);
        green_ = greenOld_ + static_cast<int>(static_cast<float>(greenNew_ - greenOld_) * mult);
        blue_ = blueOld_ + static_cast<int>(static_cast<float>(blueNew_ - blueOld_) * mult);
        return;
    }

    frac_ = fracNew_;
    red_ = redNew_;
    green_ = greenNew_;
    blue_ = blueNew_;
    fadeTimeMax_ = -1.0f;
}

void ColourScreen::StartBodyFade(float duration, float targetFrac, uint8_t currentAlpha) {
    bodyTime_ = 0.0f;
    bodyTimeMax_ = duration;
    bodyFracOld_ = static_cast<float>(currentAlpha) / 255.0f;
    bodyFracNew_ = targetFrac;
}

int ColourScreen::TickBodyFade(float globalTimerDelta) {
    if (bodyTimeMax_ < 0.0f) {
        return -1;
    }
    float frac;
    bodyTime_ += globalTimerDelta;

    if (bodyTime_ < bodyTimeMax_) {
        frac = bodyFracOld_ + (bodyFracNew_ - bodyFracOld_) * bodyTime_ / bodyTimeMax_;
    } else {
        frac = bodyFracNew_;
        bodyTimeMax_ = -1.0f;
    }
    return static_cast<uint8_t>(static_cast<int8_t>(static_cast<int>(frac * 255.0f)));
}

} // namespace Frigate::Level
