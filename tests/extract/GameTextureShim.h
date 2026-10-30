// The declarations the decomp's texture decompression expects around it, for GameTextureOracle.c.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

#define TRUE 1
#define FALSE 0
#define ARRAYCOUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Which byte of a halfword holds its low bits when the halfword is viewed as bytes (1 on the game's CPU).
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define TEX_LOW_BYTE 1
#else
#define TEX_LOW_BYTE 0
#endif

struct tex {
    int texturenum, width, height, maxlod, gbiformat, depth, lutmodeindex, hasExplicitLods;
};

struct texpool {
    struct tex* rightpos;
};

struct texcacheitem {
    s16 texturenum;
    u8 widths[7];
    u8 heights[7];
};

static struct texcacheitem g_TexCacheItems[150];
static s32 g_TexCacheCount;

// Only copied into the tex record, which nothing here reads.
static s32 g_TexFormatGbiMappings[13];
static s32 g_TexFormatDepths[13];
static s32 g_TexFormatLutModes[13];
#define G_MDSFT_TEXTLUT 14

static u8* img_curpos;
static u32 img_curdatatable;
static s32 img_bitcount;
