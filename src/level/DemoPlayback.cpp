#include "level/DemoPlayback.h"

namespace Frigate::Level {

namespace {

constexpr size_t kHeaderSize = 0xE8;
constexpr size_t kSizeCmdsOffset = 0x18;
constexpr size_t kTotalTimeOffset = 0x7C;
constexpr size_t kSeedSize = 4;
constexpr size_t kSampleSize = 4;

uint32_t ReadU32(const std::vector<uint8_t>& file, size_t at) {
    return (uint32_t{ file[at] } << 24) | (uint32_t{ file[at + 1] } << 16) | (uint32_t{ file[at + 2] } << 8) |
           uint32_t{ file[at + 3] };
}

} // namespace

MoveInput DemoPacket::Input() const {
    MoveInput input = samples.empty() ? MoveInput{} : samples.back();
    if (!samples2.empty()) {
        input.stick2X = samples2.back().stickX;
        input.stick2Y = samples2.back().stickY;
        input.buttons2 = samples2.back().buttons;
    }
    return input;
}

std::optional<DemoPlayback> DemoPlayback::Parse(std::vector<uint8_t> file) {
    if (file.size() < kHeaderSize) {
        return std::nullopt;
    }
    DemoPlayback playback;
    playback.controllers_ = ReadU32(file, kSizeCmdsOffset);
    playback.totalTime_ = static_cast<int32_t>(ReadU32(file, kTotalTimeOffset));
    playback.cursor_ = kHeaderSize;
    playback.file_ = std::move(file);
    if (playback.controllers_ == 0 || playback.controllers_ > 4) {
        return std::nullopt;
    }
    return playback;
}

std::optional<DemoPacket> DemoPlayback::Next() {
    if (cursor_ + kSeedSize > file_.size()) {
        return std::nullopt;
    }
    const uint8_t speedFrames = file_[cursor_];
    const uint8_t count = file_[cursor_ + 1];
    const uint8_t seed = file_[cursor_ + 2];
    const uint8_t check = file_[cursor_ + 3];

    if (count == 0 && speedFrames == 0) {
        return std::nullopt;
    }
    const size_t dataSize = controllers_ * kSampleSize * count;
    if (cursor_ + kSeedSize + dataSize > file_.size()) {
        return std::nullopt;
    }

    DemoPacket packet;
    packet.speedFrames = speedFrames;
    packet.randSeed = seed;
    uint8_t sum = static_cast<uint8_t>(speedFrames + count + seed);
    size_t at = cursor_ + kSeedSize;
    for (size_t frame = 0; frame < count; frame++) {
        for (size_t controller = 0; controller < controllers_; controller++) {
            MoveInput sample;
            sample.stickX = static_cast<int8_t>(file_[at]);
            sample.stickY = static_cast<int8_t>(file_[at + 1]);
            sample.buttons = static_cast<uint16_t>((file_[at + 3] << 8) | file_[at + 2]);
            sum = static_cast<uint8_t>(sum + file_[at] + file_[at + 1] + file_[at + 2] + file_[at + 3]);
            at += kSampleSize;
            if (controller == 0) {
                packet.samples.push_back(sample);
            } else if (controller == 1) {
                packet.samples2.push_back(sample);
            }
        }
    }
    if (sum != check) {
        return std::nullopt;
    }
    cursor_ = at;
    return packet;
}

} // namespace Frigate::Level
