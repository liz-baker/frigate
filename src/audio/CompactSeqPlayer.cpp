#include "CompactSeqPlayer.h"

#include <algorithm>
#include <utility>

namespace Frigate::AudioDriver {

namespace {

constexpr uint8_t kDefaultFxMix = 0;
constexpr uint8_t kDefaultPriority = 5;
constexpr uint8_t kVolFull = 127;
constexpr int kDefaultBendRange = 200;
constexpr uint8_t kSustainThreshold = 63;
// The compact sequence player's initial microseconds per tick.
constexpr int32_t kInitialUspt = 488;

// MIDI controllers the player reads.
constexpr uint8_t kCtrlVolume = 0x07;
constexpr uint8_t kCtrlPan = 0x0A;
constexpr uint8_t kCtrlPriority = 0x10;
constexpr uint8_t kCtrlSustain = 0x40;
constexpr uint8_t kCtrlFx1 = 0x5B;

// Times wrap as the original's 32-bit counters do.
MicroTime AddTime(MicroTime a, MicroTime b) {
    return static_cast<MicroTime>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

} // namespace

CompactSeqPlayer::CompactSeqPlayer(Synth* synth, const SeqPlayerConfig& config)
    : mSynth(synth), mMaxEvents(static_cast<size_t>(config.maxEvents)), mMaxChannels(config.maxChannels) {
    // The next event is a bare API event, which starts the voice handler
    // "spinning" on its first call.
    mNextEvent.type = kSeqpApiEvt;

    mChanState.resize(static_cast<size_t>(config.maxChannels));
    for (int i = 0; i < mMaxChannels; i++) {
        mChanState[static_cast<size_t>(i)].instrument = Audio::kNoIndex;
        ResetPerfChanState(i);
    }

    mVoiceStorage.resize(static_cast<size_t>(config.maxVoices));
    for (VoiceState& vs : mVoiceStorage) {
        vs.next = mFreeList;
        mFreeList = &vs;
    }

    mPlayer.handler = [this]() { return VoiceHandler(); };
    mSynth->AddPlayer(&mPlayer);
}

int CompactSeqPlayer::ActiveVoices() const {
    int count = 0;
    for (const VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
        count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Public calls: each posts an event for the handler.

void CompactSeqPlayer::SetSeq(CompactSeq* seq) {
    SeqpEvent evt;
    evt.type = kSeqpSeqEvt;
    evt.seq = seq;
    PostEvent(evt, 0);
}

void CompactSeqPlayer::SetBank(const SeqBank& bank) {
    SeqpEvent evt;
    evt.type = kSeqpBankEvt;
    evt.bank = bank;
    PostEvent(evt, 0);
}

void CompactSeqPlayer::Play() {
    SeqpEvent evt;
    evt.type = kSeqpPlayEvt;
    PostEvent(evt, 0);
}

void CompactSeqPlayer::Stop() {
    SeqpEvent evt;
    evt.type = kSeqpStoppingEvt;
    PostEvent(evt, 0);
}

void CompactSeqPlayer::SetVol(int16_t volume) {
    SeqpEvent evt;
    evt.type = kSeqpVolEvt;
    evt.seqpVol = volume;
    PostEvent(evt, 0);
}

// ---------------------------------------------------------------------------
// Event queue

void CompactSeqPlayer::PostEvent(const SeqpEvent& evt, MicroTime delta) {
    if (mEvents.size() >= mMaxEvents) {
        return;
    }
    const bool postAtEnd = delta == kEvtqEnd;

    auto it = mEvents.begin();
    for (; it != mEvents.end(); ++it) {
        if (delta < it->delta) {
            it->delta -= delta;
            break;
        }
        delta -= it->delta;
    }

    EventItem item;
    item.evt = evt;
    item.delta = (it == mEvents.end() && postAtEnd) ? 0 : delta;
    mEvents.insert(it, item);
}

MicroTime CompactSeqPlayer::NextEvent(SeqpEvent* evt) {
    if (mEvents.empty()) {
        // Only reached when the queue has been overrun with events that do not
        // repost themselves.
        evt->type = kSeqNoEvent;
        return 0;
    }
    *evt = mEvents.front().evt;
    const MicroTime delta = mEvents.front().delta;
    mEvents.pop_front();
    return delta;
}

void CompactSeqPlayer::FlushType(int16_t type) {
    for (auto it = mEvents.begin(); it != mEvents.end();) {
        auto next = std::next(it);
        if (it->evt.type == type) {
            if (next != mEvents.end()) {
                next->delta += it->delta;
            }
            mEvents.erase(it);
        }
        it = next;
    }
}

void CompactSeqPlayer::RepostItem(EventItem item) {
    auto it = mEvents.begin();
    for (; it != mEvents.end(); ++it) {
        if (item.delta < it->delta) {
            it->delta -= item.delta;
            break;
        }
        item.delta -= it->delta;
    }
    mEvents.insert(it, item);
}

// ---------------------------------------------------------------------------
// The synth's callback

MicroTime CompactSeqPlayer::VoiceHandler() {
    do {
        switch (mNextEvent.type) {
            case kSeqRefEvt:
                HandleNextSeqEvent();
                break;

            case kSeqpApiEvt: {
                SeqpEvent evt;
                evt.type = kSeqpApiEvt;
                PostEvent(evt, mFrameTime);
                break;
            }

            case kNoteEndEvt: {
                VoiceState* vs = mNextEvent.voice;
                mSynth->StopVoice(&vs->voice);
                mSynth->FreeVoice(&vs->voice);
                UnmapVoice(vs);
                break;
            }

            case kSeqpEnvEvt: {
                VoiceState* vs = mNextEvent.voice;
                if (vs->envPhase == kPhaseAttack) {
                    vs->envPhase = kPhaseDecay;
                }
                const MicroTime delta = mNextEvent.delta;
                vs->envEndTime = AddTime(mCurTime, delta);
                vs->envGain = mNextEvent.vol;
                mSynth->SetVolume(&vs->voice, VsVol(vs), delta);
                break;
            }

            case kSeqpMidiEvt:
            case kCspNoteOffEvt:
                HandleMidiMsg(mNextEvent);
                break;

            case kSeqpMetaEvt:
                HandleMetaMsg(mNextEvent);
                break;

            case kSeqpVolEvt:
                mVol = mNextEvent.seqpVol;
                for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
                    mSynth->SetVolume(&vs->voice, VsVol(vs), VsDelta(vs, mCurTime));
                }
                break;

            case kSeqpPlayEvt:
                if (mState != kSeqPlaying) {
                    mState = kSeqPlaying;
                    PostNextSeqEvent();
                }
                break;

            case kSeqpStopEvt:
                if (mState == kSeqStopping) {
                    for (VoiceState* vs = mAllocHead; vs != nullptr; vs = mAllocHead) {
                        mSynth->StopVoice(&vs->voice);
                        mSynth->FreeVoice(&vs->voice);
                        UnmapVoice(vs);
                    }
                    // The queue keeps running: only the voices and the state
                    // are reset.
                    mState = kSeqStopped;
                }
                break;

            case kSeqpStoppingEvt:
                if (mState == kSeqPlaying) {
                    // A compact sequence's note-offs are generated from its
                    // note-ons rather than stored, so the queued ones go too.
                    FlushType(kSeqRefEvt);
                    FlushType(kCspNoteOffEvt);
                    FlushType(kSeqpMidiEvt);

                    // Voices whose note-end is further off than the kill time
                    // are released early.
                    for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
                        if (VoiceNeedsNoteKill(vs, kSeqKillTime)) {
                            ReleaseVoice(vs, kSeqKillTime);
                        }
                    }

                    mState = kSeqStopping;
                    SeqpEvent evt;
                    evt.type = kSeqpStopEvt;
                    PostEvent(evt, kEvtqEnd);
                }
                break;

            case kSeqpPriorityEvt:
                mChanState[mNextEvent.chan].priority = mNextEvent.priority;
                break;

            case kSeqpSeqEvt:
                mTarget = mNextEvent.seq;
                SetUsptFromTempo(500000.0f);
                if (mBank.linked != nullptr) {
                    InitFromBank();
                }
                break;

            case kSeqpBankEvt:
                mBank = mNextEvent.bank;
                InitFromBank();
                break;

            default:
                break;
        }

        // The original's loop spins forever on an empty queue; the API event
        // keeps it from emptying, so this only bounds a dropped one.
        if (mEvents.empty()) {
            mNextDelta = mFrameTime;
            mNextEvent.type = kSeqNoEvent;
            break;
        }
        mNextDelta = NextEvent(&mNextEvent);
    } while (mNextDelta == 0);

    mCurTime = AddTime(mCurTime, mNextDelta);
    return mNextDelta;
}

// Posts a reference to the sequence's next event, due after the ticks until
// it. Does nothing unless playing a sequence that has events left.
void CompactSeqPlayer::PostNextSeqEvent() {
    if (mState != kSeqPlaying || mTarget == nullptr) {
        return;
    }

    int32_t deltaTicks;
    if (!mTarget->NextDelta(&deltaTicks)) {
        return;
    }

    SeqpEvent evt;
    evt.type = kSeqRefEvt;
    PostEvent(evt, static_cast<MicroTime>(static_cast<uint32_t>(deltaTicks) * static_cast<uint32_t>(mUspt)));
}

void CompactSeqPlayer::HandleNextSeqEvent() {
    if (mTarget == nullptr) {
        return;
    }

    SeqEvent seqEvent;
    mTarget->NextEvent(&seqEvent);
    SeqpEvent evt;
    static_cast<SeqEvent&>(evt) = seqEvent;

    switch (evt.type) {
        case kSeqMidiEvt:
            HandleMidiMsg(evt);
            PostNextSeqEvent();
            break;

        case kTempoEvt:
            HandleMetaMsg(evt);
            PostNextSeqEvent();
            break;

        case kSeqEndEvt: {
            mState = kSeqStopping;
            SeqpEvent stop;
            stop.type = kSeqpStopEvt;
            PostEvent(stop, kEvtqEnd);
            break;
        }

        case kTrackEnd:
        case kCspLoopStart:
        case kCspLoopEnd:
            PostNextSeqEvent();
            break;

        default:
            break;
    }
}

void CompactSeqPlayer::HandleMidiMsg(const SeqpEvent& event) {
    const int status = event.midi.status & kMidiStatusMask;
    const uint8_t chan = event.midi.status & kMidiChannelMask;
    const uint8_t byte1 = event.midi.byte1;
    const uint8_t byte2 = event.midi.byte2;
    const uint8_t key = byte1;
    const uint8_t vel = byte2;

    switch (status) {
        case kMidiNoteOn:
            if (vel != 0) {
                // Notes only start while playing.
                if (mState != kSeqPlaying) {
                    break;
                }

                const SoundView* sound = LookupSoundQuick(key, vel, chan);
                if (sound == nullptr) {
                    break;
                }

                VoiceConfig config;
                config.priority = mChanState[chan].priority;
                config.fxBus = 0;
                config.unityPitch = 0;

                VoiceState* vstate = MapVoice(key, vel, chan);
                if (vstate == nullptr) {
                    break;
                }

                Voice* voice = &vstate->voice;
                mSynth->AllocVoice(voice, config);

                vstate->sound = sound;
                vstate->envPhase = kPhaseAttack;
                if (mChanState[chan].sustain > kSustainThreshold) {
                    vstate->phase = kPhaseSustain;
                } else {
                    vstate->phase = kPhaseNoteOn;
                }

                const int16_t cents =
                    static_cast<int16_t>((key - sound->keyMap->keyBase) * 100 + sound->keyMap->detune);

                vstate->pitch = CentsToRatio(cents);
                vstate->envGain = sound->envelope->attackVolume;
                vstate->envEndTime = AddTime(mCurTime, sound->envelope->attackTime);

                // The music player is configured without tremolo and vibrato
                // oscillators, so a note starts at their defaults.
                vstate->tremolo = static_cast<uint8_t>(static_cast<float>(kVolFull));
                vstate->vibrato = 1.0f;

                const float pitch = vstate->pitch * mChanState[chan].pitchBend * vstate->vibrato;
                const uint8_t fxMix = mChanState[chan].fxMix;
                const uint8_t pan = VsPan(vstate);
                const int16_t vol = VsVol(vstate);
                MicroTime deltaTime = sound->envelope->attackTime;

                mSynth->StartVoiceParams(voice, sound->wave, pitch, vol, pan, fxMix, deltaTime);

                // The envelope's decay follows the attack.
                SeqpEvent evt;
                evt.type = kSeqpEnvEvt;
                evt.voice = vstate;
                evt.vol = sound->envelope->decayVolume;
                evt.delta = sound->envelope->decayTime;
                PostEvent(evt, deltaTime);

                if (event.midi.duration) {
                    // A note with a duration gets its note-off posted now.
                    SeqpEvent off;
                    off.type = kCspNoteOffEvt;
                    off.midi.status = static_cast<uint8_t>(chan | kMidiNoteOff);
                    off.midi.byte1 = key;
                    off.midi.byte2 = 0;
                    deltaTime = static_cast<MicroTime>(static_cast<uint32_t>(mUspt) * event.midi.duration);
                    PostEvent(off, deltaTime);
                }
                break;
            }
            // A note-on with no velocity is a note-off.
            [[fallthrough]];

        case kMidiNoteOff: {
            VoiceState* vstate = LookupVoice(key, chan);
            if (vstate == nullptr) {
                break;
            }

            if (vstate->phase == kPhaseSustain) {
                vstate->phase = kPhaseSustRel;
            } else {
                vstate->phase = kPhaseRelease;
                ReleaseVoice(vstate, vstate->sound->envelope->releaseTime);
            }
            break;
        }

        case kMidiPolyKeyPressure: {
            VoiceState* vstate = LookupVoice(key, chan);
            if (vstate == nullptr) {
                break;
            }
            vstate->velocity = byte2;
            mSynth->SetVolume(&vstate->voice, VsVol(vstate), VsDelta(vstate, mCurTime));
            break;
        }

        case kMidiChannelPressure:
            for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
                if (vs->channel == chan) {
                    vs->velocity = byte1;
                    mSynth->SetVolume(&vs->voice, VsVol(vs), VsDelta(vs, mCurTime));
                }
            }
            break;

        case kMidiControlChange:
            switch (byte1) {
                case kCtrlPan:
                    mChanState[chan].pan = byte2;
                    for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
                        if (vs->channel == chan) {
                            mSynth->SetPan(&vs->voice, VsPan(vs));
                        }
                    }
                    break;

                case kCtrlVolume:
                    mChanState[chan].vol = byte2;
                    for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
                        if (vs->channel == chan && vs->envPhase != kPhaseRelease) {
                            mSynth->SetVolume(&vs->voice, VsVol(vs), VsDelta(vs, mCurTime));
                        }
                    }
                    break;

