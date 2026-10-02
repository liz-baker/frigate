#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "level/PlayerMove.h"

namespace Frigate::Level {

// One recorded frame: how many clock ticks it took and the controller samples
// taken during it (ramrom_seed and ramrom_blockbuf). On-disk layout:
// docs/formats/goldeneye-ramrom.md.
struct DemoPacket {
    int speedFrames = 0;
    // randseed: the low byte of the random state when the frame was recorded.
    uint8_t randSeed = 0;
    // The first controller's samples, oldest first.
    std::vector<MoveInput> samples;
    // The second controller's, when the demo recorded two.
    std::vector<MoveInput> samples2;
    // The first controller's last sample of the packet before (the zero
    // sample for the first), which this packet's buttons are pressed against.
    MoveInput previous;
    // The number of controllers the demo recorded.
    int controllers = 1;

    // What the game reads for the frame: the last sample.
    MoveInput Input() const;
};

// The packet stream of a ramrom demo file, as ramrom_replay_handler reads it.
class DemoPlayback {
  public:
    // Empty when the file is shorter than its header.
    static std::optional<DemoPlayback> Parse(std::vector<uint8_t> file);

    // totaltime_ms: the game fades to the title this many ticks (minus 60) in.
    int32_t TotalTime() const {
        return totalTime_;
    }
    // The next frame; empty at the end of the demo or on a packet whose
    // checksum fails (either way the game fades to the title). The caller
    // compares `randSeed` with its random state: the game fades to the title
    // on a mismatch.
    std::optional<DemoPacket> Next();

  private:
    std::vector<uint8_t> file_;
    size_t cursor_ = 0;
    // The first controller's last sample so far.
    MoveInput last_;
    size_t controllers_ = 1;
    int32_t totalTime_ = 0;
};

} // namespace Frigate::Level
