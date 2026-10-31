// Applies the profiler's allocator experiment once a second (Wii U builds with FAST3D_PROFILE only).
#if defined(__WIIU__) && defined(FAST3D_PROFILE)
#include "fast/profiler.h"
#include "platform/WiiUAllocator.h"

namespace Frigate::Platform {

namespace {

// The game registers this slot as the medium_pool experiment (Game.cpp).
constexpr int kMediumPoolExperiment = 4;

void ApplyExperiments() {
    SetMediumPool(Fast::Prof::gExp.user[kMediumPoolExperiment] != 0);
}

} // namespace

void InstallProfilerHooks() {
    Fast::Prof::SetReportHook(ApplyExperiments);
}

} // namespace Frigate::Platform
#endif
