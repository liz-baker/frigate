// The Wii U's replacement for the global operator new and delete (WiiUAllocator.cpp).
#pragma once

namespace Frigate::Platform {

// Serves allocations of 513 bytes to 256 KiB from per-size free lists that keep their blocks, as
// small ones always are, instead of from the default heap. Safe to switch at any time: a block is
// freed the way it was allocated.
void SetMediumPool(bool enabled);

// Starts the profiler's once-a-second heap probe and applies the medium_pool experiment from it
// (WiiUHeapProbe.cpp, Wii U builds with FAST3D_PROFILE).
void InstallHeapProbe();

} // namespace Frigate::Platform
