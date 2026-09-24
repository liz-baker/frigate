#pragma once

#include <functional>

// The interpreter caches a texture by the address it is loaded from, so a
// buffer whose contents change has to be invalidated for the change to show.
namespace Frigate::TextureCache {

using TextureInvalidator = std::function<void(const void*)>;

inline TextureInvalidator& Invalidator() {
    static TextureInvalidator invalidator;
    return invalidator;
}

// Forgets the cached texture loaded from `address`, if any.
inline void Invalidate(const void* address) {
    if (Invalidator()) {
        Invalidator()(address);
    }
}

} // namespace Frigate::TextureCache
