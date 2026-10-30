// The GoldenEye decomp's own non-paletted texture decompression, compiled for the host so the extractor's decoder can
// be checked against game code (docs/extract-verification.md). game_texture_decoder.inc is generated from the decomp
// by gen_game_texture_decoder.py and is not committed.

#include "GameTextureShim.h"
#include "GameTextureOracle.h"

#include "game_texture_decoder.inc"

int GameDecodeTexture(const uint8_t* bitstream, uint8_t* dst, size_t capacity, GameTextureInfo* info) {
    static struct tex record;
    struct texpool pool = { &record };

    memset(dst, 0, capacity);
    info->format = bitstream[0] >> 4;
    info->width = ((bitstream[0] & 0xF) << 4) | (bitstream[1] >> 4);
    info->height = ((bitstream[1] & 0xF) << 4) | (bitstream[2] >> 4);
    info->method = bitstream[2] & 0xF;
    info->bytes = 0;
    if (info->format > TEXFORMAT_I4 || info->method > TEXCOMPMETHOD_RLEBLUR || info->width * info->height > 0x2000) {
        return 0;
    }

    // One image, no explicit levels, none synthesised: the base level as decoded, before the game swaps alternate
    // rows for the hardware.
    int bytes = texInflateNonZlib((u8*)bitstream, dst, 0, 0, &pool);
    info->bytes = bytes;

#if !(defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    // The game stores whole words; restore the byte order they have in memory on the game's CPU.
    size_t unit = 1;
    if (info->format == TEXFORMAT_RGBA32 || info->format == TEXFORMAT_RGB24) {
        unit = 4;
    } else if (info->format == TEXFORMAT_RGBA16 || info->format == TEXFORMAT_RGB15 || info->format == TEXFORMAT_IA16) {
        unit = 2;
    }
    for (size_t i = 0; unit > 1 && i + unit <= (size_t)bytes; i += unit) {
        for (size_t j = 0; j < unit / 2; j++) {
            uint8_t t = dst[i + j];
            dst[i + j] = dst[i + unit - 1 - j];
            dst[i + unit - 1 - j] = t;
        }
    }
#endif
    return bytes;
}
