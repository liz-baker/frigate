#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/Game.h"
#include "game/SoundOutput.h"
#include "libultraship/log/luslog.h"
#include "menu/Menu.h"
#include "platform/Boot.h"
#include "platform/FirstRun.h"
#include "ship/core/Context.h"

#ifdef FRIGATE_DEV_TOOLS
#include "dev/DevArgs.h"
#endif

namespace {

// The archives the packer writes, looked up by these names in the archive
// directory. Any that are missing are skipped; what needs them says so.
constexpr std::array<const char*, 4> kStandardArchives = {
    "goldeneye_textures.o2r",
    "goldeneye_objects.o2r",
    "goldeneye_geometry.o2r",
    "goldeneye_audio.o2r",
};

constexpr const char* kUsage = "usage: %s [--archive-dir <dir>] [--rom <rom.z64>] [--no-extract]";

} // namespace

int main(int argc, char** argv) {
    // Where the archives live: the app directory (SD card /wiiu/apps/frigate/
    // on Wii U, SHIP_HOME or the working directory elsewhere), unless
    // overridden with --archive-dir.
    std::filesystem::path archiveDir = Ship::Context::GetAppDirectoryPath("frigate");
    // --rom names the ROM to extract from instead of searching; --no-extract
    // never extracts (docs/extract.md, "First run").
    std::optional<std::filesystem::path> romPath;
    bool noExtract = false;
    const char* usage = kUsage;
#ifdef FRIGATE_DEV_TOOLS
    Frigate::Dev::Args dev;
    usage = Frigate::Dev::Usage();
#endif
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--archive-dir") == 0 && i + 1 < argc) {
            archiveDir = argv[++i];
        } else if (std::strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            romPath = argv[++i];
        } else if (std::strcmp(argv[i], "--no-extract") == 0) {
            noExtract = true;
        } else {
#ifdef FRIGATE_DEV_TOOLS
            const auto status = Frigate::Dev::ParseArg(argc, argv, i, dev);
            if (status == Frigate::Dev::ParseStatus::Consumed) {
                continue;
            }
            if (status == Frigate::Dev::ParseStatus::Error) {
                return 1;
            }
#endif
            LUSLOG_ERROR("frigate: unknown argument: %s", argv[i]);
            LUSLOG_ERROR(usage, argv[0]);
            return 1;
        }
    }
    Frigate::Game::Options gameOptions;
    // Without a window only the sound test runs; the first-run window and the
    // rendering context both need one.
    bool windowed = true;
    bool runsGame = true;
#ifdef FRIGATE_DEV_TOOLS
    if (!Frigate::Dev::Finish(dev)) {
        return 1;
    }
    gameOptions = dev.game;
    windowed = !dev.playSound.has_value();
    runsGame = !dev.level;
#endif
    // A missing or damaged archive set is rebuilt from the US ROM before
    // anything loads it. The Ship::Context needs the archive paths up front,
    // so this runs first and draws its own progress window.
    if (!noExtract) {
        Frigate::Extract::FirstRunOptions firstRun;
        firstRun.archiveDir = archiveDir;
        firstRun.rom = romPath;
        firstRun.searchDirs = { std::filesystem::absolute(Ship::Context::GetAppDirectoryPath("frigate")),
                                std::filesystem::current_path() };
        const auto outcome = Frigate::Platform::RunFirstRun(std::move(firstRun), windowed);
        if (outcome.status == Frigate::Extract::FirstRunStatus::Failed ||
            outcome.status == Frigate::Extract::FirstRunStatus::Cancelled) {
            return 1;
        }
    }
    std::vector<std::string> archivePaths;
    for (const char* name : kStandardArchives) {
        const auto path = archiveDir / name;
        if (std::filesystem::exists(path)) {
            archivePaths.push_back(path.string());
        } else {
            LUSLOG_WARN("frigate: archive not found: %s", path.string().c_str());
        }
    }
    // The game loop draws nothing beyond a black clear without the objects
    // archive, but doesn't require one. The level runner inherently needs
    // something to load, so it does require at least one archive.
    if (archivePaths.empty() && !runsGame) {
        LUSLOG_ERROR(usage, argv[0]);
        return 1;
    }

    // Fast3D's shader archive, built next to the executable; only a window
    // draws with it. The GX2 backend generates its shaders at runtime and
    // never loads it.
#ifndef __WIIU__
    if (windowed) {
        auto f3dArchive = std::filesystem::absolute(argv[0]).parent_path() / "f3d.o2r";
        if (!std::filesystem::exists(f3dArchive)) {
            LUSLOG_ERROR("frigate: shader archive not found: %s", f3dArchive.string().c_str());
            return 1;
        }
        archivePaths.push_back(f3dArchive.string());
    }
#endif

    auto context = Frigate::Platform::CreateContext("frigate", "frigate", "frigate.json", archivePaths, windowed);
    if (context == nullptr) {
        return 1;
    }
#ifdef FRIGATE_DEV_TOOLS
    Frigate::Dev::RunSoundTestIfRequested(context, dev);
#endif
    Frigate::ConfigMenu::Install(context, runsGame ? std::function<void()>(Frigate::Game::RequestReset) : nullptr,
                                 runsGame ? std::function<void(const Frigate::Game::MultiplayerSettings&)>(
                                                Frigate::Game::RequestMultiplayerStart)
                                          : nullptr);
    // The game's audio runs for as long as the game does.
    const auto soundOutput = Frigate::Game::SoundOutput::Start(context);
#ifdef FRIGATE_DEV_TOOLS
    if (dev.level) {
        return Frigate::Dev::RunLevel(context, *dev.level);
    }
#endif
    return Frigate::Game::Run(context, gameOptions);
}
