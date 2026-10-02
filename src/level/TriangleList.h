#pragma once

#include <cstddef>
#include <cstdint>

#include "gfx/GbiF3dex.h"
#include "fast/indy.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Level {

inline uint8_t Opcode(const Gfx& command) {
    return static_cast<uint8_t>(command.words.w0 >> 24);
}

// How many Gfx entries the command at `command` takes: a command that names
// its operand by hash is followed by the pair that holds it.
inline size_t CommandWidth(const Gfx& command) {
    return F3dex::IsExpandedCommand(Opcode(command)) ? 2 : 1;
}

inline bool IsEndOfList(const Gfx& command) {
    return Opcode(command) == static_cast<uint8_t>(Fast::F3DEX_G_ENDDL);
}

inline bool IsVertexLoad(const Gfx& command) {
    return Opcode(command) == static_cast<uint8_t>(G_VTX_OTR_HASH);
}

inline bool IsImageLoad(const Gfx& command) {
    return Opcode(command) == static_cast<uint8_t>(G_SETTIMG_OTR_HASH);
}

// The hash a two-entry command names its operand by (`command` is its second
// entry).
inline uint64_t OperandHash(const Gfx& second) {
    return (static_cast<uint64_t>(second.words.w0) << 32) | second.words.w1;
}

// A vertex load: how many vertices, the buffer slot the first lands in, and
// the first one's address in the vertex array the list was made for (or, until
// the interpreter first runs the load, its offset into the block).
struct VertexLoad {
    uint32_t count;
    uint32_t firstSlot;
    uintptr_t operand;
};
inline VertexLoad DecodeVertexLoad(const Gfx& command) {
    const uint32_t count = (command.words.w0 >> 12) & 0xFF;
    const uint32_t end = (command.words.w0 >> 1) & 0x7F;
    return { count, end - count, command.words.w1 };
}

inline bool IsTriangle(const Gfx& command) {
    const uint8_t opcode = Opcode(command);
    return opcode == static_cast<uint8_t>(Fast::F3DEX_G_TRI1) || opcode == static_cast<uint8_t>(Fast::INDY_G_TRI4);
}

// extract_vertex_indices_from_triangle: the three vertex slots of a G_TRI1
// (type 0) or of one triangle (1 to 4) of a G_TRI4.
inline void TriangleSlots(const Gfx* gfx, uint32_t type, int (&slot)[3]) {
    const uint32_t w0 = gfx->words.w0;
    const uint32_t w1 = static_cast<uint32_t>(gfx->words.w1);
    switch (type) {
        case 0:
            slot[0] = static_cast<int>((w1 >> 16) & 0xFF) / 10;
            slot[1] = static_cast<int>((w1 >> 8) & 0xFF) / 10;
            slot[2] = static_cast<int>(w1 & 0xFF) / 10;
            break;
        case 1:
            slot[0] = w1 & 0xF;
            slot[1] = (w1 & 0xFF) >> 4;
            slot[2] = w0 & 0xF;
            break;
        case 2:
            slot[0] = (w1 >> 8) & 0xF;
            slot[1] = (w1 & 0xFFFF) >> 12;
            slot[2] = (w0 & 0xFF) >> 4;
            break;
        case 3:
            slot[0] = (w1 >> 16) & 0xF;
            slot[1] = ((w1 >> 16) & 0xFF) >> 4;
            slot[2] = (w0 >> 8) & 0xF;
            break;
        default:
            slot[0] = (w1 >> 24) & 0xF;
            slot[1] = w1 >> 28;
            slot[2] = (w0 & 0xFFFF) >> 12;
            break;
    }
}

} // namespace Frigate::Level
