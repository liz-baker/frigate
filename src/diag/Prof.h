// Lets code with no libultraship dependency (src/level) feed the Wii U profiler. The game installs
// the hooks in Game.cpp; with none installed, as in the unit tests, every call here does nothing.
// Compiled in only with FRIGATE_PROFILE (the profiler build).
#pragma once

#ifdef FRIGATE_PROFILE

#include <cstdint>

namespace Frigate::Diag {

// Stages, experiments and counters, numbered from the first of the profiler's user ones; Game.cpp
// maps them and gives them their names.
enum Stage {
    kRenderSetup,
    kRenderSky,
    kRenderProps,
    kRenderTicks,
    kRenderRoom,
    kRenderEffects,
    kRenderGuns,
    kRenderHud,
};
enum Experiment { kSkipEffects, kSkipSky, kSkipGunsAndHud, kSkipTicks };
enum Counter { kOnScreenProps };

struct Hooks {
    void (*enter)(int stage) = nullptr;
    void (*exit)() = nullptr;
    void (*replace)(int stage) = nullptr;
    int (*experiment)(int experiment) = nullptr;
    void (*count)(int counter, uint64_t n) = nullptr;
};
inline Hooks gHooks;

struct Scope {
    explicit Scope(int stage) {
        if (gHooks.enter) {
            gHooks.enter(stage);
        }
    }
    ~Scope() {
        if (gHooks.exit) {
            gHooks.exit();
        }
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

inline void Replace(int stage) {
    if (gHooks.replace) {
        gHooks.replace(stage);
    }
}
inline bool Experiment(int experiment) {
    return gHooks.experiment && gHooks.experiment(experiment) != 0;
}
inline void Count(int counter, uint64_t n) {
    if (gHooks.count) {
        gHooks.count(counter, n);
    }
}

} // namespace Frigate::Diag

#define FRIGATE_PROF_SCOPE(stage) ::Frigate::Diag::Scope frigateProfScope(::Frigate::Diag::stage)
#define FRIGATE_PROF_SWITCH(stage) ::Frigate::Diag::Replace(::Frigate::Diag::stage)
#define FRIGATE_PROF_EXP(experiment) ::Frigate::Diag::Experiment(::Frigate::Diag::experiment)
#define FRIGATE_PROF_COUNT(counter, n) ::Frigate::Diag::Count(::Frigate::Diag::counter, (n))

#else

#define FRIGATE_PROF_SCOPE(stage) ((void)0)
#define FRIGATE_PROF_SWITCH(stage) ((void)0)
#define FRIGATE_PROF_EXP(experiment) false
#define FRIGATE_PROF_COUNT(counter, n) ((void)0)

#endif