                case kCtrlPriority:
                    // Voices already sounding keep theirs.
                    mChanState[chan].priority = byte2;
                    break;

                case kCtrlSustain:
                    mChanState[chan].sustain = byte2;
                    for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
                        if (vs->channel == chan && vs->phase != kPhaseRelease) {
                            if (byte2 > kSustainThreshold) {
                                // Pedal down.
                                if (vs->phase == kPhaseNoteOn) {
                                    vs->phase = kPhaseSustain;
                                }
                            } else {
                                // Pedal up.
                                if (vs->phase == kPhaseSustain) {
                                    vs->phase = kPhaseNoteOn;
                                } else if (vs->phase == kPhaseSustRel) {
                                    vs->phase = kPhaseRelease;
                                    ReleaseVoice(vs, vs->sound->envelope->releaseTime);
                                }
                            }
                        }
                    }
                    break;

                case kCtrlFx1:
                    mChanState[chan].fxMix = byte2;
                    for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
                        if (vs->channel == chan) {
                            mSynth->SetFxMix(&vs->voice, byte2);
                        }
                    }
                    break;

                default:
                    break;
            }
            break;

        case kMidiProgramChange: {
            if (mBank.linked == nullptr) {
                break;
            }
            const Audio::Bank& bank = mBank.linked->Bank().banks[mBank.bank];
            if (key < bank.instruments.size() && bank.instruments[key] != Audio::kNoIndex) {
                SetInstChanState(bank.instruments[key], chan);
            }
            break;
        }

        case kMidiPitchBendChange: {
            // A 14-bit value centred on 8192, scaled to the channel's range.
            const int32_t bendVal = ((byte2 << 7) + byte1) - 8192;
            const int32_t cents = (mChanState[chan].bendRange * bendVal) / 8192;
            const float bendRatio = CentsToRatio(cents);
            mChanState[chan].pitchBend = bendRatio;

            for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
                if (vs->channel == chan) {
                    mSynth->SetPitch(&vs->voice, vs->pitch * bendRatio * vs->vibrato);
                }
            }
            break;
        }

        default:
            break;
    }
}

