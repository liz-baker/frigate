// Writes the fixed set of resources writer_equivalence.py also writes with o2r_writer.py, so the two archives'
// members can be compared byte for byte.

#include "extract/O2rWriter.h"

#include <cstdio>
#include <vector>

using namespace Frigate::Extract;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output.o2r>\n", argv[0]);
        return 2;
    }
    std::vector<uint8_t> ramp(5000);
    for (size_t i = 0; i < ramp.size(); i++) {
        ramp[i] = uint8_t(i / 3);
    }
    std::vector<uint8_t> noise(300);
    uint32_t state = 12345;
    for (uint8_t& byte : noise) {
        state = state * 1664525u + 1013904223u;
        byte = uint8_t(state >> 24);
    }

    O2rWriter writer;
    bool ok = writer.Open(argv[1]).ok();
    ok = ok && writer.AddResource("blob/ramp", "OBLB", 0, 0x1122334455667788ull, ramp).ok();
    ok = ok && writer.AddResource("blob/noise", "OBLB", 2, 5, noise).ok();
    ok = ok && writer.AddResource("tex/noise", "OTEX", 1, 99, noise).ok();
    ok = ok && writer.AddResource("blob/little", "OBLB", 3, 77, ramp, Endianness::Little, true).ok();
    ok = ok && writer.AddResource("blob/empty", "OBLB", 0, 0, {}).ok();
    ok = ok && writer.AddRawFile("raw/file", noise).ok();
    ok = ok && writer.Close().ok();
    return ok ? 0 : 1;
}
