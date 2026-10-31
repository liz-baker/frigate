// The Wii U's replacement for the global operator new and delete (WiiUAllocator.cpp).
#pragma once

#include <cstddef>

namespace Frigate::Platform {

// Bytes of freed medium blocks the allocator is holding on to for reuse. For the tests.
size_t MediumBytesRetained();

// The most MediumBytesRetained() will hold; a block freed beyond it goes back to the default heap.
size_t MediumRetainLimit();

} // namespace Frigate::Platform
