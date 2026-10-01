#include "level/ClippedDoor.h"

namespace Frigate::Level {

int16_t DoorClipCutoff(const ModelBounds& bbox, bool vertical) {
    return static_cast<int16_t>(vertical ? bbox.ymax + 0.5f : bbox.xmin + 0.5f);
}

std::vector<ModelVertex> ClipDoorVertices(const ModelVertex* source, size_t count, bool vertical, int16_t cutoff) {
    std::vector<ModelVertex> out(count);
    for (size_t quad = 0; quad < count / 4; quad++) {
        const ModelVertex* src = source + (quad * 4);
        ModelVertex* dst = out.data() + (quad * 4);
        for (size_t j = 0; j < 4; j++) {
            if (j == 0) {
                for (size_t k = 0; k < 4; k++) {
                    dst[k] = src[k];
                }
            }
            const ModelVertex& cur = src[j];
            ModelVertex& dcur = dst[j];
            const ModelVertex* neighbours[3] = { &src[(j + 1) % 4], &src[(j + 2) % 4], &src[(j + 3) % 4] };

            if (vertical) {
                // Texture coordinates follow the cut along the edge that runs
                // straight up or down from the vertex.
                if (cur.position[1] >= cutoff) {
                    for (const ModelVertex* n : neighbours) {
                        if (cur.position[0] == n->position[0] && cur.position[2] == n->position[2] &&
                            cur.position[1] != n->position[1]) {
                            dcur.s = static_cast<int16_t>(
                                ((cur.position[1] - cutoff) * (n->s - cur.s) / (cur.position[1] - n->position[1])) +
                                cur.s);
                            dcur.t = static_cast<int16_t>(
                                ((cur.position[1] - cutoff) * (n->t - cur.t) / (cur.position[1] - n->position[1])) +
                                cur.t);
                            break;
                        }
                    }
                    dcur.position[1] = cutoff;
                }
            } else if (cur.position[0] <= cutoff) {
                for (const ModelVertex* n : neighbours) {
                    if (cur.position[1] == n->position[1] && cur.position[2] == n->position[2] &&
                        cur.position[0] != n->position[0]) {
                        dcur.s = static_cast<int16_t>(
                            ((cutoff - cur.position[0]) * (n->s - cur.s) / (n->position[0] - cur.position[0])) + cur.s);
                        dcur.t = static_cast<int16_t>(
                            ((cutoff - cur.position[0]) * (n->t - cur.t) / (n->position[0] - cur.position[0])) + cur.t);
                        break;
                    }
                }
                dcur.position[0] = cutoff;
            }
        }
    }
    return out;
}

} // namespace Frigate::Level
