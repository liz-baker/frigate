// Gfx-builder helpers for the indy ucode's command numbering.
#pragma once

#include <cstdint>

#include "fast/lus_gbi.h" // defines OPCODE, which f3dex.h needs
#include "fast/f3dex.h"
#include "libultraship/libultra/gbi.h"

// The indy ucode numbers its commands like F3DEX, not F3DEX2, so the stock
// gSP macros would emit opcodes it has no handler for. RDP commands are
// ucode-independent and can stay stock macros.
//
// Not nested under a "Gfx" namespace -- that would shadow the `Gfx`
// display-list command type (libultraship's libultra/gbi.h) that every
// function here returns.
namespace Frigate::F3dex {

inline Gfx Command(int8_t opcode, uint32_t w0Low, uintptr_t w1) {
    Gfx cmd;
    cmd.words.w0 = (static_cast<uint32_t>(static_cast<uint8_t>(opcode)) << 24) | w0Low;
    cmd.words.w1 = w1;
    return cmd;
}

inline Gfx SetGeometryMode(uint32_t bits) {
    return Command(Fast::F3DEX_G_SETGEOMETRYMODE, 0, bits);
}

inline Gfx ClearGeometryMode(uint32_t bits) {
    return Command(Fast::F3DEX_G_CLEARGEOMETRYMODE, 0, bits);
}

inline Gfx SetOtherModeL(uint32_t shift, uint32_t length, uint32_t value) {
    return Command(Fast::F3DEX_G_SETOTHERMODE_L, (shift << 8) | length, value);
}

inline Gfx SetOtherModeH(uint32_t shift, uint32_t length, uint32_t value) {
    return Command(Fast::F3DEX_G_SETOTHERMODE_H, (shift << 8) | length, value);
}

inline Gfx Matrix(const void* fixedMtx, uint8_t params) {
    return Command(Fast::F3DEX_G_MTX, static_cast<uint32_t>(params) << 16, reinterpret_cast<uintptr_t>(fixedMtx));
}

inline Gfx Viewport(const void* vp) {
    return Command(Fast::F3DEX_G_MOVEMEM, static_cast<uint32_t>(Fast::F3DEX_G_MV_VIEWPORT) << 16,
                   reinterpret_cast<uintptr_t>(vp));
}

// gSPTexture().
inline Gfx Texture(uint16_t s, uint16_t t, uint8_t level, uint8_t tile, uint8_t on) {
    return Command(Fast::F3DEX_G_TEXTURE,
                   (static_cast<uint32_t>(level) << 11) | (static_cast<uint32_t>(tile) << 8) | on,
                   (static_cast<uint32_t>(s) << 16) | t);
}

// Full-scale tile 0 texturing on.
inline Gfx TextureOn() {
    return Command(Fast::F3DEX_G_TEXTURE, 1, (0xffffu << 16) | 0xffffu);
}

inline Gfx TextureOff() {
    return Command(Fast::F3DEX_G_TEXTURE, 0, (0xffffu << 16) | 0xffffu);
}

inline Gfx EndDisplayList() {
    return Command(Fast::F3DEX_G_ENDDL, 0, 0);
}

} // namespace Frigate::F3dex
