#include "extract/DisplayListEmit.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

namespace Frigate::Extract {

namespace {

Error EmitError(const std::string& what) {
    return Error::Make(ErrorCode::Corrupt, "A display list in the ROM cannot be converted: " + what + ".");
}

std::string Hex6(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%06x", value);
    return text;
}

std::string Hex(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%x", value);
    return text;
}

std::array<uint64_t, 256> MakeCrc64Table() {
    std::array<uint64_t, 256> table = {};
    for (uint64_t byte = 0; byte < 256; byte++) {
        uint64_t crc = byte << 56;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 63) ? ((crc << 1) ^ 0x42F0E1EBA9EA3693ull) : (crc << 1);
        }
        table[byte] = crc;
    }
    return table;
}

uint32_t Read32(std::span<const uint8_t> data, size_t at) {
    return (uint32_t(data[at]) << 24) | (uint32_t(data[at + 1]) << 16) | (uint32_t(data[at + 2]) << 8) |
           uint32_t(data[at + 3]);
}

void Append64(std::vector<uint8_t>& out, uint64_t value) {
    AppendWord(out, uint32_t(value >> 32));
    AppendWord(out, uint32_t(value));
}

constexpr uint32_t kTextureIdMask = 0xFFF;
constexpr uint32_t kRequestTypeMask = 7;
constexpr uint32_t kRequestTypeDetail = 1;
constexpr uint32_t kOffsetMask = 0x00FFFFFF;
// Set in a texture reference's w1 when the game sets G_TEXTURE's level for the request, as it does for the model
// and room lists it rewrites on load.
constexpr uint32_t kReferenceSetsTextureLevel = 1u << 16;
// The segment the models' node matrices are in, one N64 fixed-point Mtx each, and the bit that marks an address as
// segmented for the interpreter.
constexpr uint32_t kModelMatrixSegment = 0x03;
constexpr uint32_t kMatrixSize = 0x40;
constexpr uint32_t kSegmentedBit = 1;
// The images the game follows with a call to a list that scrolls them: the Caverns water, and the water in Dam and
// Complex.
constexpr std::array<uint32_t, 2> kWaterTextures = { 1508, 1511 };
constexpr std::array<uint32_t, 4> kBitsPerTexel = { 4, 8, 16, 32 };

void AppendTextureReference(std::vector<uint8_t>& out, const TextureBinding& binding, uint32_t flags) {
    AppendReferenceCommand(out, kOtrG_SETTIMG_HASH, (binding.fmt << 21) | (binding.size << 19) | (binding.width - 1),
                           binding.path, uint32_t(binding.detail) | flags);
}

struct Scope {
    std::span<const uint8_t> data;
    // The walk's references by the offset of the command that holds them.
    std::unordered_map<uint32_t, VertexRef> vertices;
    std::unordered_map<uint32_t, TextureRef> timgs;
    std::unordered_map<uint32_t, AddrRef> children;
    const ReemitSpec* spec;
    ReemittedLists* result;
};

Error RecordImage(Scope& scope, uint32_t target, uint32_t fmt, uint32_t size, uint32_t width, uint32_t texels) {
    std::string path = scope.spec->imagePath(target);
    size_t length = (size_t(texels) * kBitsPerTexel[size] + 7) / 8;
    if (size_t(target) + length > scope.data.size()) {
        return EmitError("an image runs off the file");
    }
    EmbeddedImage* known = scope.result->FindImage(path);
    if (known != nullptr && (known->fmt != fmt || known->size != size || known->width != width)) {
        return EmitError("an image is set up with two different formats");
    }
    if (known == nullptr || length > known->data.size()) {
        EmbeddedImage image;
        image.path = path;
        image.offset = target;
        image.fmt = fmt;
        image.size = size;
        image.width = width;
        image.texels = texels;
        image.data.assign(scope.data.begin() + target, scope.data.begin() + std::ptrdiff_t(target + length));
        scope.result->SetImage(std::move(image));
    }
    return {};
}

