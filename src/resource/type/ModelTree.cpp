#include "resource/type/ModelTree.h"

namespace Frigate {

ModelTree::ModelTree() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
}

ModelNode* ModelTree::GetPointer() {
    return Nodes.data();
}

size_t ModelTree::GetPointerSize() {
    return Nodes.size() * sizeof(ModelNode);
}

} // namespace Frigate
