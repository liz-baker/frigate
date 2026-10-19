// Packs libultraship's Fast3D shader sources into f3d.o2r. The rendering backends load their shaders through the
// ResourceManager (`shaders/<api>/default.shader.<ext>`) and abort on the first draw that needs one if no mounted
// archive has them. Needs no ROM.

#include "PackShaders.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <libultraship src/fast/shaders> <f3d.o2r>\n", argv[0]);
        return 2;
    }
    size_t count = 0;
    Frigate::Extract::Error error = Frigate::Extract::PackShaders(argv[1], argv[2], &count);
    if (!error.ok()) {
        std::fprintf(stderr, "frigate-pack-shaders: %s\n", error.message.c_str());
        return 1;
    }
    std::printf("wrote %zu shaders to %s\n", count, argv[2]);
    return 0;
}
