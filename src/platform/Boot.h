#pragma once

#include <memory>
#include <string>

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
// populated O2R/OTR archive: the ResourceManager here gets no archivePaths,
// since nothing produces one yet. Pass real archivePaths once something does.
std::shared_ptr<Ship::Context> CreateContext(const std::string& name, const std::string& shortName,
                                             const std::string& configFilePath);

} // namespace Frigate::Platform