// A tempo change rescales the time left on every queued note-off, since each
// was posted in microseconds at the old tempo.
void CompactSeqPlayer::HandleMetaMsg(const SeqpEvent& event) {
    if (event.tempo.status != kMidiMeta || event.tempo.type != kMidiMetaTempo) {
        return;
    }

    const int32_t oldUspt = mUspt;
    const int32_t tempo = (event.tempo.byte1 << 16) | (event.tempo.byte2 << 8) | event.tempo.byte3;
    SetUsptFromTempo(static_cast<float>(tempo));

    // Pull the note-offs out of the queue with their times made absolute.
    // Each later one is put in front of the ones already pulled, but after the
    // first, which is the order they are reposted in.
    std::vector<EventItem> pulled;
    MicroTime curDelta = 0;
    for (auto it = mEvents.begin(); it != mEvents.end();) {
        auto next = std::next(it);
        curDelta = AddTime(curDelta, it->delta);
        if (it->evt.type == kCspNoteOffEvt) {
            EventItem item = *it;
            mEvents.erase(it);

            const MicroTime absolute = curDelta;
            if (next != mEvents.end()) {
                curDelta = AddTime(curDelta, -item.delta);
                next->delta = AddTime(next->delta, item.delta);
            }
            item.delta = absolute;

            pulled.insert(pulled.empty() ? pulled.begin() : pulled.begin() + 1, item);
        }
        it = next;
    }

    for (EventItem& item : pulled) {
        const uint32_t ticks = oldUspt != 0 ? static_cast<uint32_t>(item.delta / oldUspt) : 0;
        item.delta = static_cast<MicroTime>(ticks * static_cast<uint32_t>(mUspt));
        RepostItem(item);
    }
}

