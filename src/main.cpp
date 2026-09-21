#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "fast/Fast3dWindow.h"
#include "fast/f3dex.h"
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

    // A solid fill is the visible proof that the frame loop reaches the
    // renderer; an empty list would present the default black framebuffer.
    Gfx clearList[9];
    Gfx* gfx = clearList;
    // A fill whose color image matches the (default, zero) depth-buffer address
    // is dropped as a redundant depth clear, so point it somewhere else.
    gDPSetColorImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, reinterpret_cast<void*>(uintptr_t{ 0x00100000 }));
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gDPPipeSync(gfx++);
    // The indy ucode numbers these two commands like F3DEX (not F3DEX2), so
    // the stock gSP/gDP macros would emit opcodes it doesn't recognize; the
    // RDP commands around them are ucode-independent and can stay macros.
    gfx->words.w0 = (static_cast<uint32_t>(static_cast<uint8_t>(Fast::F3DEX_G_SETOTHERMODE_H)) << 24) |
                    (G_MDSFT_CYCLETYPE << 8) | 2;
    gfx->words.w1 = G_CYC_FILL;
    gfx++;
    gDPSetFillColor(gfx++, (GPACK_RGBA5551(255, 0, 255, 1) << 16) | GPACK_RGBA5551(255, 0, 255, 1));
    gDPFillRectangle(gfx++, 0, 0, 319, 239);
    gDPPipeSync(gfx++);
    gDPFullSync(gfx++);
    gfx->words.w0 = static_cast<uint32_t>(static_cast<uint8_t>(Fast::F3DEX_G_ENDDL)) << 24;
    gfx->words.w1 = 0;
    gfx++;

    // DrawAndRunGraphicsCommands runs the interpreter's own StartFrame/EndFrame,
    // so the loop must not call them again. It doesn't poll window events,
    // though; without HandleEvents() the close button never reaches IsRunning().
    while (window->IsRunning()) {
        window->HandleEvents();
        window->DrawAndRunGraphicsCommands(clearList, {});
    }

    window->Close();
    return 0;
}
