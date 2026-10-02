#include "SoundPlayer.h"

#include <algorithm>

namespace Frigate::AudioDriver {

namespace {

// Pitch shift applied to every sound's key base, in cents.
constexpr int kDefaultSetupPitchShift = -0x1770;

constexpr uint8_t kSoundFlagMaskPlayingOrLooped = 0x12;

// Key map fields the player reads as parameters.
int SoundGroup(const Audio::KeyMap& keyMap) {
    return keyMap.SoundGroup();
}
int SoundFxMix(const Audio::KeyMap& keyMap) {
    return keyMap.SoundFxMix();
}

} // namespace

SoundPlayer::SoundPlayer(Synth* synth, const SoundPlayerConfig& config)
    : mSynth(synth), mMaxEvents(static_cast<size_t>(config.maxEvents)), mMaxSounds(config.maxSounds) {
    mStates.resize(static_cast<size_t>(config.stateCount));
    // Free list in index order.
    for (size_t i = 0; i < mStates.size(); i++) {
        mStates[i].prev = i > 0 ? &mStates[i - 1] : nullptr;
        mStates[i].next = i + 1 < mStates.size() ? &mStates[i + 1] : nullptr;
    }
    mFreeHead = mStates.empty() ? nullptr : &mStates[0];

    for (int i = 0; i < kSfxSlotCount; i++) {
        mSlotVolume[i] = 0x7FFF;
        mSlotNaturalVolume[i] = 0x7FFF;
    }

    mPlayer.handler = [this]() { return VoiceHandler(); };
    mSynth->AddPlayer(&mPlayer);

    // Start answering API events.
    SndEvent evt;
    evt.type = kSndpApi;
    PostEvent(evt, mFrameTime);
    mNextDelta = NextEvent(&mNextEvent);
}

int SoundPlayer::ActiveStates() const {
    int count = 0;
    for (const SoundState* s = mActiveHead; s != nullptr; s = s->next) {
        count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Event queue: a list ordered by time, each item holding its delay after the
// one before it.

void SoundPlayer::PostEvent(const SndEvent& evt, MicroTime delta) {
    if (mEvents.size() >= mMaxEvents) {
        return;
    }

    EventItem item;
    item.evt = evt;
    const bool postAtEnd = delta == 0x7fffffff;

    for (auto it = mEvents.begin();; ++it) {
        if (it == mEvents.end()) {
            item.delta = postAtEnd ? 0 : delta;
            mEvents.insert(it, item);
            return;
        }
        if (delta < it->delta) {
            item.delta = delta;
            it->delta -= delta;
            mEvents.insert(it, item);
            return;
        }
        delta -= it->delta;
    }
}

MicroTime SoundPlayer::NextEvent(SndEvent* evt) {
    if (mEvents.empty()) {
        // The queue ran dry: it overflowed with events that do not repost.
        evt->type = -1;
        return 0;
    }
    *evt = mEvents.front().evt;
    const MicroTime delta = mEvents.front().delta;
    mEvents.pop_front();
    return delta;
}

void SoundPlayer::RemoveEvents(SoundState* state, uint16_t eventMask) {
    for (auto it = mEvents.begin(); it != mEvents.end();) {
        if (it->evt.state == state && (static_cast<uint16_t>(it->evt.type) & eventMask) != 0) {
            auto next = std::next(it);
            if (next != mEvents.end()) {
                next->delta += it->delta;
            }
            it = mEvents.erase(it);
        } else {
            ++it;
        }
    }
}

MicroTime SoundPlayer::VoiceHandler() {
    do {
        switch (mNextEvent.type) {
            case kSndpApi: {
                SndEvent evt;
                evt.type = kSndpApi;
                PostEvent(evt, mFrameTime);
                break;
            }
            default:
                HandleEvent(&mNextEvent);
                break;
        }

        mNextDelta = NextEvent(&mNextEvent);
    } while (mNextDelta == 0);

    mCurTime += mNextDelta;
    return mNextDelta;
}

// ---------------------------------------------------------------------------
// Sound states

SoundState* SoundPlayer::SetupSound(const SoundView* sound) {
    const Audio::KeyMap& keymap = *sound->keyMap;
    SoundState* state = mFreeHead;
    if (state == nullptr) {
        return nullptr;
    }

    mFreeHead = state->next;
    if (mFreeHead != nullptr) {
        mFreeHead->prev = nullptr;
    }

    // Link at the head of the active list.
    if (mActiveHead != nullptr) {
        state->next = mActiveHead;
        state->prev = nullptr;
        mActiveHead->prev = state;
        mActiveHead = state;
    } else {
        state->prev = nullptr;
        state->next = nullptr;
        mActiveHead = state;
        mActiveTail = state;
    }

    const bool decayTimeFlag = sound->envelope->decayTime == -1;
    state->priority = static_cast<uint8_t>(decayTimeFlag + 0x40);

    state->playingState = kSoundStateInit;
    state->retries = 2;
    state->sound = sound;
    state->pitchBend = 1.0f;
    state->flags = keymap.keyMax & 0xf0;
    state->chained = nullptr;

    if (state->flags & 0x20) {
        state->pitchBase = CentsToRatio(keymap.keyBase * 100 + kDefaultSetupPitchShift);
    } else {
        state->pitchBase = CentsToRatio((keymap.keyBase * 100 + keymap.detune) + kDefaultSetupPitchShift);
    }

    if (decayTimeFlag) {
        state->flags |= 2;
    }

    state->fxMix = 0;
    state->pan = kPanCenter;
    state->vol = 0x7fff;
    return state;
}

void SoundPlayer::UnlinkClearSound(SoundState* state) {
    if (state == mActiveHead) {
        mActiveHead = state->next;
    }
    if (state == mActiveTail) {
        mActiveTail = state->prev;
    }

    if (state->next) {
        state->next->prev = state->prev;
    }
    if (state->prev) {
        state->prev->next = state->next;
    }

    // Back to the head of the free list.
    if (mFreeHead != nullptr) {
        state->next = mFreeHead;
        state->prev = nullptr;
        mFreeHead->prev = state;
        mFreeHead = state;
    } else {
        state->prev = nullptr;
        state->next = nullptr;
        mFreeHead = state;
    }

    if (state->flags & kSoundPlaying) {
        mAllocatedVoices--;
    }

    state->playingState = kSoundStateNone;

    if (state->chained != nullptr) {
        if (state == *state->chained) {
            *state->chained = nullptr;
        }
        state->chained = nullptr;
    }
}

void SoundPlayer::DisposeSound(SoundState* state) {
    if (state->flags & kSoundPlaying) {
        mSynth->StopVoice(&state->voice);
        mSynth->FreeVoice(&state->voice);
    }

    UnlinkClearSound(state);
    RemoveEvents(state, 0xffff);
}

void SoundPlayer::CreatePitchEvent(SoundState* state) {
    SndEvent evt;
    const float pitch = CentsToRatio(state->sound->keyMap->detune) * state->pitchBend;
    evt.state = state;
    evt.type = kSndpPitch;
    evt.arg.f = pitch;
    PostEvent(evt, kDelta33Ms);
}

// Volume a sound is held at while it plays, given the envelope volume `level`
// (the attack or decay volume).
static int SoundVolume(const int16_t* slotVolume, const SoundView& sound, int level, int vol) {
    return std::max(
        0,
        slotVolume[SoundGroup(*sound.keyMap)] * (level * vol * sound.sampleVolume / 16129) / kSndpGroupVolumeMax - 1);
}

void SoundPlayer::HandleEvent(SndEvent* event) {
    SoundState* soundState = nullptr;
    SoundState* nextState = nullptr;
    SndEvent nextStateEvent;
    bool lastInSequence = true;
    bool isVoiceAllocated = false;
    bool isEventForSingleSound;

    if (event->type < 0 || event->state == nullptr) {
        return;
    }

    do {
        if (nextState != nullptr) {
            nextStateEvent.state = soundState;
            nextStateEvent.type = event->type;
            nextStateEvent.arg.i = event->arg.i;
            event = &nextStateEvent;
        }

        soundState = event->state;
        const SoundView* sound = soundState->sound;

        if (sound == nullptr) {
            return;
        }

        const Audio::KeyMap& keyMap = *sound->keyMap;
        nextState = soundState->next;

        switch (event->type) {
            case kSndpPlay: {
                if (soundState->playingState != kSoundStateInit && soundState->playingState != kSoundStateWaitVoice) {
                    return;
                }

                VoiceConfig config;
                config.fxBus = 0;
                config.priority = soundState->priority;
                config.unityPitch = 0;

                bool limitReached = mMaxSounds <= mAllocatedVoices;

                if (!limitReached || (soundState->flags & kSoundRetrigger)) {
                    // A retriggered sound ignores the limit.
                    isVoiceAllocated = mSynth->AllocVoice(&soundState->voice, config);
                }

                if (!isVoiceAllocated) {
                    // No voice free: wait for another sound to stop.
                    if ((soundState->flags & (kSoundRetrigger | kSoundLooped)) || soundState->retries > 0) {
                        // Looped and retriggered sounds retry every frame.
                        soundState->playingState = kSoundStateWaitVoice;
                        soundState->retries--;
                        PostEvent(*event, kDelta33Ms);
                    } else if (limitReached) {
                        // Retries are spent; see whether a lower-priority sound can be preempted.
                        SoundState* iterState = mActiveTail;

                        do {
                            if (!(iterState->flags & kSoundFlagMaskPlayingOrLooped) &&
                                (iterState->flags & kSoundPlaying) && iterState->playingState != kSoundStatePreempt) {
                                SndEvent interruptEvent;
                                interruptEvent.type = kSndpEnd;
                                interruptEvent.state = iterState;
                                iterState->playingState = kSoundStatePreempt;
                                limitReached = false;
                                PostEvent(interruptEvent, kDelta1Ms);
                                mSynth->SetVolume(&iterState->voice, (soundState->playingState == 1) * 0, kDelta1Ms);
                            }
                            iterState = iterState->prev;
                        } while (limitReached && iterState != nullptr);

                        if (!limitReached) {
                            // Retry once the preempted sound has gone.
                            soundState->retries = 2;
                            PostEvent(*event, kDelta1Ms + 1);
                        } else {
                            DisposeSound(soundState);
                        }
                    } else {
                        DisposeSound(soundState);
                    }
                    return;
                }

                soundState->flags |= kSoundPlaying;
                mSynth->StartVoice(&soundState->voice, sound->wave);
                soundState->playingState = kSoundStatePlaying;
                mAllocatedVoices++;

                MicroTime delta =
                    static_cast<MicroTime>(sound->envelope->attackTime / soundState->pitchBend / soundState->pitchBase);
                int volume = SoundVolume(mSlotVolume, *sound, sound->envelope->attackVolume, soundState->vol);
                mSynth->SetVolume(&soundState->voice, 0, 0);
                mSynth->SetVolume(&soundState->voice, static_cast<int16_t>(volume), delta);

                int pan = std::min(std::max(soundState->pan + sound->samplePan - kPanCenter, kPanLeft), kPanRight);
                mSynth->SetPan(&soundState->voice, static_cast<uint8_t>(pan));

                mSynth->SetPitch(&soundState->voice, soundState->pitchBend * soundState->pitchBase);

                // The mix is scaled by 8 here and in the FX event handler,
                // though its range is already 0 to 127 once added to the key
                // map's four-bit mix.
                int fxMix = (soundState->fxMix + SoundFxMix(keyMap)) * 8;
                fxMix = std::min(127, std::max(0, fxMix));
                mSynth->SetFxMix(&soundState->voice, static_cast<uint8_t>(fxMix));

                SndEvent decay;
                decay.type = kSndpDecay;
                decay.state = soundState;
                PostEvent(decay, static_cast<MicroTime>(sound->envelope->attackTime / soundState->pitchBend /
                                                        soundState->pitchBase));
                break;
            }
            case kSndpStop:
            case kSndpDeactivate:
            case kSndpUnknown12:
                // A sound in release makes this a no-op for the rest of a
                // composite sound, whose later parts have not started; a
                // looped part is processed anyway.
                if (event->type != kSndpUnknown12 || (soundState->flags & kSoundLooped)) {
                    switch (soundState->playingState) {
                        case kSoundStatePlaying: {
                            RemoveEvents(soundState, kSndpDecay);
                            const MicroTime delta = static_cast<MicroTime>(
                                sound->envelope->releaseTime / soundState->pitchBase / soundState->pitchBend);
                            mSynth->SetVolume(&soundState->voice, 0, delta);
                            if (delta != 0) {
                                SndEvent end;
                                end.type = kSndpEnd;
                                end.state = soundState;
                                PostEvent(end, delta);
                                soundState->playingState = kSoundStateStopping;
                            } else {
                                DisposeSound(soundState);
                            }
                            break;
                        }
                        case kSoundStateWaitVoice:
                        case kSoundStateInit:
                            DisposeSound(soundState);
                            break;
                    }
                    if (event->type == kSndpStop) {
                        event->type = kSndpUnknown12;
                    }
                }
                break;
            case kSndpPan:
                soundState->pan = static_cast<uint8_t>(event->arg.i);
                if (soundState->playingState == kSoundStatePlaying) {
                    int pan = std::min(std::max(soundState->pan + sound->samplePan - kPanCenter, kPanLeft), kPanRight);
                    mSynth->SetPan(&soundState->voice, static_cast<uint8_t>(pan));
                }
                break;
            case kSndpPitch:
                soundState->pitchBend = event->arg.f;
                if (soundState->playingState == kSoundStatePlaying) {
                    mSynth->SetPitch(&soundState->voice, soundState->pitchBend * soundState->pitchBase);
                    if (soundState->flags & kSoundPitchSlide) {
                        CreatePitchEvent(soundState);
                    }
                }
                break;
            case kSndpFx:
                soundState->fxMix = static_cast<uint8_t>(event->arg.i);
                if (soundState->playingState == kSoundStatePlaying) {
                    int fxMix = (soundState->fxMix + SoundFxMix(keyMap)) * 8;
                    fxMix = std::min(127, std::max(0, fxMix));
                    mSynth->SetFxMix(&soundState->voice, static_cast<uint8_t>(fxMix));
                }
                break;
            case kSndpVol:
                soundState->vol = static_cast<int16_t>(event->arg.i);
                if (soundState->playingState == kSoundStatePlaying) {
                    const int volume = SoundVolume(mSlotVolume, *sound, sound->envelope->decayVolume, soundState->vol);
                    mSynth->SetVolume(&soundState->voice, static_cast<int16_t>(volume), 1000);
                }
                break;
            case kSndpRelease:
                if (soundState->playingState == kSoundStatePlaying) {
                    const MicroTime delta = static_cast<MicroTime>(sound->envelope->releaseTime /
                                                                   soundState->pitchBase / soundState->pitchBend);
                    const int volume = SoundVolume(mSlotVolume, *sound, sound->envelope->decayVolume, soundState->vol);
                    mSynth->SetVolume(&soundState->voice, static_cast<int16_t>(volume), delta);
                }
                break;
            case kSndpDecay:
                // The voice has reached its attack volume: schedule the
                // release, unless the sound loops.
                if (!(soundState->flags & kSoundLooped)) {
                    const int volume = SoundVolume(mSlotVolume, *sound, sound->envelope->decayVolume, soundState->vol);
                    const MicroTime delta = static_cast<MicroTime>(sound->envelope->decayTime / soundState->pitchBase /
                                                                   soundState->pitchBend);
                    mSynth->SetVolume(&soundState->voice, static_cast<int16_t>(volume), delta);

                    SndEvent stop;
                    stop.type = kSndpStop;
                    stop.state = soundState;
                    PostEvent(stop, delta);

                    // A pitch slide starts once the decay phase does.
                    if (soundState->flags & kSoundPitchSlide) {
                        CreatePitchEvent(soundState);
                    }
                }
                break;
            case kSndpEnd:
                DisposeSound(soundState);
                break;
            case kSndpPlaySfx:
                if (soundState->flags & kSoundRetrigger) {
                    PlaySfx(*event->bank, static_cast<int16_t>(event->soundIndex), soundState->chained);
                }
                break;
            default:
                break;
        }

        soundState = nextState;
        isEventForSingleSound = (event->type & (kSndpPlay | kSndpPitch | kSndpDecay | kSndpEnd | kSndpPlaySfx)) != 0;

        if (soundState != nullptr && !isEventForSingleSound) {
            lastInSequence = (soundState->flags & kSoundFinalInSequence) != 0;
        }

    } while (!lastInSequence && soundState != nullptr && !isEventForSingleSound);
}

// ---------------------------------------------------------------------------
// API

SoundState* SoundPlayer::PlaySfx(const LinkedBank& bank, int16_t soundIndex, SoundState** pending) {
    SoundState* nextState = nullptr;
    int16_t eventSoundIndex = 0;
    MicroTime deltaTotal = 0;
    MicroTime playSfxDelta = 0;
    MicroTime deltaLoop = 0;
    const SoundView* sound = nullptr;
    SoundState* newState = nullptr;

    if (mBootswitchSound) {
        return nullptr;
    }
    if (soundIndex == 0) {
        return nullptr;
    }

    do {
        // The bank's first instrument holds the sounds, indexed from one.
        sound = bank.FindSound(0, static_cast<size_t>(soundIndex - 1));
        if (sound == nullptr) {
            newState = nullptr;
            break;
        }

        newState = SetupSound(sound);

        if (newState != nullptr) {
            SndEvent playEvent;
            playEvent.type = kSndpPlay;
            playEvent.state = newState;
            deltaLoop = sound->keyMap->velocityMax * kDelta33Ms;

            if (newState->flags & kSoundRetrigger) {
                newState->flags &= static_cast<uint8_t>(~kSoundRetrigger);
                PostEvent(playEvent, deltaTotal + 1);
                playSfxDelta = deltaLoop + 1;
                eventSoundIndex = soundIndex;
            } else {
                PostEvent(playEvent, deltaLoop + 1);
            }

            nextState = newState;
        }

        deltaTotal += deltaLoop;

        // The key map names the next sound of a composite.
        const Audio::KeyMap* keyMap = sound->keyMap;
        soundIndex = static_cast<int16_t>(keyMap->velocityMin + ((keyMap->keyMin & 0xC0) * 4));
    } while (soundIndex != 0 && newState != nullptr);

    if (nextState != nullptr) {
        nextState->flags |= kSoundFinalInSequence;
        nextState->chained = pending;

        if (eventSoundIndex != 0) {
            nextState->flags |= kSoundRetrigger;

            SndEvent playSfxEvent;
            playSfxEvent.type = kSndpPlaySfx;
            playSfxEvent.state = nextState;
            playSfxEvent.soundIndex = eventSoundIndex;
            playSfxEvent.bank = &bank;
            PostEvent(playSfxEvent, playSfxDelta);
        }
    }

    if (pending != nullptr) {
        *pending = nextState;
    }

    return nextState;
}

void SoundPlayer::Deactivate(SoundState* state) {
    if (state == nullptr) {
        return;
    }
    SndEvent evt;
    evt.type = kSndpDeactivate;
    evt.state = state;
    state->flags &= static_cast<uint8_t>(~kSoundRetrigger);
    PostEvent(evt, 0);
}

void SoundPlayer::DetachHandle(SoundState** handle) {
    for (SoundState* item = mActiveHead; item != nullptr; item = item->next) {
        if (item->chained == handle) {
            item->chained = nullptr;
        }
    }
}

void SoundPlayer::DeactivateAllByFlag(uint8_t flag) {
    for (SoundState* item = mActiveHead; item != nullptr; item = item->next) {
        SndEvent evt;
        evt.type = kSndpDeactivate;
        evt.state = item;

        if ((item->flags & flag) == flag) {
            item->flags &= static_cast<uint8_t>(~kSoundRetrigger);
            PostEvent(evt, 0);
        }
    }
}

void SoundPlayer::CreatePostEvent(SoundState* state, int16_t eventType, int32_t arg) {
    if (state == nullptr) {
        return;
    }
    SndEvent evt;
    evt.type = static_cast<uint16_t>(eventType);
    evt.state = state;
    evt.arg.i = arg;
    PostEvent(evt, 0);
}

uint8_t SoundPlayer::GetPlayingState(const SoundState* state) const {
    return state != nullptr ? state->playingState : 0;
}

void SoundPlayer::SetPriority(SoundState* state, uint8_t priority) {
    if (state != nullptr) {
        state->priority = priority;
    }
}

void SoundPlayer::SetSfxSlotVolume(uint8_t slot, uint16_t volume) {
    mSlotNaturalVolume[slot] = volume;
    mSlotVolume[slot] = static_cast<int16_t>(static_cast<float>(volume) * mSfxVolumeScale);

    for (SoundState* item = mActiveHead; item != nullptr; item = item->next) {
        if (item->sound != nullptr && SoundGroup(*item->sound->keyMap) == slot) {
            SndEvent evt;
            evt.type = kSndpRelease;
            evt.state = item;
            PostEvent(evt, 0);
        }
    }
}

void SoundPlayer::ApplyVolumeAllSfxSlot(uint16_t volume) {
    for (uint8_t i = 0; i < kSfxSlotCount; i++) {
        SetSfxSlotVolume(i, volume);
    }
}

void SoundPlayer::SetScalerApplyVolumeAllSfxSlot(float scale) {
    mSfxVolumeScale = scale;
    ApplyVolumeAllSfxSlot(GetSfxSlotFirstNaturalVolume());
}

} // namespace Frigate::AudioDriver