// uspt needs the target's qnpt, so without a target it keeps its initial value.
void CompactSeqPlayer::SetUsptFromTempo(float tempo) {
    if (mTarget != nullptr) {
        mUspt = static_cast<int32_t>(tempo * mTarget->Qnpt());
    } else {
        mUspt = kInitialUspt;
    }
}

// ---------------------------------------------------------------------------
// Voices

VoiceState* CompactSeqPlayer::MapVoice(uint8_t key, uint8_t vel, uint8_t channel) {
    VoiceState* vs = mFreeList;
    if (vs != nullptr) {
        mFreeList = vs->next;
        vs->next = nullptr;

        if (mAllocHead == nullptr) {
            mAllocHead = vs;
        } else {
            mAllocTail->next = vs;
        }
        mAllocTail = vs;

        vs->channel = channel;
        vs->key = key;
        vs->velocity = vel;
    }
    return vs;
}

void CompactSeqPlayer::UnmapVoice(VoiceState* voice) {
    VoiceState* prev = nullptr;
    for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
        if (vs == voice) {
            if (prev != nullptr) {
                prev->next = vs->next;
            } else {
                mAllocHead = vs->next;
            }
            if (vs == mAllocTail) {
                mAllocTail = prev;
            }
            vs->next = mFreeList;
            mFreeList = vs;
            return;
        }
        prev = vs;
    }
}

