// The Wii U's replacement for the global operator new and delete (WiiUAllocator.cpp).
#pragma once

namespace Frigate::Platform {

// Serves allocations of 513 bytes to 256 KiB from per-size free lists that keep their blocks, as
// small ones always are, instead of from the default heap. Safe to switch at any time: a block is
// freed the way it was allocated.
void SetMediumPool(bool enabled);

// Applies the medium_pool experiment from the profiler's once-a-second report
// (WiiUProfilerHooks.cpp, Wii U builds with FAST3D_PROFILE).
void InstallProfilerHooks();

} // namespace Frigate::Platform
