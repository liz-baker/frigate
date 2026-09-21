#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "fast/Fast3dWindow.h"
#include "libultraship/libultra/gbi.h"
#include "platform/Boot.h"
#include "ship/core/Context.h"

int main(int argc, char** argv) {
    std::vector<std::string> archivePaths;
    for (int i = 1; i < argc; i++) {
        if (!std::filesystem::exists(argv[i])) {
            std::fprintf(stderr, "frigate: archive not found: %s\n", argv[i]);
            return 1;
        }
        archivePaths.emplace_back(argv[i]);
    }
    if (archivePaths.empty()) {
        std::fprintf(stderr, "usage: %s <archive.o2r> [more archives...]\n", argv[0]);
        return 1;
    }

    auto context = Frigate::Platform::CreateContext("frigate", "frigate", "frigate.json", archivePaths);
    if (context == nullptr) {
        return 1;
    }
    auto window = context->GetChildren().GetFirst<Fast::Fast3dWindow>();

    Gfx emptyList[1];
    Gfx* gfx = emptyList;
    gSPEndDisplayList(gfx++);

    // DrawAndRunGraphicsCommands runs the interpreter's own StartFrame/EndFrame,
    // so the loop must not call them again.
    while (window->IsRunning()) {
        window->DrawAndRunGraphicsCommands(emptyList, {});
    }

    window->Close();
    return 0;
}