Error ScanList(Scope& scope, uint32_t start, EmittedList& emitted) {
    std::vector<uint8_t> out(kGfxSize, 0);
    out[0] = kUcodeIndyGe;
    struct Pending {
        uint32_t offset, fmt, size, width;
    };
    std::optional<Pending> pending;
    size_t offset = start;
    for (;;) {
        if (offset + kGfxSize > scope.data.size()) {
            return EmitError("a list runs off its buffer");
        }
        uint32_t w0 = Read32(scope.data, offset), w1 = Read32(scope.data, offset + 4);
        uint32_t opcode = w0 >> 24;
        uint32_t cmdOffset = uint32_t(offset);
        offset += kGfxSize;

        if (opcode == kG_VTX) {
            auto it = scope.vertices.find(cmdOffset);
            if (it == scope.vertices.end()) {
                return EmitError("a vertex load was not found by the walk");
            }
            const VertexRef& ref = it->second;
            std::string path;
            uint32_t byteOffset = 0;
            if (Error err = scope.spec->vertexTarget(ref, path, byteOffset); !err.ok()) {
                return err;
            }
            if (ref.count > 0xFF || ref.destIndex + ref.count > 0x7F) {
                return EmitError("a G_VTX does not fit the reference encoding");
            }
            AppendWord(out, (kOtrG_VTX_HASH << 24) | (ref.count << 12) | ((ref.destIndex + ref.count) << 1));
            AppendWord(out, byteOffset);
            Append64(out, PathHash(path));
        } else if (opcode == kG_DL) {
            auto it = scope.children.find(cmdOffset);
            if (it == scope.children.end()) {
                return EmitError("a list call was not found by the walk");
            }
            std::string path = scope.spec->listPath(it->second.offset);
            uint32_t flag = (w0 >> 16) & 0xFF;
            AppendReferenceCommand(out, kOtrG_DL_HASH, flag << 16, path);
            if (flag == kG_DL_NOPUSH) {
                break; // a jump never returns; the walker stops here too
            }
        } else if (opcode == kG_SETTEX) {
            TextureBinding binding;
            if (Error err = (*scope.spec->textures)(w1 & kTextureIdMask, binding); !err.ok()) {
                return err;
            }
            // The original word stays: libultraship ignores it, and the display list factory expands it at load
            // time.
            AppendWord(out, w0);
            AppendWord(out, w1);
            AppendTextureReference(out, binding, kReferenceSetsTextureLevel);
            if ((w0 & kRequestTypeMask) == kRequestTypeDetail) {
                TextureBinding detail;
                if (Error err = (*scope.spec->textures)((w1 >> 12) & kTextureIdMask, detail); !err.ok()) {
                    return err;
                }
                AppendTextureReference(out, detail, 0);
            }
            uint32_t id = w1 & kTextureIdMask;
            if (std::find(kWaterTextures.begin(), kWaterTextures.end(), id) != kWaterTextures.end()) {
                AppendReferenceCommand(out, kOtrG_DL_HASH, 0, "bg/water/" + std::to_string(id));
            }
        } else if (opcode == kG_SETTIMG) {
            if (pending) {
                return EmitError("a G_SETTIMG follows one that was never loaded");
            }
            uint32_t fmt = (w0 >> 21) & 7, size = (w0 >> 19) & 3, width = (w0 & 0xFFF) + 1;
            auto it = scope.timgs.find(cmdOffset);
            if (it == scope.timgs.end()) {
                return EmitError("an image load was not found by the walk");
            }
            uint32_t target = it->second.offset;
            AppendReferenceCommand(out, kOtrG_SETTIMG_HASH, w0 & 0x00FFFFFF, scope.spec->imagePath(target));
            pending = Pending{ target, fmt, size, width };
        } else if (opcode == kG_LOADBLOCK && pending) {
            if (Error err = RecordImage(scope, pending->offset, pending->fmt, pending->size, pending->width,
                                        ((w1 >> 12) & 0xFFF) + 1);
                !err.ok()) {
                return err;
            }
            pending.reset();
            AppendWord(out, w0);
            AppendWord(out, w1);
        } else if (opcode == kG_MTX && (w1 >> 24) == kModelMatrixSegment) {
            if ((w1 & kOffsetMask) % kMatrixSize != 0) {
                return EmitError("a G_MTX is not a whole matrix offset");
            }
            // The interpreter only resolves a segment address whose low bit is set.
            AppendWord(out, w0);
            AppendWord(out, w1 | kSegmentedBit);
        } else if (opcode == kG_ENDDL) {
            AppendWord(out, w0);
            AppendWord(out, w1);
            break;
        } else {
            AppendWord(out, w0);
            AppendWord(out, w1);
        }
    }

    if (pending) {
        return EmitError("a G_SETTIMG is never loaded");
    }
    emitted.path = scope.spec->listPath(start);
    emitted.payload = std::move(out);
    return {};
}

} // namespace

uint64_t PathHash(std::string_view path) {
    static const std::array<uint64_t, 256> table = MakeCrc64Table();
    uint64_t crc = ~uint64_t(0);
    for (char c : path) {
        crc = table[size_t((crc >> 56) ^ uint8_t(c))] ^ (crc << 8);
    }
    return crc;
}

void AppendWord(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(uint8_t(value >> 24));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
}

void AppendReferenceCommand(std::vector<uint8_t>& out, uint32_t opcode, uint32_t flags, std::string_view path,
                            uint32_t w1) {
    AppendWord(out, (opcode << 24) | flags);
    AppendWord(out, w1);
    Append64(out, PathHash(path));
}

