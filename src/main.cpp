#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "game/Game.h"
#include "libultraship/log/luslog.h"
#include "platform/Boot.h"
#include "viewer/LevelView.h"
#include "viewer/Viewer.h"

namespace {

// "x,y,z".
bool ParseVec3(const char* text, std::array<float, 3>& out) {
    return std::sscanf(text, "%f,%f,%f", &out[0], &out[1], &out[2]) == 3;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archivePaths;
    Frigate::Viewer::Options options;
    // The real game loop is the default; --model/--texture (or a bare
    // --viewer) opt into the throwaway asset-inspection viewer instead
    // (docs/viewer.md).
    bool viewerMode = false;
    std::optional<Frigate::Viewer::LevelOptions> levelOptions;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--viewer") == 0) {
            viewerMode = true;
        } else if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            options.model = argv[++i];
            viewerMode = true;
        } else if (std::strcmp(argv[i], "--texture") == 0 && i + 1 < argc) {
            options.texture = argv[++i];
            viewerMode = true;
        } else if (std::strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
            levelOptions.emplace();
            levelOptions->stage = argv[++i];
        } else if (std::strcmp(argv[i], "--room") == 0 && i + 1 < argc && levelOptions) {
            levelOptions->room = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--camera") == 0 && i + 1 < argc && levelOptions) {
            levelOptions->hasPosition = ParseVec3(argv[++i], levelOptions->position);
        } else if (std::strcmp(argv[i], "--look") == 0 && i + 1 < argc && levelOptions) {
            ParseVec3(argv[++i], levelOptions->look);
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
    // The game loop draws nothing beyond a black clear without the objects
    // archive, but doesn't require one. The viewer inherently needs
    // something to inspect, so it does require at least one archive.
    if (archivePaths.empty() && (viewerMode || levelOptions)) {
        LUSLOG_ERROR("usage: %s [--viewer [--model <models/dir/name> | --texture <textures/name>] [--distance "
                     "<units>] [--no-textures]] [--level <bg_dir> [--room <n>] [--camera x,y,z] [--look x,y,z]] "
                     "<archive.o2r> [more...]",
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
    if (levelOptions) {
        return Frigate::Viewer::RunLevel(context, *levelOptions);
    }
    if (viewerMode) {
        return Frigate::Viewer::Run(context, options);
    }
    return Frigate::Game::Run(context);
}
