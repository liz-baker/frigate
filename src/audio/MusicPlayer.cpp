#include "MusicPlayer.h"

#include <cstring>
#include <utility>

namespace Frigate::AudioDriver {

namespace {

// Addresses below this are not a sequence's ROM address: the entry has no data.
constexpr uint32_t kRomMusicStartOffset = 0x10000;

// The first bank of the instruments file holds the music's programs.
constexpr uint32_t kMusicBankIndex = 0;

// A float to s32 conversion as the N64's FPU does it: out of range and NaN
// give the largest value.
int32_t TruncToS32(float value) {
    if (!(value > -2147483648.0f && value < 2147483648.0f)) {
        return 0x7FFFFFFF;
    }
    return static_cast<int32_t>(value);
}

// clang-format off
constexpr std::array<int16_t, kNumMusicTracks + 2> kInitialDefaultVolume = {
    0x6665, 0x7332, 0x7332, 0x7998, 0x7332, 0x5998, 0x6665, 0x6665,
    0x6665, 0x6665, 0x7332, 0x6665, 0x7332, 0x6665, 0x6665, 0x7332,
    0x7332, 0x6665, 0x3FFF, 0x6665, 0x6665, 0x3FFF, 0x6665, 0x6665,
    0x6665, 0x6665, 0x6665, 0x7998, 0x6665, 0x5998, 0x6665, 0x6665,
    0x6665, 0x6665, 0x6665, 0x6665, 0x6665, 0x5998, 0x6665, 0x3332,
    0x6665, 0x7332, 0x7332, 0x5998, 0x7332, 0x6665, 0x6665, 0x6665,
    0x6665, 0x6665, 0x6665, 0x6665, 0x6CCB, 0x6665, 0x6665, 0x6665,
    0x6665, 0x6665, 0x6665, 0x6665, 0x7332, 0x6665, 0x7332, 0x7998,
    // The control sequence, which has no track.
    static_cast<int16_t>(0xFFFF),
};
// clang-format on

} // namespace

MusicPlayer::MusicPlayer(Synth* synth, const LinkedBank& instruments, Audio::MusicSequenceTable sequences,
                         const MusicPlayerConfig& config)
    : mSequences(std::move(sequences)), mConfig(config), mDefaultVolume(kInitialDefaultVolume) {
    Audio::MusicSeqFileNew(&mSequences, mSequences.romBase);

    // Each track gets its own player, with the instrument bank set straight
    // away. (The original calls alSeqpSetBank, not alCSPSetBank, on them; both
    // post the same bank event.)
    SeqPlayerConfig seqConfig;
    seqConfig.maxVoices = config.maxVoices;
    seqConfig.maxEvents = config.maxEvents;
    seqConfig.maxChannels = config.maxChannels;

    SeqBank bank;
    bank.linked = &instruments;
    bank.bank = kMusicBankIndex;

    for (Slot& slot : mSlots) {
        slot.player = std::make_unique<CompactSeqPlayer>(synth, seqConfig);
        slot.player->SetBank(bank);
    }
}

bool MusicPlayer::TrackPlay(int slot, int track) {
    if (!ValidSlot(slot) || track < 0 || static_cast<size_t>(track) >= mSequences.Count()) {
        return false;
    }
    if (mBootswitchSound) {
        return true;
    }

    Slot& s = mSlots[static_cast<size_t>(slot)];
    if (s.currentTrack) {
        TrackStop(slot);
    }

    s.currentTrack = track;
    s.playPending = true;
    Update();
    return true;
}

void MusicPlayer::Update() {
    for (int i = 0; i < kMusicSlotCount; i++) {
        Slot& s = mSlots[static_cast<size_t>(i)];
        if (s.playPending && s.player->GetState() == kSeqStopped) {
            s.playPending = false;
            LoadAndPlay(i);
        }
    }
}

void MusicPlayer::LoadAndPlay(int slot) {
    Slot& s = mSlots[static_cast<size_t>(slot)];

    const size_t track = static_cast<size_t>(s.currentTrack);
    if (mSequences.entries[track].address < kRomMusicStartOffset) {
        // No data for this track: play the short solo death jingle instead,
        // unless that is what has none.
        if (s.currentTrack != kMusicShortSoloDeath) {
            TrackPlay(slot, kMusicShortSoloDeath);
        }
        return;
    }

    // The original copies the compressed track out of the ROM and decompresses
    // it into the slot's buffer; the table already holds it decompressed.
    const uint8_t* source = mSequences.Sequence(track);
    s.seqData.assign(source, source + mSequences.SequenceSize(track));

    s.seq.New(s.seqData.data(), s.seqData.size());
    s.player->SetSeq(&s.seq);
    TrackApplySeqpVol(slot, TrackGetVolume(slot));
    s.player->Play();
}

void MusicPlayer::TrackStop(int slot) {
    if (!ValidSlot(slot) || mBootswitchSound) {
        return;
    }
    Slot& s = mSlots[static_cast<size_t>(slot)];

    s.fade = kMusicFadeUnset;
    s.playPending = false;

    if (s.currentTrack != 0) {
        if (s.player->GetState() == kSeqPlaying) {
            s.player->Stop();
        }
    }

    s.currentTrack = 0;
}

uint16_t MusicPlayer::TrackGetVolume(int slot) const {
    return ValidSlot(slot) ? mSlots[static_cast<size_t>(slot)].volume : 0;
}

void MusicPlayer::TrackApplySeqpVol(int slot, uint16_t volume) {
    if (!ValidSlot(slot)) {
        return;
    }
    Slot& s = mSlots[static_cast<size_t>(slot)];

    uint32_t t1 = volume;
    s.volume = static_cast<uint16_t>(t1);

    // The player's volume is the slot's scaled by the track's default, in
    // 15-bit fixed point.
    t1 *= static_cast<uint32_t>(static_cast<int32_t>(mDefaultVolume[static_cast<size_t>(s.currentTrack)]));
    t1 >>= 15;

    s.player->SetVol(static_cast<int16_t>(t1));
}

void MusicPlayer::TrackSaveCurrentVolumeAsTrackDefault(int slot) {
    if (!ValidSlot(slot)) {
        return;
    }
    const Slot& s = mSlots[static_cast<size_t>(slot)];
    mDefaultVolume[static_cast<size_t>(s.currentTrack)] = static_cast<int16_t>(TrackGetVolume(slot));
}

void MusicPlayer::TrackFadeOut(int slot, float seconds) {
    if (!ValidSlot(slot)) {
        return;
    }
    Slot& s = mSlots[static_cast<size_t>(slot)];

    if (s.fade >= kMusicFadeUnset) {
        s.preFadeSavedVolume = TrackGetVolume(slot);
        s.fadeToVolume = 0;
        s.fadeRemainingFrames = TruncToS32(seconds * mConfig.fadeFramerate);
        s.fade = kMusicFadeOut;
    }
}

void MusicPlayer::TrackFadeIn(int slot, float seconds, uint16_t volume) {
    if (!ValidSlot(slot)) {
        return;
    }
    Slot& s = mSlots[static_cast<size_t>(slot)];

    if (s.fade <= kMusicFadeUnset) {
        s.player->Play();

        if (volume == kMusicVolumeSaved) {
            s.fadeToVolume = static_cast<int16_t>(s.preFadeSavedVolume);
        } else {
            s.fadeToVolume = static_cast<int16_t>(volume);
        }

        s.preFadeSavedVolume = 0;
        s.fadeRemainingFrames = TruncToS32(seconds * mConfig.fadeFramerate);
        s.fade = kMusicFadeIn;
    }
}

void MusicPlayer::FadeTick() {
    for (Slot& s : mSlots) {
        FadeTickSlot(s);
    }
}

void MusicPlayer::FadeTickSlot(Slot& s) {
    if (!s.fade) {
        return;
    }
    const int slot = static_cast<int>(&s - mSlots.data());

    uint16_t t0 = TrackGetVolume(slot);
    const int32_t t1 = static_cast<int32_t>(static_cast<uint16_t>(s.fadeToVolume)) - t0;
    const int32_t step = TruncToS32(static_cast<float>(t1) / static_cast<float>(s.fadeRemainingFrames));
    t0 = static_cast<uint16_t>(static_cast<uint32_t>(t0) + static_cast<uint32_t>(step));

    TrackApplySeqpVol(slot, t0);
    s.fadeRemainingFrames--;

    if (s.fadeRemainingFrames <= 0) {
        if (t0 == 0) {
            s.player->Stop();
        }

        s.fadeRemainingFrames = 0;
        s.fade = kMusicFadeUnset;
    }
}

int MusicPlayer::CurrentTrack(int slot) const {
    return ValidSlot(slot) ? mSlots[static_cast<size_t>(slot)].currentTrack : 0;
}

int32_t MusicPlayer::FadeState(int slot) const {
    return ValidSlot(slot) ? mSlots[static_cast<size_t>(slot)].fade : kMusicFadeUnset;
}

int32_t MusicPlayer::SeqState(int slot) const {
    return ValidSlot(slot) ? mSlots[static_cast<size_t>(slot)].player->GetState() : kSeqStopped;
}

int16_t MusicPlayer::TrackDefaultVolume(int track) const {
    return track >= 0 && static_cast<size_t>(track) < mDefaultVolume.size() ? mDefaultVolume[static_cast<size_t>(track)]
                                                                            : 0;
}

} // namespace Frigate::AudioDriver
