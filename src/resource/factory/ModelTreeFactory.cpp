#include "resource/factory/ModelTreeFactory.h"
#include "resource/type/ModelTree.h"

namespace Frigate {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryModelTreeV4::ReadResource(std::shared_ptr<Ship::File> file,
                                               std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);
    auto tree = std::make_shared<ModelTree>(initData);

    const uint32_t count = reader->ReadUInt32();
    tree->MatrixCount = reader->ReadUInt32();

    // Each record is 44 bytes; reject a count the file cannot hold before reserving.
    constexpr size_t kRecordSize = 44;
    if (count > file->Buffer->size() / kRecordSize) {
        return nullptr;
    }

    tree->Nodes.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        ModelNode node;
        node.offset = reader->ReadUInt32();
        node.type = reader->ReadUByte();
        reader->ReadUByte();
        node.matrix = reader->ReadInt16();
        node.bendMatrix = reader->ReadInt16();
        reader->ReadInt16();
        node.child = reader->ReadInt32();
        node.next = reader->ReadInt32();
        node.parent = reader->ReadInt32();
        for (float& v : node.origin) {
            v = reader->ReadFloat();
        }
        node.primaryList = reader->ReadUInt32();
        node.secondaryList = reader->ReadUInt32();
        for (int32_t link : { node.child, node.next, node.parent }) {
            if (link < kNoNode || link >= static_cast<int32_t>(count)) {
                return nullptr;
            }
        }
        tree->Nodes.push_back(node);
    }

    const uint32_t lodCount = reader->ReadUInt32();
    // Each LOD entry is 12 bytes; reject a count the file cannot hold.
    constexpr size_t kLodEntrySize = 12;
    if (lodCount > (file->Buffer->size() - reader->GetBaseAddress()) / kLodEntrySize) {
        return nullptr;
    }
    for (uint32_t i = 0; i < lodCount; i++) {
        const uint32_t nodeIndex = reader->ReadUInt32();
        const float minDistance = reader->ReadFloat();
        const float maxDistance = reader->ReadFloat();
        if (nodeIndex >= count) {
            return nullptr;
        }
        tree->Nodes[nodeIndex].lodMinDistance = minDistance;
        tree->Nodes[nodeIndex].lodMaxDistance = maxDistance;
    }

    const uint32_t bspCount = reader->ReadUInt32();
    // Each BSP entry is 36 bytes; reject a count the file cannot hold.
    constexpr size_t kBspEntrySize = 36;
    if (bspCount > (file->Buffer->size() - reader->GetBaseAddress()) / kBspEntrySize) {
        return nullptr;
    }
    for (uint32_t i = 0; i < bspCount; i++) {
        const uint32_t nodeIndex = reader->ReadUInt32();
        float point[3];
        for (float& v : point) {
            v = reader->ReadFloat();
        }
        float vector[3];
        for (float& v : vector) {
            v = reader->ReadFloat();
        }
        const int32_t transformMode = reader->ReadInt32();
        const int32_t secondChild = reader->ReadInt32();
        if (nodeIndex >= count || secondChild < kNoNode || secondChild >= static_cast<int32_t>(count)) {
            return nullptr;
        }
        ModelNode& node = tree->Nodes[nodeIndex];
        node.bspPoint[0] = point[0];
        node.bspPoint[1] = point[1];
        node.bspPoint[2] = point[2];
        node.bspVector[0] = vector[0];
        node.bspVector[1] = vector[1];
        node.bspVector[2] = vector[2];
        node.bspTransformMode = transformMode;
        node.bspSecondChild = secondChild;
    }

    const uint32_t jointCount = reader->ReadUInt32();
    // Each joint entry is 6 bytes; reject a count the file cannot hold.
    constexpr size_t kJointEntrySize = 6;
    if (jointCount > (file->Buffer->size() - reader->GetBaseAddress()) / kJointEntrySize) {
        return nullptr;
    }
    for (uint32_t i = 0; i < jointCount; i++) {
        const uint32_t nodeIndex = reader->ReadUInt32();
        const uint16_t jointId = reader->ReadUInt16();
        if (nodeIndex >= count) {
            return nullptr;
        }
        tree->Nodes[nodeIndex].jointId = jointId;
    }

    const uint32_t switchCount = reader->ReadUInt32();
    // Each switch entry is 4 bytes; reject a count the file cannot hold.
    constexpr size_t kSwitchEntrySize = 4;
    if (switchCount > (file->Buffer->size() - reader->GetBaseAddress()) / kSwitchEntrySize) {
        return nullptr;
    }
    tree->Switches.reserve(switchCount);
    for (uint32_t i = 0; i < switchCount; i++) {
        const int32_t nodeIndex = reader->ReadInt32();
        if (nodeIndex < kNoNode || nodeIndex >= static_cast<int32_t>(count)) {
            return nullptr;
        }
        tree->Switches.push_back(nodeIndex);
    }
    return tree;
}

} // namespace Frigate
