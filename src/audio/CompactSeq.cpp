#include "CompactSeq.h"

namespace Frigate::AudioDriver {

namespace {

constexpr uint32_t kHeaderDivisionOffset = 0x40;

} // namespace

// A read past the data returns 0 rather than running off the buffer, which
// ends the track's events at the next end-of-track.
uint8_t CompactSeq::ByteAt(uint32_t position) const {
    return position < mSize ? mData[position] : 0;
}

void CompactSeq::New(uint8_t* data, size_t size) {
    mData = data;
    mSize = size;
    mValidTracks = 0;
    mLastDeltaTicks = 0;
    mLastTicks = 0;
    mDeltaFlag = 1;

    for (int i = 0; i < kSeqTrackCount; i++) {
        mLastStatus[i] = 0;
        mCurBackupPtr[i] = 0;
        mCurBackupLen[i] = 0;
        const uint32_t base = static_cast<uint32_t>(i) * 4;
        const uint32_t offset = (uint32_t(ByteAt(base)) << 24) | (uint32_t(ByteAt(base + 1)) << 16) |
                                (uint32_t(ByteAt(base + 2)) << 8) | ByteAt(base + 3);
        if (offset != 0) {
            mValidTracks |= 1u << i;
            mCurLoc[i] = offset;
            mEvtDeltaTicks[i] = ReadVarLen(i);
        } else {
            mCurLoc[i] = 0;
        }
    }

    const uint32_t division = (uint32_t(ByteAt(kHeaderDivisionOffset)) << 24) |
                              (uint32_t(ByteAt(kHeaderDivisionOffset + 1)) << 16) |
                              (uint32_t(ByteAt(kHeaderDivisionOffset + 2)) << 8) | ByteAt(kHeaderDivisionOffset + 3);
    mQnpt = static_cast<float>(1.0 / static_cast<float>(division));
}

void CompactSeq::NextEvent(SeqEvent* event) {
    uint32_t firstTime = 0xFFFFFFFF;
    int firstTrack = -1;
    const uint32_t lastTicks = mLastDeltaTicks;

    for (int i = 0; i < kSeqTrackCount; i++) {
        if ((mValidTracks >> i) & 1) {
            if (mDeltaFlag) {
                mEvtDeltaTicks[i] -= lastTicks;
            }
            if (mEvtDeltaTicks[i] < firstTime) {
                firstTime = mEvtDeltaTicks[i];
                firstTrack = i;
            }
        }
    }

    // The player only asks while a track remains.
    if (firstTrack < 0) {
        event->type = kSeqEndEvt;
        return;
    }

    GetTrackEvent(firstTrack, event);

    event->ticks = static_cast<int32_t>(firstTime);
    mLastTicks += firstTime;
    mLastDeltaTicks = firstTime;
    if (event->type != kTrackEnd) {
        mEvtDeltaTicks[firstTrack] += ReadVarLen(firstTrack);
    }
    mDeltaFlag = 1;
}

bool CompactSeq::NextDelta(int32_t* deltaTicks) {
    uint32_t firstTime = 0xFFFFFFFF;
    const uint32_t lastTicks = mLastDeltaTicks;

    if (!mValidTracks) {
        return false;
    }

    for (int i = 0; i < kSeqTrackCount; i++) {
        if ((mValidTracks >> i) & 1) {
            if (mDeltaFlag) {
                mEvtDeltaTicks[i] -= lastTicks;
            }
            if (mEvtDeltaTicks[i] < firstTime) {
                firstTime = mEvtDeltaTicks[i];
            }
        }
    }

    mDeltaFlag = 0;
    *deltaTicks = static_cast<int32_t>(firstTime);
    return true;
}

void CompactSeq::GetTrackEvent(int track, SeqEvent* event) {
    const uint8_t status = GetTrackByte(track);

    if (status == kMidiMeta) {
        const uint8_t type = GetTrackByte(track);

        if (type == kMidiMetaTempo) {
            event->type = kTempoEvt;
            event->tempo.status = status;
            event->tempo.type = type;
            event->tempo.byte1 = GetTrackByte(track);
            event->tempo.byte2 = GetTrackByte(track);
            event->tempo.byte3 = GetTrackByte(track);
            mLastStatus[track] = 0;
        } else if (type == kMidiMetaEndOfTrack) {
            mValidTracks ^= 1u << track;
            event->type = mValidTracks ? kTrackEnd : kSeqEndEvt;
        } else if (type == kCmidiLoopStartCode) {
            // Two operand bytes follow, unused.
            GetTrackByte(track);
            GetTrackByte(track);
            mLastStatus[track] = 0;
            event->type = kCspLoopStart;
        } else if (type == kCmidiLoopEndCode) {
            // Loop count, remaining count (0xFF loops forever), then a 32-bit
            // distance back from the end of the event. The remaining count
            // lives in the sequence data and is rewritten as the loop runs.
            uint32_t at = mCurLoc[track];
            const uint8_t loopCount = ByteAt(at++);
            const uint8_t current = ByteAt(at);
            if (current == 0) {
                if (at < mSize) {
                    mData[at] = loopCount;
                }
                mCurLoc[track] = at + 5;
            } else {
                if (current != 0xFF && at < mSize) {
                    mData[at] = static_cast<uint8_t>(current - 1);
                }
                at++;
                uint32_t offset = uint32_t(ByteAt(at++)) << 24;
                offset += uint32_t(ByteAt(at++)) << 16;
                offset += uint32_t(ByteAt(at++)) << 8;
                offset += ByteAt(at++);
                mCurLoc[track] = at - offset;
            }
            mLastStatus[track] = 0;
            event->type = kCspLoopEnd;
        }
    } else {
        event->type = kSeqMidiEvt;
        if (status & 0x80) {
            event->midi.status = status;
            event->midi.byte1 = GetTrackByte(track);
            mLastStatus[track] = status;
        } else {
            // Running status.
            event->midi.status = mLastStatus[track];
            event->midi.byte1 = status;
        }

        if (((event->midi.status & 0xF0) != kMidiProgramChange) &&
            ((event->midi.status & 0xF0) != kMidiChannelPressure)) {
            event->midi.byte2 = GetTrackByte(track);
            if ((event->midi.status & 0xF0) == kMidiNoteOn) {
                event->midi.duration = ReadVarLen(track);
            }
        } else {
            event->midi.byte2 = 0;
        }
    }
}

// Reads a track byte: from the back-reference being replayed if there is one,
// otherwise from the track, where a block code starts a new back-reference
// (the distance to back up, then the length to replay) unless it is doubled.
uint8_t CompactSeq::GetTrackByte(int track) {
    if (mCurBackupLen[track]) {
        const uint8_t byte = ByteAt(mCurBackupPtr[track]);
        mCurBackupPtr[track]++;
        mCurBackupLen[track]--;
        return byte;
    }

    uint8_t byte = ByteAt(mCurLoc[track]++);
    if (byte == kCmidiBlockCode) {
        const uint8_t next = ByteAt(mCurLoc[track]++);
        if (next != kCmidiBlockCode) {
            const uint8_t hiBackUp = next;
            const uint8_t loBackUp = ByteAt(mCurLoc[track]++);
            const uint8_t length = ByteAt(mCurLoc[track]++);
            const uint32_t backup = (uint32_t(hiBackUp) << 8) + loBackUp;
            // curLoc has advanced past the four code bytes.
            mCurBackupPtr[track] = mCurLoc[track] - (backup + 4);
            mCurBackupLen[track] = length;

            byte = ByteAt(mCurBackupPtr[track]);
            mCurBackupPtr[track]++;
            mCurBackupLen[track]--;
        }
    }
    return byte;
}

uint32_t CompactSeq::ReadVarLen(int track) {
    uint32_t value = GetTrackByte(track);
    if (value & 0x80) {
        value &= 0x7F;
        uint32_t c;
        do {
            c = GetTrackByte(track);
            value = (value << 7) + (c & 0x7F);
        } while (c & 0x80);
    }
    return value;
}

} // namespace Frigate::AudioDriver
