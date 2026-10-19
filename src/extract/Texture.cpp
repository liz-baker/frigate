#include "extract/Texture.h"

#include "extract/Inflate.h"

#include <array>
#include <string>

namespace Frigate::Extract {

namespace {

Error Bad(const std::string& what) {
    return Error::Make(ErrorCode::Corrupt, "A texture in the ROM is damaged: " + what + ".");
}

// MSB-first, like the game's own reader. A read past the end returns 0 and sets failed(); callers check it at
// loop boundaries.
class BitReader {
  public:
    explicit BitReader(std::span<const uint8_t> data) : mData(data) {
    }

    uint32_t Read(unsigned bits) {
        while (mAvailable < bits) {
            if (mPosition >= mData.size()) {
                mFailed = true;
                return 0;
            }
            mBuffer = (mBuffer << 8) | mData[mPosition++];
            mAvailable += 8;
        }
        mAvailable -= bits;
        return uint32_t((mBuffer >> mAvailable) & ((uint64_t(1) << bits) - 1));
    }

    bool failed() const {
        return mFailed;
    }
    // Only meaningful while no bits are buffered, which holds at every call site: each field read before the
    // DEFLATE stream is a multiple of 8 bits.
    size_t bytePosition() const {
        return mPosition;
    }

  private:
    std::span<const uint8_t> mData;
    uint64_t mBuffer = 0;
    unsigned mAvailable = 0;
    size_t mPosition = 0;
    bool mFailed = false;
};

enum class Compression : uint32_t {
    Uncompressed0 = 0,
    Uncompressed1 = 1,
    Huffman = 2,
    HuffmanPerChannel = 3,
    Rle = 4,
    Lookup = 5,
    HuffmanLookup = 6,
    RleLookup = 7,
    HuffmanBlur = 8,
    RleBlur = 9,
};

struct FormatInfo {
    unsigned channels;
    bool oneBitAlpha;
    uint32_t channelSize; // alphabet size
    unsigned bitsPerPixel;
};

FormatInfo InfoOf(TexFormat format) {
    switch (format) {
        case TexFormat::RGBA32:
            return { 4, false, 256, 32 };
        case TexFormat::RGBA16:
            return { 3, true, 32, 16 };
        case TexFormat::RGB24:
            return { 3, false, 256, 24 };
        case TexFormat::RGB15:
            return { 3, false, 32, 15 };
        case TexFormat::IA16:
            return { 2, false, 256, 16 };
        case TexFormat::IA8:
            return { 2, false, 16, 8 };
        case TexFormat::IA4:
            return { 1, true, 8, 4 };
        case TexFormat::I8:
            return { 1, false, 256, 8 };
        case TexFormat::I4:
            return { 1, false, 16, 4 };
        default:
            return { 0, false, 0, 0 };
    }
}

constexpr int kLeaf = 10000;
constexpr int kUsed = 9999;

// The decoder has to rebuild the tree the encoder assigned codes against, so the tie-breaking and merge order
// below are the format's, not an implementation choice.
bool InflateHuffman(BitReader& reader, size_t numSymbols, size_t alphabet, std::vector<uint32_t>& out) {
    if (alphabet == 0) {
        return numSymbols == 0;
    }
    std::vector<int> frequencies(alphabet);
    for (int& f : frequencies) {
        f = int(reader.Read(8));
    }
    if (reader.failed()) {
        return false;
    }
    std::vector<std::array<int, 2>> children(alphabet, { -1, -1 });
    auto unassigned = [&](int i) { return children[size_t(i)][0] == -1 && children[size_t(i)][1] == -1; };

    int min1, min2, idx1, idx2;
    auto findTwoSmallest = [&]() {
        min1 = min2 = kUsed;
        idx1 = idx2 = -1;
        for (size_t i = 0; i < alphabet; i++) {
            if (frequencies[i] < min1) {
                if (min1 > min2) {
                    min1 = frequencies[i];
                    idx1 = int(i);
                } else {
                    min2 = frequencies[i];
                    idx2 = int(i);
                }
            } else if (frequencies[i] < min2) {
                min2 = frequencies[i];
                idx2 = int(i);
            }
        }
    };

    findTwoSmallest();
    int root = 0;
    while (min1 != kUsed && min2 != kUsed) {
        int total = frequencies[size_t(idx1)] + frequencies[size_t(idx2)];
        if (total == 0) {
            total = 1;
        }
        frequencies[size_t(idx1)] = kUsed;
        frequencies[size_t(idx2)] = kUsed;

        if (unassigned(idx1)) {
            children[size_t(idx1)][0] = idx1 + kLeaf;
            root = idx1;
            frequencies[size_t(idx1)] = total;
            children[size_t(idx1)][1] = unassigned(idx2) ? idx2 + kLeaf : idx2;
        } else if (unassigned(idx2)) {
            children[size_t(idx2)][0] = idx2 + kLeaf;
            root = idx2;
            frequencies[size_t(idx2)] = total;
            children[size_t(idx2)][1] = unassigned(idx1) ? idx1 + kLeaf : idx1;
        } else {
            int node = -1;
            for (size_t i = 0; i < alphabet; i++) {
                if (unassigned(int(i)) && frequencies[i] >= kUsed) {
                    node = int(i);
                    break;
                }
            }
            if (node < 0) {
                return false;
            }
            frequencies[size_t(node)] = total;
            children[size_t(node)][0] = idx1;
            children[size_t(node)][1] = idx2;
            root = node;
        }
        findTwoSmallest();
    }

    out.resize(numSymbols);
    for (size_t i = 0; i < numSymbols; i++) {
        int node = root;
        while (node < kLeaf) {
            if (node < 0) {
                return false;
            }
            node = children[size_t(node)][reader.Read(1)];
            if (reader.failed()) {
                return false;
            }
        }
        out[i] = uint32_t(node - kLeaf);
    }
    return true;
}

// A run is always followed by one literal value with no directive bit of its own; every other value is
// directive-prefixed. The output may overshoot `numValues` by the last run, which callers rely on.
bool InflateRle(BitReader& reader, size_t numValues, std::vector<uint32_t>& out) {
    unsigned backBits = reader.Read(3);
    unsigned runBits = reader.Read(3);
    unsigned valueBits = reader.Read(4);

    int cost = int(backBits + runBits + valueBits + 1);
    uint32_t fudge = 0;
    while (cost > 0) {
        cost -= int(valueBits + 1);
        fudge++;
    }

    out.clear();
    while (out.size() < numValues) {
        if (reader.failed()) {
            return false;
        }
        if (reader.Read(1) == 0) {
            out.push_back(reader.Read(valueBits));
        } else {
            int64_t start = int64_t(out.size()) - int64_t(reader.Read(backBits)) - 1;
            uint32_t runLength = reader.Read(runBits) + fudge;
            if (start < 0) {
                return false;
            }
            for (uint32_t i = 0; i < runLength; i++) {
                out.push_back(out[size_t(start) + i]);
            }
            out.push_back(reader.Read(valueBits));
        }
    }
    return !reader.failed();
}

// Each value is a delta against a prediction from its already-reconstructed neighbours, so this reads and writes
// the same array in scan order. The halving truncates toward zero like C.
bool Blur(std::vector<uint32_t>& values, size_t width, size_t height, uint32_t method, uint32_t alphabet) {
    if (method > 6 && width != 0 && height != 0) {
        return false;
    }
    auto at = [&](int64_t x, int64_t y) -> int64_t {
        return (x >= 0 && y >= 0) ? int64_t(values[size_t(y) * width + size_t(x)]) : 0;
    };
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            int64_t cur = int64_t(values[y * width + x]) + int64_t(alphabet) * 2;
            int64_t left = at(int64_t(x) - 1, int64_t(y));
            int64_t above = at(int64_t(x), int64_t(y) - 1);
            int64_t aboveLeft = at(int64_t(x) - 1, int64_t(y) - 1);
            int64_t predicted = 0;
            switch (method) {
                case 0:
                    predicted = left;
                    break;
                case 1:
                    predicted = above;
                    break;
                case 2:
                    predicted = aboveLeft;
                    break;
                case 3:
                    predicted = left + above - aboveLeft;
                    break;
                case 4:
                    predicted = (above - aboveLeft) / 2 + left;
                    break;
                case 5:
                    predicted = (left - aboveLeft) / 2 + above;
                    break;
                case 6:
                    predicted = (left + above) / 2;
                    break;
            }
            int64_t sum = (cur + predicted) % int64_t(alphabet);
            if (sum < 0) {
                sum += int64_t(alphabet);
            }
            values[y * width + x] = uint32_t(sum);
        }
    }
    return true;
}

