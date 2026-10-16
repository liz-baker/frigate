// frigate-extract <rom> <outdir> [--region u|e|j]: builds the O2R archives from a ROM without the game.

#include "extract/Extractor.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace Frigate::Extract;

namespace {

void Usage() {
    std::fprintf(stderr, "usage: frigate-extract <rom.z64> <output-dir> [--region u|e|j]\n");
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--region") == 0 && i + 1 < argc) {
            std::string code = argv[++i];
            bool found = false;
            for (const RegionInfo& info : AllRegions()) {
                if (code == info.code) {
                    options.region = info.region;
                    found = true;
                }
            }
            if (!found) {
                Usage();
                return 2;
            }
        } else if (argv[i][0] == '-') {
            Usage();
            return 2;
        } else if (positional == 0) {
            options.romPath = argv[i];
            positional++;
        } else if (positional == 1) {
            options.outputDir = argv[i];
            positional++;
        } else {
            Usage();
            return 2;
        }
    }
    if (positional != 2) {
        Usage();
        return 2;
    }

    std::string lastStage;
    options.progress = [&](const Progress& progress) {
        if (progress.stageName != lastStage && !progress.stageName.empty()) {
            lastStage = progress.stageName;
            std::printf("[%zu/%zu] %s\n", progress.stageIndex + 1, progress.stageCount, progress.stageName.c_str());
        }
    };

    Extractor extractor;
    RegisterDefaultStages(extractor);
    Result result = extractor.Run(options);
    if (!result.error.ok()) {
        std::fprintf(stderr, "error: %s\n", result.error.message.c_str());
        return 1;
    }
    std::printf("Extracted the %s ROM:\n", GetRegionInfo(result.region).name);
    for (const auto& path : result.archives) {
        std::printf("  %s\n", path.string().c_str());
    }
    return 0;
}