VoiceState* CompactSeqPlayer::LookupVoice(uint8_t key, uint8_t channel) {
    for (VoiceState* vs = mAllocHead; vs != nullptr; vs = vs->next) {
        if (vs->key == key && vs->channel == channel && vs->phase != kPhaseRelease && vs->phase != kPhaseSustRel) {
            return vs;
        }
    }
    return nullptr;
}

// Binary search over the instrument's sounds, which are ordered by key range
// and then velocity range.
const SoundView* CompactSeqPlayer::LookupSoundQuick(uint8_t key, uint8_t vel, uint8_t chan) const {
    const uint32_t instrument = mChanState[chan].instrument;
    const Audio::Instrument* inst = InstrumentAt(instrument);
    if (inst == nullptr) {
        return nullptr;
    }

    int32_t l = 1;
    int32_t r = static_cast<int32_t>(inst->sounds.size());
    while (r >= l) {
        const int32_t i = (l + r) / 2;
        const SoundView* sound = mBank.linked->FindSound(instrument, static_cast<size_t>(i - 1));
        const Audio::KeyMap* keymap = sound->keyMap;

        if (key >= keymap->keyMin && key <= keymap->keyMax && vel >= keymap->velocityMin &&
            vel <= keymap->velocityMax) {
            return sound;
        }
        if (key < keymap->keyMin || (vel < keymap->velocityMin && key <= keymap->keyMax)) {
            r = i - 1;
        } else {
            l = i + 1;
        }
    }
    return nullptr;
}

// The voice's target volume, from the note's velocity, envelope, the sound's
// own volume and the channel and player volumes.
int16_t CompactSeqPlayer::VsVol(const VoiceState* vs) const {
    uint32_t t1 = (vs->tremolo * vs->velocity * vs->envGain) >> 6;
    const uint32_t t2 = (vs->sound->sampleVolume * mVol * mChanState[vs->channel].vol) >> 14;

    t1 *= t2;
    t1 >>= 15;

    return static_cast<int16_t>(t1);
}