// Two nibbles per byte, starting a fresh byte on each row: an odd row's last nibble is zero-padded, never carried
// into the next row.
std::vector<uint8_t> PackNibbles(const std::vector<uint8_t>& nibbles, size_t width, size_t height) {
    std::vector<uint8_t> out;
    out.reserve(((width + 1) / 2) * height);
    for (size_t y = 0; y < height; y++) {
        const uint8_t* row = nibbles.data() + y * width;
        for (size_t i = 0; i < width; i += 2) {
            uint8_t hi = row[i] & 0xF;
            uint8_t lo = (i + 1 < width) ? (row[i + 1] & 0xF) : 0;
            out.push_back(uint8_t((hi << 4) | lo));
        }
    }
    return out;
}

void Put16(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
}

// The colour planes, then the one-bit alpha plane when the format has one.
std::vector<uint8_t> ChannelsToNative(const std::array<const uint32_t*, 4>& ch, TexFormat format, size_t width,
                                      size_t height) {
    size_t n = width * height;
    std::vector<uint8_t> out;
    switch (format) {
        case TexFormat::RGBA32:
            out.reserve(n * 4);
            for (size_t i = 0; i < n; i++) {
                out.push_back(uint8_t(ch[0][i]));
                out.push_back(uint8_t(ch[1][i]));
                out.push_back(uint8_t(ch[2][i]));
                out.push_back(uint8_t(ch[3][i]));
            }
            break;
        case TexFormat::RGB24:
            out.reserve(n * 4);
            for (size_t i = 0; i < n; i++) {
                out.push_back(uint8_t(ch[0][i]));
                out.push_back(uint8_t(ch[1][i]));
                out.push_back(uint8_t(ch[2][i]));
                out.push_back(0xFF);
            }
            break;
        case TexFormat::RGBA16:
            out.reserve(n * 2);
            for (size_t i = 0; i < n; i++) {
                Put16(out, (ch[0][i] << 11) | (ch[1][i] << 6) | (ch[2][i] << 1) | ch[3][i]);
            }
            break;
        case TexFormat::RGB15:
            out.reserve(n * 2);
            for (size_t i = 0; i < n; i++) {
                Put16(out, (ch[0][i] << 11) | (ch[1][i] << 6) | (ch[2][i] << 1) | 1);
            }
            break;
        case TexFormat::IA16:
            out.reserve(n * 2);
            for (size_t i = 0; i < n; i++) {
                out.push_back(uint8_t(ch[0][i]));
                out.push_back(uint8_t(ch[1][i]));
            }
            break;
        case TexFormat::IA8:
            out.reserve(n);
            for (size_t i = 0; i < n; i++) {
                out.push_back(uint8_t(((ch[0][i] & 0xF) << 4) | (ch[1][i] & 0xF)));
            }
            break;
        case TexFormat::IA4: {
            std::vector<uint8_t> nibbles(n);
            for (size_t i = 0; i < n; i++) {
                nibbles[i] = uint8_t(((ch[0][i] & 0x7) << 1) | (ch[1][i] & 1));
            }
            return PackNibbles(nibbles, width, height);
        }
        case TexFormat::I8:
            out.reserve(n);
            for (size_t i = 0; i < n; i++) {
                out.push_back(uint8_t(ch[0][i]));
            }
            break;
        case TexFormat::I4: {
            std::vector<uint8_t> nibbles(n);
            for (size_t i = 0; i < n; i++) {
                nibbles[i] = uint8_t(ch[0][i] & 0xF);
            }
            return PackNibbles(nibbles, width, height);
        }
        default:
            break;
    }
    return out;
}

