#include "extract/ModelPackers.h"
#include "extract/ModelTree.h"
#include "extract/PackCommon.h"
#include "extract/TexturePackers.h"

#include <string>

namespace Frigate::Extract {

namespace {

constexpr std::string_view kModelCategories[] = { "chr", "gun", "prop" };

Error ReemitNode(const ModelImage& image, const std::string& base, const ModelNode& node,
                 const std::vector<ModelVertexBlock>& blocks, const TextureLookup& textures, ReemittedLists& result) {
    const DisplayListRecord& record = *node.displayList;
    ReemitSpec spec;
    if (record.primary) {
        spec.roots.push_back(*record.primary);
    }
    if (record.secondary) {
        spec.roots.push_back(*record.secondary);
    }
    if (spec.roots.empty()) {
        return {};
    }

    const ModelVertexBlock* block = nullptr;
    for (const ModelVertexBlock& candidate : blocks) {
        if (candidate.node == node.offset) {
            block = &candidate;
        }
    }
    std::string vertexPath = base + "/vtx/" + Hex6(node.offset);

    spec.what = base + " node " + Hex6(node.offset);
    spec.data = image.data;
    spec.resolve = image.DisplayListResolver(record.verticesAddress);
    spec.listPath = [&](uint32_t offset) { return base + "/dl/" + Hex6(node.offset) + "/" + Hex6(offset); };
    spec.imagePath = [&](uint32_t offset) { return base + "/img/" + Hex6(offset); };
    spec.vertexTarget = [&](const VertexRef& ref, std::string& path, uint32_t& byteOffset) -> Error {
        if (block == nullptr) {
            return Error::Make(ErrorCode::Corrupt, "A model node loads vertices but has no vertex array.");
        }
        int64_t relative = int64_t(ref.offset) - int64_t(*record.vertices);
        if (relative < 0 || relative + int64_t(ref.count) * int64_t(kVertexSize) > int64_t(block->length)) {
            return Error::Make(ErrorCode::Corrupt, "A model's vertex load leaves its node's vertex array.");
        }
        path = vertexPath;
        byteOffset = uint32_t(relative);
        return {};
    };
    spec.textures = &textures;
    return ReemitLists(spec, result);
}

Error PackModel(const std::string& base, const ModelHeader& header, const ModelImage& image,
                const TextureLookup& textures, O2rWriter& writer) {
    ModelTree tree;
    if (Error err = ParseModelTree(image, header, tree); !err.ok()) {
        return err;
    }
    std::vector<ModelVertexBlock> blocks;
    if (Error err = FindModelVertexBlocks(image, tree, blocks); !err.ok()) {
        return err;
    }

    std::vector<uint8_t> scratch;
    for (const ModelVertexBlock& block : blocks) {
        if (Error err =
                WriteVertexBlock(writer, base + "/vtx/" + Hex6(block.node),
                                 std::span<const uint8_t>(image.data).subspan(block.offset, block.length), scratch);
            !err.ok()) {
            return err;
        }
    }

    if (Error err = EncodeModelTreePayload(image, tree, textures, static_cast<float>(header.radius), scratch); !err.ok()) {
        return err;
    }
    if (Error err = writer.AddResource(base + "/tree", kModelTreeResourceType, kModelTreeResourceVersion, 0, scratch);
        !err.ok()) {
        return err;
    }

    ReemittedLists result;
    for (const auto& [offset, node] : tree.nodes) {
        if (!node.displayList) {
            continue;
        }
        if (Error err = ReemitNode(image, base, node, blocks, textures, result); !err.ok()) {
            return err;
        }
        if (Error err = WriteLists(writer, result); !err.ok()) {
            return err;
        }
    }
    if (Error err = WriteImages(writer, result); !err.ok()) {
        return err;
    }

    for (const auto& [offset, node] : tree.nodes) {
        if (node.type != kNodeGunfire && node.type != kNodeShadow) {
            continue;
        }
        std::optional<EffectRecord> effect;
        if (Error err = ReadEffectRecord(image, tree, node, effect); !err.ok()) {
            return err;
        }
        if (!effect || !effect->texture) {
            continue;
        }
        TextureBinding binding;
        if (Error err = textures(effect->texture->textureId, binding); !err.ok()) {
            return err;
        }
        std::vector<uint8_t> list =
            EncodeTextureRequestList(EffectImageRequest(*effect->texture), effect->texture->textureId, binding);
        if (Error err = writer.AddResource(base + "/fx/" + Hex6(offset), kDisplayListResourceType,
                                           kDisplayListResourceVersion, 0, list);
            !err.ok()) {
            return err;
        }
    }

    if (header.skeleton != nullptr) {
        bool found = false;
        std::span<const SkeletonJoint> joints = FindSkeleton(header.skeleton, &found);
        if (!found) {
            return Error::Make(ErrorCode::Corrupt, std::string("A model names the skeleton ") + header.skeleton +
                                                       ", which the extractor does not know.");
        }
        std::vector<uint8_t> payload = EncodeSkeletonPayload(joints);
        if (Error err =
                writer.AddResource(base + "/skeleton", kSkeletonResourceType, kSkeletonResourceVersion, 0, payload);
            !err.ok()) {
            return err;
        }
    }
    return {};
}

} // namespace

Error PackModels(StageContext& context, O2rWriter& writer) {
    if (Error err = RequireKnownRegion(context, "models"); !err.ok()) {
        return err;
    }

    std::vector<const FileEntry*> models;
    for (const FileEntry& entry : context.layout().files) {
        if (!entry.extract) {
            continue;
        }
        for (std::string_view category : kModelCategories) {
            if (StartsWith(entry.name, std::string("assets/obseg/") + std::string(category) + "/")) {
                models.push_back(&entry);
                break;
            }
        }
    }

    TextureBindings bindings(context);
    TextureLookup textures = bindings.AsLookup();
    ModelImage image;
    for (size_t i = 0; i < models.size(); i++) {
        if (context.Cancelled()) {
            return Error::Make(ErrorCode::Cancelled, "Cancelled.");
        }
        const FileEntry& entry = *models[i];
        std::string_view file = BaseName(entry.name);
        std::string_view stem = WithoutExtension(file);
        std::string_view directory = std::string_view(entry.name).substr(0, std::string_view(entry.name).rfind('/'));
        std::string_view category = BaseName(directory);
        std::string base = "models/" + std::string(category) + "/" + std::string(stem);
        context.Report(i, models.size(), base);

        // The header name is the file's stem without its directory letter and its `Z`.
        if (stem.size() < 2) {
            return Error::Make(ErrorCode::Corrupt, "A model file has a name too short to be a model.");
        }
        const ModelHeader* header = FindModelHeader(stem.substr(1, stem.size() - 2));
        if (header == nullptr) {
            return Error::Make(ErrorCode::Corrupt,
                               "The extractor has no header for the model " + std::string(stem) + ".");
        }
        if (Error err = context.ReadFile(entry, image.data); !err.ok()) {
            return err;
        }
        if (Error err = PackModel(base, *header, image, textures, writer); !err.ok()) {
            return err;
        }
    }
    context.Report(models.size(), models.size());
    return {};
}

} // namespace Frigate::Extract
