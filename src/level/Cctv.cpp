#include "level/Cctv.h"

#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kPi = 3.1415927f;
// How fast the swing speeds up and slows down, per tick.
constexpr float kAcceleration = 0.00065449846f;

// DegToRad, with the product taken in single precision as the game's macro does.
constexpr float DegToRad(int degrees) {
    return static_cast<float>(static_cast<double>(static_cast<float>(degrees) * kTau) / 360.0);
}

} // namespace

CctvState MakeCctv(const Mtxf& matrix, const Vec3& position, const std::array<float, 3>& pivot, const Vec3& lookPad,
                   float modelScale, const std::array<int32_t, 4>& fixed) {
    CctvState state;
    Vec3 world = RotateVector(matrix, { pivot[0], pivot[1], pivot[2] });
    world[0] += position[0];
    world[1] += position[1];
    world[2] += position[2];

    state.basis =
        BasisToward({ world[0] - lookPad[0], world[1] - lookPad[1], world[2] - lookPad[2] }, { 0.0f, 1.0f, 0.0f });
    ScaleRows(&state.basis, modelScale);

    state.swingA = (static_cast<float>(fixed[0]) * kTau) / 65536.0f;
    state.swingB = (static_cast<float>(fixed[1]) * kTau) / 65536.0f;
    state.maxSpeed = (static_cast<float>(fixed[2]) * kTau) / 65536.0f;
    state.range = static_cast<float>(fixed[3]);

    state.towardsA = false;
    state.speed = 0.0f;
    state.swing = state.swingA;
    state.lookHeading = GameAtan2f(world[0] - lookPad[0], world[2] - lookPad[2]);
    state.timer = 0;
    return state;
}

bool CctvInView(const CctvState& state, const Vec3& toPlayer, bool inMotion) {
    const float xdiff = toPlayer[0];
    const float ydiff = toPlayer[1];
    const float zdiff = toPlayer[2];
    bool sees = true;

    if (state.range > 0.0f && (state.range * state.range) < (((xdiff * xdiff) + (ydiff * ydiff)) + (zdiff * zdiff))) {
        sees = false;
    }
    if (inMotion) {
        sees = false;
    }
    if (sees) {
        const float heading = GameAtan2f(xdiff, zdiff);
        float angle = state.swing;
        if (angle < 0.0f) {
            angle += kTau;
        } else if (angle >= kTau) {
            angle -= kTau;
        }
        angle += state.lookHeading;
        if (angle >= kTau) {
            angle -= kTau;
        }

        float delta = heading - angle;
        if (heading < angle) {
            delta += kTau;
        }
        delta -= kPi;
        if (delta < 0.0f) {
            delta += kTau;
        }
        if (delta > kPi) {
            delta -= kTau;
        }
        if (delta > DegToRad(45) || delta < DegToRad(-45)) {
            sees = false;
        }
    }
    return sees;
}

void SwingCctv(CctvState* state, float timerDelta) {
    const float target = state->towardsA ? state->swingA : state->swingB;

    if (state->swing < target) {
        float stopping = ((state->speed * state->speed) * 0.5f) / kAcceleration;
        if ((target - stopping) <= state->swing) {
            state->speed = state->speed - (kAcceleration * timerDelta);
            if (state->speed < kAcceleration) {
                state->speed = kAcceleration;
            }
        } else if (state->speed < state->maxSpeed) {
            stopping = state->speed + (kAcceleration * timerDelta);
            if (state->maxSpeed < stopping) {
                stopping = state->maxSpeed;
            }
            if (state->swing < (target - (((stopping * stopping) * 0.5f) / kAcceleration))) {
                state->speed = stopping;
            }
        }

        state->swing += state->speed * timerDelta;
        if (target <= state->swing) {
            state->swing = target;
            state->speed = 0.0f;
            state->towardsA = false;
        }
    } else {
        float stopping = ((state->speed * state->speed) * 0.5f) / kAcceleration;
        if (state->swing <= (target + stopping)) {
            state->speed = state->speed - (kAcceleration * timerDelta);
            if (state->speed < kAcceleration) {
                state->speed = kAcceleration;
            }
        } else if (state->speed < state->maxSpeed) {
            stopping = state->speed + (kAcceleration * timerDelta);
            if (state->maxSpeed < stopping) {
                stopping = state->maxSpeed;
            }
            if (state->swing > (target + (((stopping * stopping) * 0.5f) / kAcceleration))) {
                state->speed = stopping;
            }
        }

        state->swing -= state->speed * timerDelta;
        if (state->swing <= target) {
            state->swing = target;
            state->speed = 0.0f;
            state->towardsA = true;
        }
    }
}

Mtxf CctvSwivel(const CctvState& state, const Mtxf& objectMatrix, const Vec3& relativePosition,
                const std::array<float, 3>& pivot) {
    float angle = state.swing;
    if (angle < 0.0f) {
        angle += kTau;
    } else if (angle >= kTau) {
        angle -= kTau;
    }

    Mtxf placed = objectMatrix;
    placed.m[3][0] = relativePosition[0];
    placed.m[3][1] = relativePosition[1];
    placed.m[3][2] = relativePosition[2];

    Mtxf swivel = Multiply(RotationY(angle), state.basis);
    const Vec3 world = TransformVector(placed, { pivot[0], pivot[1], pivot[2] });
    swivel.m[3][0] = world[0];
    swivel.m[3][1] = world[1];
    swivel.m[3][2] = world[2];
    return swivel;
}

} // namespace Frigate::Level