// A LOOKUP table's entries are already native pixel words; this fixes the byte order per format.
void AppendLookupEntry(std::vector<uint8_t>& out, TexFormat format, uint32_t entry) {
    switch (format) {
        case TexFormat::RGBA32:
            out.push_back(uint8_t(entry >> 24));
            out.push_back(uint8_t(entry >> 16));
            out.push_back(uint8_t(entry >> 8));
            out.push_back(uint8_t(entry));
            break;
        case TexFormat::RGB24:
            out.push_back(uint8_t(entry >> 16));
            out.push_back(uint8_t(entry >> 8));
            out.push_back(uint8_t(entry));
            out.push_back(0xFF);
            break;
        case TexFormat::RGBA16:
        case TexFormat::IA16:
            Put16(out, entry);
            break;
        case TexFormat::RGB15:
            Put16(out, (entry << 1) | 1);
            break;
        default:
            out.push_back(uint8_t(entry));
            break;
    }
}

bool AssembleFromLookup(TexFormat format, const std::vector<uint32_t>& table, const std::vector<uint32_t>& indices,
                        size_t width, size_t height, std::vector<uint8_t>& out) {
    for (uint32_t index : indices) {
        if (index >= table.size()) {
            return false;
        }
    }
    size_t n = width * height;
    if (format == TexFormat::IA4 || format == TexFormat::I4) {
        std::vector<uint8_t> nibbles(n);
        for (size_t i = 0; i < n; i++) {
            nibbles[i] = uint8_t(table[indices[i]] & 0xF);
        }
        out = PackNibbles(nibbles, width, height);
        return true;
    }
    out.clear();
    for (uint32_t index : indices) {
        AppendLookupEntry(out, format, table[index]);
    }
    return true;
}

