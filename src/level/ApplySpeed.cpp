#include "level/ApplySpeed.h"

namespace Frigate::Level {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTau = 6.2831855f;

} // namespace

void ChrObjApplySpeed(float* position, float target, float* speedPtr, float accel, float decel, float maxSpeed,
                      int32_t ticks) {
    float speed = *speedPtr;
    for (int32_t i = 0; i < ticks; i++) {
        const float limit = speed * speed * 0.5f / decel;
        const float remaining = target - *position;
        if (remaining > 0.0f) {
            if (speed > 0.0f && remaining <= limit) {
                speed -= decel;
                if (speed < decel) {
                    speed = decel;
                }
            } else if (speed < maxSpeed) {
                speed += (speed < 0.0f) ? decel : accel;
                if (speed > maxSpeed) {
                    speed = maxSpeed;
                }
            }
            if (speed >= remaining) {
                *position = target;
                break;
            }
            *position += speed;
        } else {
            if (speed < 0.0f && -remaining <= limit) {
                speed += decel;
                if (speed > -decel) {
                    speed = -decel;
                }
            } else if (speed > -maxSpeed) {
                speed -= (speed > 0.0f) ? decel : accel;
                if (speed < -maxSpeed) {
                    speed = -maxSpeed;
                }
            }
            if (speed <= remaining) {
                *position = target;
                break;
            }
            *position += speed;
        }
    }
    *speedPtr = speed;
}

void ChrObjCallsApplySpeed(float* position, float target, float* speed, float accel, float decel, float maxSpeed,
                           int32_t ticks) {
    if (target - *position < -kPi) {
        target += kTau;
    } else if (target - *position >= kPi) {
        target -= kTau;
    }

    ChrObjApplySpeed(position, target, speed, accel, decel, maxSpeed, ticks);

    if (*position < 0.0f) {
        *position += kTau;
    }
    if (*position >= kTau) {
        *position -= kTau;
    }
}

} // namespace Frigate::Level
