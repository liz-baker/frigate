#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/Boot.h"
#include "viewer/Viewer.h"

int main(int argc, char** argv) {
    std::vector<std::string> archivePaths;
    Frigate::Viewer::Options options;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            options.model = argv[++i];
        } else if (std::strcmp(argv[i], "--textures") == 0) {
            options.textures = true;
        } else if (std::strcmp(argv[i], "--distance") == 0 && i + 1 < argc) {
            options.distance = std::strtof(argv[++i], nullptr);
        } else if (!std::filesystem::exists(argv[i])) {
            std::fprintf(stderr, "frigate: archive not found: %s\n", argv[i]);
            return 1;
        } else {
            archivePaths.emplace_back(argv[i]);
        }
    }
    if (archivePaths.empty()) {
        std::fprintf(
            stderr, "usage: %s [--model <models/dir/name>] [--distance <units>] [--textures] <archive.o2r> [more...]\n",
            argv[0]);
        return 1;
    }

    // Fast3D's shader archive, built next to the executable.
    auto f3dArchive = std::filesystem::absolute(argv[0]).parent_path() / "f3d.o2r";
    if (!std::filesystem::exists(f3dArchive)) {
        std::fprintf(stderr, "frigate: shader archive not found: %s\n", f3dArchive.string().c_str());
        return 1;
    }
    archivePaths.push_back(f3dArchive.string());

    auto context = Frigate::Platform::CreateContext("frigate", "frigate", "frigate.json", archivePaths);
    if (context == nullptr) {
        return 1;
    }
    return Frigate::Viewer::Run(context, options);
}