// UNCOMPRESSED0/1 read one already-packed native pixel word per pixel, not channel planes.
bool ReadUncompressed(BitReader& reader, TexFormat format, size_t width, size_t height, std::vector<uint8_t>& out) {
    size_t n = width * height;
    out.clear();
    switch (format) {
        case TexFormat::RGBA32:
            for (size_t i = 0; i < n; i++) {
                AppendLookupEntry(out, format, reader.Read(32));
            }
            break;
        case TexFormat::RGB24:
            for (size_t i = 0; i < n; i++) {
                AppendLookupEntry(out, format, reader.Read(24));
            }
            break;
        case TexFormat::RGBA16:
        case TexFormat::IA16:
            for (size_t i = 0; i < n; i++) {
                Put16(out, reader.Read(16));
            }
            break;
        case TexFormat::RGB15:
            for (size_t i = 0; i < n; i++) {
                AppendLookupEntry(out, format, reader.Read(15));
            }
            break;
        case TexFormat::IA8:
        case TexFormat::I8:
            for (size_t i = 0; i < n; i++) {
                out.push_back(uint8_t(reader.Read(8)));
            }
            break;
        case TexFormat::IA4:
        case TexFormat::I4: {
            std::vector<uint8_t> nibbles(n);
            for (size_t i = 0; i < n; i++) {
                nibbles[i] = uint8_t(reader.Read(4));
            }
            out = PackNibbles(nibbles, width, height);
            break;
        }
        default:
            return false;
    }
    return !reader.failed();
}

unsigned BitLength(uint32_t value) {
    unsigned bits = 0;
    while (value != 0) {
        bits++;
        value >>= 1;
    }
    return bits;
}

