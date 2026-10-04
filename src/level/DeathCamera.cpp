#include "level/DeathCamera.h"

#include <cmath>

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kStep = 0.39269909f;
constexpr float kRandomUnit = 2.3283064e-10f;
// The farthest the camera is tried at, past the clearance.
constexpr float kSearchDistance = 1500.0f;
constexpr int kOuterTries = 0x80;
constexpr int kAngleTries = 0x10;
// The cdtypes the camera's line is tested against, and the volume it fits in.
constexpr int kLineTypes = CdType::Objs | CdType::Doors | CdType::PathBlocker;
constexpr int kVolumeTypes = 0x1f;
constexpr float kCameraHeightRange = 185.0f;
constexpr float kVerticalLimit = 1000.0f;

float Unit(const std::function<uint32_t()>& random) {
    return static_cast<float>(random()) * kRandomUnit;
}

} // namespace

bool PickDeathCameraAngles(Stan& stan, const DeathCameraRequest& request, const std::function<uint32_t()>& random,
                           Vec3* camera) {
    const Vec3& pos = request.lookAt;
    const Vec3& collision = request.collisionPosition;
    const float clearance = request.clearance;

    bool found = false;
    int outerTries = 0;
    float angleRange = kTau;

    while (outerTries <= kOuterTries && !found) {
        float reach = kSearchDistance + clearance;

        float angle = Unit(random);
        angle = angle * angleRange;

        int angleTries = 0;
        while (angleTries < kAngleTries && !found) {
            angleRange = kTau;

            angle += kStep;
            if (angleRange <= angle) {
                angle -= angleRange;
            }

            const Vec3 direction = { std::sin(angle), 0.0f, std::cos(angle) };
            Vec3 farPoint = { (direction[0] * reach) + pos[0], pos[1], (direction[2] * reach) + pos[2] };

            stan.ResetHits();
            int tile = request.tile;
            request.setSubjectCollides(false);
            bool lineOk = stan.TestLineUnobstructed(&tile, collision[0], collision[2], farPoint[0], farPoint[2], kLineTypes, 0.0f,
                                                    1.0f, 0.0f, 1.0f) != 0;
            request.setSubjectCollides(true);

            if (!lineOk) {
                farPoint = stan.CollisionEdgeRayPoint(collision, direction);
                const float dx = farPoint[0] - collision[0];
                const float dz = farPoint[2] - collision[2];
                reach = std::sqrt((dx * dx) + (dz * dz));
            }

            reach -= clearance;

            if (request.distance <= reach) {
                float frac = 1.0f;
                while (0.0f < frac && !found) {
                    const float span = reach - request.distance;

                    float candidate = Unit(random);
                    candidate *= span;
                    candidate *= frac;
                    candidate += request.distance;

                    Vec3 spot = { (direction[0] * candidate) + pos[0], pos[1], (direction[2] * candidate) + pos[2] };

                    tile = request.tile;
                    request.setSubjectCollides(false);
                    lineOk = stan.TestLineUnobstructed(&tile, collision[0], collision[2], spot[0], spot[2], kLineTypes,
                                                       0.0f, 1.0f, 0.0f, 1.0f) != 0;
                    request.setSubjectCollides(true);

                    if (lineOk) {
                        if (stan.TestVolume(&tile, spot[0], spot[2], clearance, kVolumeTypes, 0.0f, 1.0f) < 0) {
                            const float floorY = stan.PositionY(tile, spot[0], spot[2]);
                            spot[1] = (floorY + clearance) + (Unit(random) * (kCameraHeightRange - clearance));

                            const float rise = spot[1] - pos[1];
                            if (-kVerticalLimit < rise && rise < kVerticalLimit) {
                                found = true;
                            }
                        }
                    }
                    *camera = spot;

                    frac -= 0.25f;
                }
            }

            angleTries++;
        }

        angleRange = kTau;
        outerTries++;
    }

    return found;
}

} // namespace Frigate::Level
