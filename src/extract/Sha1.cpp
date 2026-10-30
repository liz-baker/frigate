#include "extract/Sha1.h"

#include <cstring>

namespace Frigate::Extract {

namespace {

constexpr uint32_t Rotl(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

} // namespace

Sha1::Sha1() : mState{ 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 } {
}

void Sha1::Block(const uint8_t* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) | (uint32_t(block[i * 4 + 2]) << 8) |
               uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = mState[0], b = mState[1], c = mState[2], d = mState[3], e = mState[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = Rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rotl(b, 30);
        b = a;
        a = temp;
    }
    mState[0] += a;
    mState[1] += b;
    mState[2] += c;
    mState[3] += d;
    mState[4] += e;
}

void Sha1::Update(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    mBytes += size;
    if (mBuffered != 0) {
        size_t take = 64 - mBuffered;
        if (take > size) {
            take = size;
        }
        std::memcpy(mBuffer + mBuffered, bytes, take);
        mBuffered += take;
        bytes += take;
        size -= take;
        if (mBuffered < 64) {
            return;
        }
        Block(mBuffer);
        mBuffered = 0;
    }
    while (size >= 64) {
        Block(bytes);
        bytes += 64;
        size -= 64;
    }
    if (size != 0) {
        std::memcpy(mBuffer, bytes, size);
        mBuffered = size;
    }
}

Sha1::Digest Sha1::Finish() {
    uint64_t bits = mBytes * 8;
    uint8_t pad[72] = { 0x80 };
    size_t padLength = (mBuffered < 56) ? (56 - mBuffered) : (120 - mBuffered);
    for (int i = 0; i < 8; i++) {
        pad[padLength + i] = uint8_t(bits >> (56 - 8 * i));
    }
    Update(pad, padLength + 8);

    Digest digest;
    for (int i = 0; i < 5; i++) {
        digest[i * 4] = uint8_t(mState[i] >> 24);
        digest[i * 4 + 1] = uint8_t(mState[i] >> 16);
        digest[i * 4 + 2] = uint8_t(mState[i] >> 8);
        digest[i * 4 + 3] = uint8_t(mState[i]);
    }
    return digest;
}

Sha1::Digest Sha1::Of(const void* data, size_t size) {
    Sha1 hash;
    hash.Update(data, size);
    return hash.Finish();
}

std::string Sha1::ToHex(const Digest& digest) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (uint8_t byte : digest) {
        out += kHex[byte >> 4];
        out += kHex[byte & 15];
    }
    return out;
}

} // namespace Frigate::Extract