Error DecodeNonPaletted(BitReader& reader, const TextureHeader& header, Compression compression,
                        std::vector<uint8_t>& pixels) {
    const TexFormat format = header.format;
    const FormatInfo info = InfoOf(format);
    const size_t width = header.width, height = header.height, n = width * height;
    const unsigned channels = info.channels;

    auto planesToNative = [&](const std::vector<uint32_t>& flat) -> Error {
        std::vector<uint32_t> alpha;
        std::array<const uint32_t*, 4> ch = {};
        for (unsigned c = 0; c < channels; c++) {
            ch[c] = flat.data() + c * n;
        }
        if (info.oneBitAlpha) {
            alpha.resize(n);
            for (size_t i = 0; i < n; i++) {
                alpha[i] = reader.Read(1);
            }
            if (reader.failed()) {
                return Bad("the bitstream ran out of data");
            }
            ch[channels] = alpha.data();
        }
        pixels = ChannelsToNative(ch, format, width, height);
        return {};
    };

    auto readTable = [&](std::vector<uint32_t>& table) {
        size_t entries = reader.Read(11);
        table.resize(entries);
        for (uint32_t& entry : table) {
            entry = reader.Read(info.bitsPerPixel);
        }
    };

    std::vector<uint32_t> flat, table, indices;
    bool ok = true;
    switch (compression) {
        case Compression::Uncompressed0:
        case Compression::Uncompressed1:
            ok = ReadUncompressed(reader, format, width, height, pixels);
            break;

        case Compression::Huffman:
            ok = InflateHuffman(reader, channels * n, info.channelSize, flat);
            if (ok) {
                return planesToNative(flat);
            }
            break;

        case Compression::HuffmanPerChannel:
            flat.reserve(channels * n);
            for (unsigned c = 0; c < channels && ok; c++) {
                std::vector<uint32_t> plane;
                ok = InflateHuffman(reader, n, info.channelSize, plane);
                flat.insert(flat.end(), plane.begin(), plane.end());
            }
            if (ok) {
                return planesToNative(flat);
            }
            break;

        case Compression::Rle:
            ok = InflateRle(reader, channels * n, flat);
            if (ok) {
                return planesToNative(flat);
            }
            break;

        case Compression::Lookup: {
            readTable(table);
            unsigned bits = table.empty() ? 1 : BitLength(uint32_t(table.size() - 1));
            indices.resize(n);
            for (uint32_t& index : indices) {
                index = reader.Read(bits);
            }
            ok = !reader.failed() && AssembleFromLookup(format, table, indices, width, height, pixels);
            break;
        }

        case Compression::HuffmanLookup:
            readTable(table);
            ok = !reader.failed() && InflateHuffman(reader, n, table.size(), indices) &&
                 AssembleFromLookup(format, table, indices, width, height, pixels);
            break;

        case Compression::RleLookup:
            readTable(table);
            ok = !reader.failed() && InflateRle(reader, n, indices) &&
                 AssembleFromLookup(format, table, indices, width, height, pixels);
            break;

        case Compression::HuffmanBlur:
        case Compression::RleBlur: {
            uint32_t method = reader.Read(3);
            ok = compression == Compression::HuffmanBlur ? InflateHuffman(reader, channels * n, info.channelSize, flat)
                                                         : InflateRle(reader, channels * n, flat);
            if (ok && Blur(flat, width, channels * height, method, info.channelSize)) {
                return planesToNative(flat);
            }
            ok = false;
            break;
        }
    }
    if (!ok) {
        return Bad("its pixel data does not decode");
    }
    return {};
}

// The lossy approximation of an IA16 palette entry that libultraship's CI importers (which read only an RGBA5551
// TLUT) can use.
uint16_t Ia16ToRgba5551(uint16_t word) {
    uint32_t intensity = (word >> 8) & 0xFF;
    uint32_t alpha = word & 0xFF;
    uint32_t channel5 = intensity >> 3;
    uint32_t alpha1 = alpha >= 128 ? 1 : 0;
    return uint16_t((channel5 << 11) | (channel5 << 6) | (channel5 << 1) | alpha1);
}

struct ParsedHeader {
    TextureHeader header;
    Compression compression = Compression::Uncompressed0;
    std::vector<uint16_t> paletteNative;
    size_t deflateStart = 0;
};

Error ParseHeader(BitReader& reader, ParsedHeader& parsed) {
    TextureHeader& header = parsed.header;
    header.hasExplicitLods = reader.Read(1) != 0;
    header.paletted = reader.Read(1) != 0;
    header.lodCount = reader.Read(6);

    if (header.paletted) {
        uint32_t format = reader.Read(8);
        if (format < uint32_t(TexFormat::RGBA16_CI8) || format > uint32_t(TexFormat::IA16_CI4)) {
            return Bad("the paletted branch holds a non-paletted format");
        }
        header.format = TexFormat(format);
        size_t colours = reader.Read(8) + 1;
        parsed.paletteNative.resize(colours);
        for (uint16_t& word : parsed.paletteNative) {
            word = uint16_t(reader.Read(16));
        }
        header.width = reader.Read(8);
        header.height = reader.Read(8);
        parsed.deflateStart = reader.bytePosition();
    } else {
        uint32_t format = reader.Read(4);
        header.width = reader.Read(8);
        header.height = reader.Read(8);
        uint32_t compression = reader.Read(4);
        if (format > uint32_t(TexFormat::I4) || compression > uint32_t(Compression::RleBlur)) {
            return Bad("it has an unknown pixel format or compression method");
        }
        header.format = TexFormat(format);
        parsed.compression = Compression(compression);
    }
    if (reader.failed()) {
        return Bad("the bitstream ran out of data");
    }
    return {};
}

} // namespace

