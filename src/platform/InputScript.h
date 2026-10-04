#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "platform/InputState.h"

namespace Frigate::Platform::Input {

// A scripted first controller for headless runs: the pad reads these buttons
// and stick on the given frames instead of the real controller. Frames count
// from 0, one per Poll.
//
// Text form: comma-separated entries, `<frames>:<state>`.
//   <frames>  `N` -- the state starts on frame N and holds until the next entry
//             `N-M` -- the state holds for frames N..M, then everything releases
//   <state>   buttons joined by `+` (A B Z START L R CU CD CL CR DU DD DL DR)
//             and `X=<n>` / `Y=<n>` for the stick (-128..127); empty releases all
// Example: "300-302:A,500-502:A+X=-70,900:START".
class InputScript {
  public:
    // Nullopt (with `error` set when given) if the text is malformed.
    static std::optional<InputScript> Parse(const std::string& text, std::string* error = nullptr);

    // What the pad reads on `frame`.
    PadSample At(uint32_t frame) const;

  private:
    struct Step {
        uint32_t frame;
        PadSample sample;
    };
    // Sorted by frame; the last step at or before a frame is in effect.
    std::vector<Step> steps_;
};

// The script Poll() reads for port 0 in place of the controller; none by default.
void SetScript(std::optional<InputScript> script);
const std::optional<InputScript>& Script();

} // namespace Frigate::Platform::Input
