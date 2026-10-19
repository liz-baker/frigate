#pragma once

#include "extract/FirstRun.h"

namespace Frigate::Platform {

// Runs Extract::EnsureArchives() on a worker thread, showing its progress. Nothing is shown (and no thread
// started) when the archive set is already complete. With `allowWindow` the progress is a small window drawn
// with SDL's software renderer, which needs no archives and no Ship::Context, so it works before either exists;
// closing it cancels. Without a window, or when none can be opened (no display, Wii U), progress and the outcome
// go to the log. The Ship::Context must not exist yet.
Extract::FirstRunResult RunFirstRun(Extract::FirstRunOptions options, bool allowWindow);

} // namespace Frigate::Platform