bool IsPaletted(TexFormat format) {
    return format >= TexFormat::RGBA16_CI8;
}

TextureType TextureTypeOf(TexFormat format) {
    switch (format) {
        case TexFormat::RGBA32:
        case TexFormat::RGB24:
            return TextureType::RGBA32bpp;
        case TexFormat::RGBA16:
        case TexFormat::RGB15:
            return TextureType::RGBA16bpp;
        case TexFormat::IA16:
            return TextureType::GrayscaleAlpha16bpp;
        case TexFormat::IA8:
            return TextureType::GrayscaleAlpha8bpp;
        case TexFormat::IA4:
            return TextureType::GrayscaleAlpha4bpp;
        case TexFormat::I8:
            return TextureType::Grayscale8bpp;
        case TexFormat::I4:
            return TextureType::Grayscale4bpp;
        case TexFormat::RGBA16_CI8:
        case TexFormat::IA16_CI8:
            return TextureType::Palette8bpp;
        case TexFormat::RGBA16_CI4:
        case TexFormat::IA16_CI4:
            return TextureType::Palette4bpp;
    }
    return TextureType::RGBA32bpp;
}

Error ReadTextureHeader(std::span<const uint8_t> data, TextureHeader& header) {
    if (data.empty()) {
        return Bad("it is empty");
    }
    BitReader reader(data);
    ParsedHeader parsed;
    if (Error err = ParseHeader(reader, parsed); !err.ok()) {
        return err;
    }
    header = parsed.header;
    return {};
}

Error DecodeTexture(std::span<const uint8_t> data, DecodedTexture& texture) {
    if (data.empty()) {
        return Bad("it is empty");
    }
    BitReader reader(data);
    ParsedHeader parsed;
    if (Error err = ParseHeader(reader, parsed); !err.ok()) {
        return err;
    }
    texture = DecodedTexture();
    texture.header = parsed.header;
    texture.textureType = TextureTypeOf(parsed.header.format);

    if (!parsed.header.paletted) {
        return DecodeNonPaletted(reader, parsed.header, parsed.compression, texture.pixels);
    }

    // "rarezip" is plain raw DEFLATE despite the decomp's "zlib" naming.
    std::span<const uint8_t> rest = data.subspan(parsed.deflateStart);
    size_t skip;
    if (rest.size() >= 2 && rest[0] == 0x11 && rest[1] == 0x73) {
        skip = 5;
    } else if (rest.size() >= 2 && rest[0] == 0x11 && rest[1] == 0x72) {
        skip = 2;
    } else {
        return Bad("the pixel data has no recognised compression header");
    }
    if (rest.size() < skip) {
        return Bad("the pixel data is cut short");
    }
    std::vector<uint8_t> indices;
    if (Error err = InflateRaw(rest.subspan(skip), indices); !err.ok()) {
        return err;
    }

    const TexFormat format = parsed.header.format;
    const bool ci4 = format == TexFormat::RGBA16_CI4 || format == TexFormat::IA16_CI4;
    const size_t rowBytes = ci4 ? (parsed.header.width + 1) / 2 : parsed.header.width;
    const size_t want = rowBytes * parsed.header.height;
    if (indices.size() > want) {
        indices.resize(want);
    }
    texture.pixels = std::move(indices);

    const bool ia = format == TexFormat::IA16_CI8 || format == TexFormat::IA16_CI4;
    texture.paletteNative = std::move(parsed.paletteNative);
    texture.paletteRgba5551 = texture.paletteNative;
    if (ia) {
        for (uint16_t& word : texture.paletteRgba5551) {
            word = Ia16ToRgba5551(word);
        }
    }
    return {};
}

} // namespace Frigate::Extract