// Time left in the voice's current envelope segment, for re-timing a volume
// change that interrupts it.
MicroTime CompactSeqPlayer::VsDelta(const VoiceState* vs, MicroTime t) const {
    const MicroTime delta = AddTime(vs->envEndTime, -t);
    return delta >= 0 ? delta : kSeqGainChangeTime;
}

uint8_t CompactSeqPlayer::VsPan(const VoiceState* vs) const {
    int32_t tmp = mChanState[vs->channel].pan - kPanCenter + vs->sound->samplePan;
    tmp = std::max(tmp, int32_t(kPanLeft));
    tmp = std::min(tmp, int32_t(kPanRight));
    return static_cast<uint8_t>(tmp);
}

void CompactSeqPlayer::ReleaseVoice(VoiceState* vs, MicroTime deltaTime) {
    // A note still in its attack has an envelope event pending; drop it.
    if (vs->envPhase == kPhaseAttack) {
        for (auto it = mEvents.begin(); it != mEvents.end();) {
            auto next = std::next(it);
            if (it->evt.type == kSeqpEnvEvt && it->evt.voice == vs) {
                if (next != mEvents.end()) {
                    next->delta += it->delta;
                }
                mEvents.erase(it);
            }
            it = next;
        }
    }

    vs->velocity = 0;
    vs->envPhase = kPhaseRelease;
    vs->envGain = 0;
    vs->envEndTime = AddTime(mCurTime, deltaTime);

    // Priority 0 makes the voice the first to be stolen.
    vs->voice.priority = 0;
    mSynth->SetVolume(&vs->voice, 0, deltaTime);
    SeqpEvent evt;
    evt.type = kNoteEndEvt;
    evt.voice = vs;
    PostEvent(evt, deltaTime);
}

// Used when stopping, to find which voices have to be cut short: a voice
// whose note-end falls after `killTime` loses that event (and so needs a
// release), while one that ends sooner is left to.
bool CompactSeqPlayer::VoiceNeedsNoteKill(const VoiceState* vs, MicroTime killTime) {
    MicroTime itemTime = 0;
    for (auto it = mEvents.begin(); it != mEvents.end(); ++it) {
        itemTime = AddTime(itemTime, it->delta);
        if (it->evt.type == kNoteEndEvt && it->evt.voice == vs) {
            if (itemTime > killTime) {
                auto next = std::next(it);
                if (next != mEvents.end()) {
                    next->delta += it->delta;
                }
                mEvents.erase(it);
                return true;
            }
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Channel state

const Audio::Instrument* CompactSeqPlayer::InstrumentAt(uint32_t instrument) const {
    if (mBank.linked == nullptr || instrument >= mBank.linked->Bank().instruments.size()) {
        return nullptr;
    }
    return &mBank.linked->Bank().instruments[instrument];
}

void CompactSeqPlayer::InitFromBank() {
    const Audio::Bank& bank = mBank.linked->Bank().banks[mBank.bank];

    // Every channel starts on the first instrument in the bank.
    uint32_t first = Audio::kNoIndex;
    for (size_t i = 0; i < bank.instruments.size() && first == Audio::kNoIndex; i++) {
        first = bank.instruments[i];
    }

    for (int i = 0; i < mMaxChannels; i++) {
        ResetPerfChanState(i);
        SetInstChanState(first, i);
    }

    // The original resets a channel state one past the last channel here, which
    // is outside its array and changes nothing in use.
    if (bank.percussion != Audio::kNoIndex) {
        SetInstChanState(bank.percussion, 9);
    }
}

void CompactSeqPlayer::SetInstChanState(uint32_t instrument, int chan) {
    const Audio::Instrument* inst = InstrumentAt(instrument);
    if (inst == nullptr) {
        return;
    }
    ChanState& state = mChanState[static_cast<size_t>(chan)];
    state.instrument = instrument;
    state.pan = inst->pan;
    state.vol = inst->volume;
    state.priority = inst->priority;
    state.bendRange = inst->bendRange;
}

void CompactSeqPlayer::ResetPerfChanState(int chan) {
    ChanState& state = mChanState[static_cast<size_t>(chan)];
    state.fxMix = kDefaultFxMix;
    state.pan = kPanCenter;
    state.vol = kVolFull;
    state.priority = kDefaultPriority;
    state.sustain = 0;
    state.bendRange = kDefaultBendRange;
    state.pitchBend = 1.0f;
}

} // namespace Frigate::AudioDriver
