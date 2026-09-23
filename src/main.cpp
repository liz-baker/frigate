#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "game/Game.h"
#include "libultraship/log/luslog.h"
#include "platform/Boot.h"
#include "viewer/Viewer.h"

int main(int argc, char** argv) {
    std::vector<std::string> archivePaths;
    Frigate::Viewer::Options options;
    // The real game loop is the default; --model/--texture (or a bare
    // --viewer) opt into the throwaway asset-inspection viewer instead
    // (docs/viewer.md) -- the two are mutually exclusive entry points,
    // not flags the game loop itself reads.
    bool viewerMode = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--viewer") == 0) {
            viewerMode = true;
        } else if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            options.model = argv[++i];
            viewerMode = true;
        } else if (std::strcmp(argv[i], "--texture") == 0 && i + 1 < argc) {
            options.texture = argv[++i];
            viewerMode = true;
        } else if (std::strcmp(argv[i], "--no-textures") == 0) {
            options.textures = false;
        } else if (std::strcmp(argv[i], "--distance") == 0 && i + 1 < argc) {
            options.distance = std::strtof(argv[++i], nullptr);
        } else if (!std::filesystem::exists(argv[i])) {
            LUSLOG_ERROR("frigate: archive not found: %s", argv[i]);
            return 1;
        } else {
            archivePaths.emplace_back(argv[i]);
        }
    }
    // The game loop doesn't load any GoldenEye archives yet (#83/#85 still
    // need to wire real text/model content into it), so it's the one mode
    // that doesn't require one on the command line.
    if (archivePaths.empty() && viewerMode) {
        LUSLOG_ERROR("usage: %s [--viewer [--model <models/dir/name> | --texture <textures/name>] [--distance "
                     "<units>] [--no-textures]] <archive.o2r> [more...]",
                     argv[0]);
        return 1;
    }

    // Fast3D's shader archive, built next to the executable.
    auto f3dArchive = std::filesystem::absolute(argv[0]).parent_path() / "f3d.o2r";
    if (!std::filesystem::exists(f3dArchive)) {
        LUSLOG_ERROR("frigate: shader archive not found: %s", f3dArchive.string().c_str());
        return 1;
    }
    archivePaths.push_back(f3dArchive.string());

    auto context = Frigate::Platform::CreateContext("frigate", "frigate", "frigate.json", archivePaths);
    if (context == nullptr) {
        return 1;
    }
    if (viewerMode) {
        return Frigate::Viewer::Run(context, options);
    }
    return Frigate::Game::Run(context);
}
