#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Ship {
class Context;
}

namespace Frigate::Platform {

// Constructs and initializes a Ship::Context with this port's full component
// set (logging, config, thread pool, resource manager, control deck, crash
// handler, console, window, audio, events, file drop). Returns nullptr on
// failure (logged via spdlog).
//
// Unlike Ship::Context::CreateDefaultInstance(), this doesn't require a
// populated O2R/OTR archive: an empty archivePaths is valid and mounts nothing.
std::shared_ptr<Ship::Context> CreateContext(const std::string& name, const std::string& shortName,
                                             const std::string& configFilePath,
                                             const std::vector<std::string>& archivePaths = {});

} // namespace Frigate::Platform
