#pragma once

#include "game/Sound.h"

namespace Frigate::Game::Sound {

// The `ALSoundState*` a game record keeps for a sound it plays, to cut the
// sound off later. It is handed to PlaySfx as the sound's handle, so the engine
// resets it when the sound ends, and detaches it when the record goes away
// first. A copy of a record does not share its sound.
class Handle {
  public:
    Handle() = default;
    Handle(const Handle&) {
    }
    Handle& operator=(const Handle&) {
        mState = nullptr;
        return *this;
    }
    ~Handle() {
        DetachHandle(&mState);
    }

    // The handle for PlaySfx(sound, handle.Pending()).
    SoundState** Pending() {
        return &mState;
    }
    SoundState* Get() const {
        return mState;
    }
    bool IsNull() const {
        return mState == nullptr;
    }
    // The handle names a sound that has not ended.
    bool IsPlaying() const {
        return mState != nullptr && GetPlayingState(mState) != 0;
    }
    // sndDeactivate for a handle that names a playing sound.
    void StopIfPlaying() {
        if (IsPlaying()) {
            Deactivate(mState);
        }
    }

  private:
    SoundState* mState = nullptr;
};

} // namespace Frigate::Game::Sound