uint32_t RequestWrapMode(uint32_t tileFlags) {
    constexpr uint32_t kMirror = 1, kClamp = 2;
    if (tileFlags & kClamp) {
        return 1;
    }
    return (tileFlags & kMirror) ? 2 : 0;
}

std::vector<uint8_t> EncodeTextureRequestList(uint32_t request, uint32_t textureId, const TextureBinding& binding) {
    std::vector<uint8_t> out(kGfxSize, 0);
    out[0] = kUcodeIndyGe;
    AppendWord(out, request);
    AppendWord(out, textureId);
    AppendReferenceCommand(out, kOtrG_SETTIMG_HASH, (binding.fmt << 21) | (binding.size << 19) | (binding.width - 1),
                           binding.path);
    AppendWord(out, kG_ENDDL << 24);
    AppendWord(out, 0);
    return out;
}

DisplayListRefs WalkDisplayList(std::span<const uint8_t> data, uint32_t start, const Resolve& resolve,
                                size_t maxCommands) {
    DisplayListRefs refs;
    std::vector<uint32_t> pending = { start };
    while (!pending.empty()) {
        uint32_t current = pending.back();
        pending.pop_back();
        if (!refs.visitedLists.insert(current).second) {
            continue;
        }

        size_t offset = current;
        for (;;) {
            if (offset + kGfxSize > data.size() || refs.commandCount >= maxCommands) {
                refs.unresolved.push_back(int64_t(offset));
                break;
            }
            uint32_t w0 = Read32(data, offset), w1 = Read32(data, offset + 4);
            uint32_t opcode = w0 >> 24;
            refs.commandCount++;
            if (opcode == kG_ENDDL) {
                break;
            }

            if (opcode == kG_VTX) {
                std::optional<uint32_t> target = resolve(w1);
                if (!target) {
                    refs.unresolved.push_back(int64_t(offset));
                } else {
                    refs.vertices.push_back(
                        { uint32_t(offset), *target, (w0 & 0xFFFF) / uint32_t(kVertexSize), (w0 >> 16) & 0xF });
                }
            } else if (opcode == kG_SETTIMG) {
                std::optional<uint32_t> target = resolve(w1);
                if (!target) {
                    refs.unresolved.push_back(int64_t(offset));
                } else {
                    refs.textures.push_back({ uint32_t(offset), *target, (w0 >> 21) & 0x7, (w0 >> 19) & 0x3 });
                }
            } else if (opcode == kG_MTX || opcode == kG_MOVEMEM) {
                std::optional<uint32_t> target = resolve(w1);
                if (!target) {
                    refs.unresolved.push_back(int64_t(offset));
                } else {
                    (opcode == kG_MTX ? refs.matrices : refs.movemem).push_back({ uint32_t(offset), *target });
                }
            } else if (opcode == kG_DL) {
                std::optional<uint32_t> target = resolve(w1);
                if (!target) {
                    refs.unresolved.push_back(int64_t(offset));
                } else {
                    refs.childLists.push_back({ uint32_t(offset), *target });
                    pending.push_back(*target);
                }
                if (((w0 >> 16) & 0xFF) == kG_DL_NOPUSH) {
                    break; // a jump never returns to this list
                }
            }
            offset += kGfxSize;
        }
    }
    return refs;
}

std::vector<std::pair<uint32_t, uint32_t>> MergeVertexRanges(const DisplayListRefs& refs) {
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    for (const VertexRef& v : refs.vertices) {
        spans.emplace_back(v.offset, v.offset + v.count * uint32_t(kVertexSize));
    }
    std::sort(spans.begin(), spans.end());
    std::vector<std::pair<uint32_t, uint32_t>> merged;
    for (const auto& [begin, end] : spans) {
        if (!merged.empty() && begin <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, end);
        } else {
            merged.emplace_back(begin, end);
        }
    }
    for (auto& [begin, end] : merged) {
        end -= begin; // now (offset, byte length)
    }
    return merged;
}

EmbeddedImage* ReemittedLists::FindImage(std::string_view path) {
    for (EmbeddedImage& image : images) {
        if (image.path == path) {
            return &image;
        }
    }
    return nullptr;
}

void ReemittedLists::SetImage(EmbeddedImage image) {
    if (EmbeddedImage* known = FindImage(image.path)) {
        *known = std::move(image);
    } else {
        images.push_back(std::move(image));
    }
}

