#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Frigate::Extract {

// Incremental SHA-1 (FIPS 180-4). Only used to recognise ROM dumps, never for security.
class Sha1 {
  public:
    using Digest = std::array<uint8_t, 20>;

    Sha1();
    void Update(const void* data, size_t size);
    Digest Finish();

    static Digest Of(const void* data, size_t size);
    static std::string ToHex(const Digest& digest);

  private:
    void Block(const uint8_t* block);

    uint32_t mState[5];
    uint64_t mBytes = 0;
    uint8_t mBuffer[64];
    size_t mBuffered = 0;
};

} // namespace Frigate::Extract
