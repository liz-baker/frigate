#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int format, width, height, method, bytes;
} GameTextureInfo;

// Decodes the base level of a non-paletted texture with the game's own code. `bitstream` is the texture-bank entry
// after its one-byte header; `dst` receives the pixels in the game's row-padded layout with halfwords and words
// big-endian. Returns the byte count, or 0 for a texture the game code is not run on.
int GameDecodeTexture(const uint8_t* bitstream, uint8_t* dst, size_t capacity, GameTextureInfo* info);

#ifdef __cplusplus
}
#endif