Error ReemitLists(const ReemitSpec& spec, ReemittedLists& out) {
    DisplayListRefs combined;
    for (uint32_t root : spec.roots) {
        DisplayListRefs refs = WalkDisplayList(spec.data, root, spec.resolve);
        if (!refs.unresolved.empty()) {
            return EmitError(spec.what + " has a list with unresolved references");
        }
        combined.vertices.insert(combined.vertices.end(), refs.vertices.begin(), refs.vertices.end());
        combined.textures.insert(combined.textures.end(), refs.textures.begin(), refs.textures.end());
        combined.childLists.insert(combined.childLists.end(), refs.childLists.begin(), refs.childLists.end());
        combined.visitedLists.insert(refs.visitedLists.begin(), refs.visitedLists.end());
    }

    Scope scope;
    scope.data = spec.data;
    scope.spec = &spec;
    scope.result = &out;
    for (const VertexRef& v : combined.vertices) {
        scope.vertices[v.cmdOffset] = v;
    }
    for (const TextureRef& t : combined.textures) {
        scope.timgs[t.cmdOffset] = t;
    }
    for (const AddrRef& c : combined.childLists) {
        scope.children[c.cmdOffset] = c;
    }

    for (uint32_t start : combined.visitedLists) {
        EmittedList emitted;
        if (Error err = ScanList(scope, start, emitted); !err.ok()) {
            return err;
        }
        out.lists.push_back(std::move(emitted));
    }
    return {};
}

Error ReemitSegmentLists(const std::string& basePath, std::span<const uint8_t> data, const std::vector<uint32_t>& roots,
                         const Resolve& resolve, const std::vector<std::pair<uint32_t, uint32_t>>& vertexRanges,
                         const TextureLookup& textures, ReemittedLists& out) {
    out = ReemittedLists();

    ReemitSpec spec;
    spec.what = basePath;
    spec.data = data;
    spec.roots = roots;
    spec.resolve = resolve;
    spec.listPath = [&](uint32_t offset) { return basePath + "/dl/" + Hex6(offset); };
    spec.imagePath = [&](uint32_t offset) { return basePath + "/img/" + Hex6(offset); };
    spec.vertexTarget = [&](const VertexRef& ref, std::string& path, uint32_t& byteOffset) -> Error {
        for (const auto& [begin, length] : vertexRanges) {
            if (begin <= ref.offset && ref.offset + ref.count * kVertexSize <= uint64_t(begin) + length) {
                path = basePath + "/vtx/" + Hex6(begin);
                byteOffset = ref.offset - begin;
                return {};
            }
        }
        return EmitError("a G_VTX at " + Hex(ref.cmdOffset) + " is in no vertex range");
    };
    spec.textures = &textures;
    return ReemitLists(spec, out);
}

std::vector<uint8_t> EncodeTexturePayload(uint32_t textureType, uint32_t width, uint32_t height,
                                          std::span<const uint8_t> pixels) {
    std::vector<uint8_t> out;
    out.reserve(28 + pixels.size());
    AppendWord(out, textureType);
    AppendWord(out, width);
    AppendWord(out, height);
    AppendWord(out, 0);          // flags
    AppendWord(out, 0x3F800000); // horizontal byte scale 1.0f
    AppendWord(out, 0x3F800000); // vertical pixel scale 1.0f
    AppendWord(out, uint32_t(pixels.size()));
    out.insert(out.end(), pixels.begin(), pixels.end());
    return out;
}

Error EncodeEmbeddedImagePayload(const EmbeddedImage& image, std::vector<uint8_t>& payload) {
    // Fast::TextureType by (format, size) for image data taken from a segment or model file.
    static const std::map<std::pair<uint32_t, uint32_t>, uint32_t> kTypes = {
        { { 0, 3 }, 1 }, { { 0, 2 }, 2 }, { { 2, 0 }, 3 }, { { 2, 1 }, 4 }, { { 4, 0 }, 5 },
        { { 4, 1 }, 6 }, { { 3, 0 }, 7 }, { { 3, 1 }, 8 }, { { 3, 2 }, 9 },
    };
    auto it = kTypes.find({ image.fmt, image.size });
    if (it == kTypes.end()) {
        // No libultraship type names this pair (an I-format 16-bit image in the legal-page prop); the resource's
        // fmt and size fields, not its type, drive decoding of unflagged textures.
        it = kTypes.find({ 0, image.size });
        if (it == kTypes.end() || image.width == 0) {
            return EmitError("an image has a format no texture type names");
        }
    }
    if (image.width == 0) {
        return EmitError("an image has no width");
    }
    uint32_t height = (image.texels + image.width - 1) / image.width;
    payload = EncodeTexturePayload(it->second, image.width, height, image.data);
    return {};
}

Error EncodeVertexPayload(std::span<const uint8_t> vertices, std::vector<uint8_t>& payload) {
    if (vertices.size() % kVertexSize != 0) {
        return EmitError("vertex data is not a whole number of 16-byte vertices");
    }
    payload.clear();
    payload.reserve(4 + vertices.size());
    AppendWord(payload, uint32_t(vertices.size() / kVertexSize));
    payload.insert(payload.end(), vertices.begin(), vertices.end());
    return {};
}

} // namespace Frigate::Extract
