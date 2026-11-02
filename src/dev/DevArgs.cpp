#include "dev/DevArgs.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <spdlog/spdlog.h>

#include "dev/SoundTest.h"
#include "fast/profiler.h"
#include "libultraship/log/luslog.h"
#include "platform/Input.h"
#include "ship/core/Context.h"

namespace Frigate::Dev {

namespace {

// "x,y,z".
bool ParseVec3(const char* text, std::array<float, 3>& out) {
    return std::sscanf(text, "%f,%f,%f", &out[0], &out[1], &out[2]) == 3;
}

// "a,b,c" as table indices.
bool ParseIndexList(const char* text, std::vector<size_t>& out) {
    out.clear();
    char* end = nullptr;
    for (const char* cursor = text; *cursor != '\0';) {
        const long value = std::strtol(cursor, &end, 10);
        if (end == cursor || value < 0) {
            return false;
        }
        out.push_back(static_cast<size_t>(value));
        cursor = *end == ',' ? end + 1 : end;
    }
    return !out.empty();
}

std::optional<Game::Menu::MissionEndPreview> ParseMissionEndPreview(const char* text) {
    using Game::Menu::MissionEndPreview;
    if (std::strcmp(text, "failed") == 0) {
        return MissionEndPreview::Failed;
    }
    if (std::strcmp(text, "kia") == 0) {
        return MissionEndPreview::KilledInAction;
    }
    if (std::strcmp(text, "aborted") == 0) {
        return MissionEndPreview::Aborted;
    }
    if (std::strcmp(text, "completed") == 0) {
        return MissionEndPreview::Completed;
    }
    if (std::strcmp(text, "stats") == 0) {
        return MissionEndPreview::Statistics;
    }
    return std::nullopt;
}

bool Is(const char* argument, const char* name) {
    return std::strcmp(argument, name) == 0;
}

} // namespace

ParseStatus ParseArg(int argc, char** argv, int& i, Args& args) {
    const char* arg = argv[i];
    const bool hasValue = i + 1 < argc;
    auto& level = args.level;
    auto& quickStart = args.quickStart;
    if (Is(arg, "--play-sound") && hasValue) {
        args.playSound = argv[++i];
    } else if (Is(arg, "--play-seconds") && hasValue) {
        args.playSeconds = std::strtod(argv[++i], nullptr);
    } else if (Is(arg, "--level") && hasValue) {
        level.emplace();
        level->stage = argv[++i];
    } else if (Is(arg, "--room") && hasValue && level) {
        level->room = std::atoi(argv[++i]);
    } else if (Is(arg, "--camera") && hasValue && level) {
        level->hasPosition = ParseVec3(argv[++i], level->position);
    } else if (Is(arg, "--look") && hasValue && level) {
        level->hasLook = ParseVec3(argv[++i], level->look);
    } else if (Is(arg, "--players") && hasValue) {
        args.players = std::atoi(argv[++i]);
    } else if (Is(arg, "--multiplayer")) {
        args.multiplayer = true;
    } else if (Is(arg, "--stage") && hasValue) {
        quickStart.stage = argv[++i];
    } else if (Is(arg, "--scenario") && hasValue) {
        quickStart.scenario = argv[++i];
    } else if (Is(arg, "--characters") && hasValue) {
        quickStart.characters = argv[++i];
    } else if (Is(arg, "--handicaps") && hasValue) {
        quickStart.handicaps = argv[++i];
    } else if (Is(arg, "--control-styles") && hasValue) {
        quickStart.controlStyles = argv[++i];
    } else if (Is(arg, "--teams") && hasValue) {
        quickStart.teams = argv[++i];
    } else if (Is(arg, "--weapon-set") && hasValue) {
        quickStart.weaponSet = argv[++i];
    } else if (Is(arg, "--game-length") && hasValue) {
        quickStart.gameLength = argv[++i];
    } else if (Is(arg, "--aim") && hasValue) {
        quickStart.aim = argv[++i];
    } else if (Is(arg, "--cheats") && hasValue) {
        quickStart.cheats = argv[++i];
    } else if (Is(arg, "--demo-slot") && hasValue && level) {
        level->demoSlot = std::atoi(argv[++i]);
    } else if (Is(arg, "--control-style") && hasValue && level) {
        level->controlStyle = Level::ParseControlStyle(argv[++i]);
        if (!level->controlStyle) {
            LUSLOG_ERROR("frigate: --control-style takes 1.1 to 1.4 or 2.1 to 2.4, not %s", argv[i]);
            return ParseStatus::Error;
        }
    } else if (Is(arg, "--demo") && hasValue) {
        if (!level) {
            level.emplace();
        }
        level->demo = std::atoi(argv[++i]);
    } else if (Is(arg, "--demos") && hasValue) {
        if (!ParseIndexList(argv[++i], args.game.demoOrder)) {
            LUSLOG_ERROR("frigate: --demos takes a comma-separated list of demo indices");
            return ParseStatus::Error;
        }
    } else if (Is(arg, "--no-abort-on-mismatch")) {
        args.game.fadeOnRandomMismatch = false;
    } else if (Is(arg, "--uncapped")) {
        args.game.uncapped = true;
    } else if (Is(arg, "--mission-end") && hasValue) {
        const auto preview = ParseMissionEndPreview(argv[++i]);
        if (!preview) {
            LUSLOG_ERROR("frigate: --mission-end takes failed, kia, aborted, completed or stats");
            return ParseStatus::Error;
        }
        args.game.missionEnd = preview;
    } else if (Is(arg, "--input-script") && hasValue) {
        std::string error;
        auto script = Platform::Input::InputScript::Parse(argv[++i], &error);
        if (!script) {
            LUSLOG_ERROR("frigate: --input-script: %s", error.c_str());
            return ParseStatus::Error;
        }
        Platform::Input::SetScript(std::move(script));
    } else if (Is(arg, "--exp") && hasValue) {
#ifdef FAST3D_PROFILE
        if (!Fast::Prof::SetExperimentFromArg(argv[++i])) {
            LUSLOG_ERROR("frigate: --exp: unknown experiment or malformed value: %s", argv[i]);
            return ParseStatus::Error;
        }
#else
        LUSLOG_ERROR("frigate: --exp needs a build with FAST3D_PROFILE (a Wii U Debug build)");
        return ParseStatus::Error;
#endif
    } else {
        return ParseStatus::NotMine;
    }
    return ParseStatus::Consumed;
}

bool Finish(Args& args) {
    if (args.level && args.players) {
        args.level->players = *args.players;
    }
    if (args.level) {
        args.level->fadeOnRandomMismatch = args.game.fadeOnRandomMismatch;
        args.level->uncapped = args.game.uncapped;
    }
    if (args.multiplayer) {
        if (args.level) {
            LUSLOG_ERROR("frigate: --multiplayer starts the game; it can't be combined with --level");
            return false;
        }
        args.quickStart.players = args.players.value_or(2);
        std::string error;
        args.game.multiplayerStart = Game::ParseQuickStart(args.quickStart, &error);
        if (!args.game.multiplayerStart) {
            LUSLOG_ERROR("frigate: --multiplayer: %s", error.c_str());
            return false;
        }
    } else if (args.players && !args.level) {
        LUSLOG_ERROR("frigate: --players needs --level or --multiplayer");
        return false;
    }
    return true;
}

const char* Usage() {
    return "usage: %s [--level <bg_dir> [--room <n>] [--camera x,y,z] [--look x,y,z] "
           "[--demo-slot <n>] [--players <1-4>] [--control-style <1.1-2.4>]] [--demo <index>] [--demos <i,j,...>] "
           "[--multiplayer [--players <2-4>] [--stage <name>] [--scenario <name>] [--characters <n,...>] "
           "[--handicaps <n,...>] [--control-styles <1.1-2.4,...>] [--teams <AABB>] [--weapon-set <name>] "
           "[--game-length <name>] [--aim <0-3>] [--cheats <name,...>]] "
           "[--no-abort-on-mismatch] [--uncapped] [--exp <name[=value]>]... "
           "[--mission-end <failed|kia|aborted|completed|stats>] "
           "[--input-script <frame[-frame]:BUTTONS+X=n+Y=n,...>] "
           "[--play-sound <name|number> [--play-seconds <n>]] "
           "[--archive-dir <dir>] [--rom <rom.z64>] [--no-extract]";
}

void RunSoundTestIfRequested(const std::shared_ptr<Ship::Context>& context, const Args& args) {
    if (!args.playSound) {
        return;
    }
    const int result = RunSoundTest(context, *args.playSound, 6.0, args.playSeconds);
    // The Context destructor logs after it has shut the logger down, which
    // crashes a Debug build on the way out. This mode has nothing to save,
    // so flush the logs and leave without running it.
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& logger) { logger->flush(); });
    std::fflush(nullptr);
    std::_Exit(result);
}

} // namespace Frigate::Dev
