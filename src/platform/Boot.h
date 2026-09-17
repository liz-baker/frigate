#pragma once

#include <memory>
#include <string>

namespace Ship {
class Context;
}

// Boots this port's libultraship-wiiu Context -- the single call site that
// replaces goldeneye-pc-port's hand-rolled OS/threading/config/crash/input
// layers (frigate issue #4) with the library's own components:
//
//   - OS/threading: Ship::Context + Ship::ThreadPool + the existing
//     libultra-compatible os.cpp shim (osContGetReadData() et al.), instead
//     of a from-scratch ~1,800-line libultra.c.
//   - Config: Ship::Config / Ship::ConsoleVariable, instead of a new
//     config.c/optionsoverlay.c-style system.
//   - Crash handling: Ship::CrashHandler as-is (no per-platform diagnostics
//     on Wii U today -- see libultraship-wiiu#33 for the tracked follow-up;
//     decided in frigate#4 rather than blocking on it).
//   - Rendering: Fast::Fast3dWindow, which self-selects GfxRenderingAPIGX2 /
//     GfxWindowBackendWiiU on CafeOS at build time (FAST3D_GX2) with no
//     platform branching needed here.
//   - Input: LUS::ControlDeck, which is already a complete N64-standard
//     ControlDeck (registers the standard button set, writes real OSContPad
//     entries from mapped input) -- no new frigate-side subclass needed.
namespace Frigate::Platform {

// Unlike Ship::Context::CreateDefaultInstance(), this does not require a
// working O2R/OTR archive: frigate issue #5 keeps GoldenEye's own assets on
// the decomp's existing recompile-to-C pipeline rather than routing them
// through libultraship's archive system, so there is no "main game OTR" to
// point a ResourceManager at. A ResourceManager is still constructed --
// Fast3dWindow/Fast3dGui require the component to be present for the
// library's own built-in ImGui/options-menu resources -- but with no
// archives loaded; it carries none of GoldenEye's own data.
//
// Returns nullptr on failure (logged via spdlog), same contract as
// CreateDefaultInstance.
std::shared_ptr<Ship::Context> CreateContext(const std::string& name, const std::string& shortName,
                                             const std::string& configFilePath);

} // namespace Frigate::Platform
