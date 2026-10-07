#include "level/AutoAim.h"

#include <cmath>

#include "level/LevelModels.h"
#include "level/ModelHit.h"

namespace Frigate::Level {

namespace {

// BONDVIEW_AUTOAIM_TIME (NTSC).
constexpr int kAutoAimTime = 30;

Model::ModelInstance& Instance(ModelHandle& handle) {
    return *static_cast<LevelModel&>(handle).Instance();
}

// transform3Dto2DCoords.
struct Projector {
    const PlayerCamera& camera;

    float X(float x, float z) const {
        return (camera.ScreenLeft() + (camera.ScreenWidth() * 0.5f)) - (x * (1.0f / z) * camera.RecipScaleX());
    }
    float Y(float y, float z) const {
        return (y * (1.0f / z) * camera.RecipScaleY()) + (camera.ScreenTop() + (camera.ScreenHeight() * 0.5f));
    }
};

} // namespace

void AutoAim::Reset() {
    *this = AutoAim{};
}

bool AutoAim::OnscreenRenderBounds(const AutoAimView& view, int propId, Bounds* bounds) const {
    const Prop& prop = (*view.props)[propId];
    const Chr& chr = (*view.chrs)[prop.object];
    if ((prop.flags & PropFlag::OnScreen) == 0) {
        return false;
    }
    if (chr.actionType == ChrAction::Die || chr.actionType == ChrAction::Dead || (chr.chrflags & ChrFlag::NoAutoAim) ||
        !chr.model) {
        return false;
    }

    const Model::ModelInstance& model = Instance(*chr.model);
    const auto& matrices = model.Walk().matrices;
    if (matrices.size() < 2) {
        return false;
    }
    const auto& first = matrices[0];
    const auto& second = matrices[1];

    bounds->aim[2] = second.m[3][2] + ((first.m[3][2] - second.m[3][2]) * 0.25f);
    if (!(bounds->aim[2] < 0.0f)) {
        return false;
    }
    bounds->aim[0] = second.m[3][0] + ((first.m[3][0] - second.m[3][0]) * 0.25f);
    bounds->aim[1] = second.m[3][1] + ((first.m[3][1] - second.m[3][1]) * 0.25f);

    bounds->yBounds[1] = 0.0f;
    bounds->yBounds[0] = 0.0f;
    bounds->xBounds[0] = 0.0f;
    bounds->xBounds[1] = 0.0f;
    AxisExtents(model, 0, &bounds->xBounds[1], &bounds->xBounds[0]);
    AxisExtents(model, 1, &bounds->yBounds[1], &bounds->yBounds[0]);
    return true;
}

float AutoAim::Score(const AutoAimView& view, int propId, const Bounds& bounds, float* outX, float* outY) const {
    const PlayerCamera& camera = *view.camera;
    const Projector project{ camera };
    const float left = camera.ScreenLeft();
    const float top = camera.ScreenTop();
    const float width = camera.ScreenWidth();
    const float height = camera.ScreenHeight();

    // The central region auto-aim accepts a target in.
    const float aimTop = top + (height * 0.175f);
    const float aimBottom = top + (height * 0.825f);
    const float aimLeft = left + (width * 0.25f);
    const float aimRight = left + (width * 0.75f);

    float score = -2.0f;

    const float aimScreenX = project.X(bounds.aim[0], bounds.aim[2]);
    const float aimScreenY = project.Y(bounds.aim[1], bounds.aim[2]);
    float leftEdgeX = project.X(bounds.xBounds[0], bounds.aim[2]);
    float rightEdgeX = project.X(bounds.xBounds[1], bounds.aim[2]);
    const float topEdgeY = project.Y(bounds.yBounds[1], bounds.aim[2]);
    const float bottomEdgeY = project.Y(bounds.yBounds[0], bounds.aim[2]);

    if (bottomEdgeY >= aimTop && aimBottom >= topEdgeY) {
        bool passesHorizontal = false;
        leftEdgeX = std::floor(leftEdgeX);
        rightEdgeX = std::ceil(rightEdgeX);
        const float centreX = left + (0.5f * width);
        float tolerance = 0.0f;

        if (view.xEnabled) {
            if (leftEdgeX <= aimRight && aimLeft <= rightEdgeX) {
                tolerance = (rightEdgeX - leftEdgeX) * 1.5f;
                tolerance = tolerance * view.difficulty;
                passesHorizontal = centreX >= ((leftEdgeX + rightEdgeX) * 0.5f) - tolerance &&
                                   centreX <= ((leftEdgeX + rightEdgeX) * 0.5f) + tolerance && aimLeft <= aimScreenX &&
                                   aimRight >= aimScreenX;
            }
        } else {
            passesHorizontal = leftEdgeX <= view.crosshairX && view.crosshairX <= rightEdgeX;
        }

        if (passesHorizontal) {
            const Prop& player = (*view.props)[view.playerProp];
            const Prop& target = (*view.props)[propId];
            int tile = player.stan;
            // The player, and the tank the player drives, are out of the line test.
            if (view.setPlayerCollides) {
                view.setPlayerCollides(false);
            }
            if (view.stan->TestLineUnobstructed(&tile, player.position[0], player.position[2], target.position[0],
                                                target.position[2], CdType::Objs | CdType::Doors | CdType::PathBlocker,
                                                view.duckingHeight, view.duckingHeight, 0.0f, 1.0f) != 0 &&
                tile == target.stan) {
                float clampedY = aimScreenY;
                if (clampedY < aimTop) {
                    clampedY = aimTop;
                } else if (clampedY > aimBottom) {
                    clampedY = aimBottom;
                }
                *outY = clampedY;

                if (view.xEnabled) {
                    float clampedX = aimScreenX;
                    if (clampedX < aimLeft) {
                        clampedX = aimLeft;
                    } else if (clampedX > aimRight) {
                        clampedX = aimRight;
                    }
                    *outX = clampedX;
                }

                // A target the screen's centre overlaps scores the best possible 1.0.
                if (centreX >= leftEdgeX && centreX <= rightEdgeX) {
                    score = 1.0f;
                } else if (centreX >= leftEdgeX) {
                    score = 1.0f - ((centreX - rightEdgeX) / tolerance);
                } else {
                    score = 1.0f - ((leftEdgeX - centreX) / tolerance);
                }
            }
            if (view.setPlayerCollides) {
                view.setPlayerCollides(true);
            }
        }
    }
    return score;
}

void AutoAim::UpdateY(int target, float aim, int clockTimer) {
    if (timeY_ >= 0) {
        timeY_ -= clockTimer;
    }
    if (target != targetY_) {
        if (timeY_ < 0) {
            timeY_ = kAutoAimTime;
            targetY_ = target;
        } else {
            return;
        }
    }
    aimY_ = aim;
}

void AutoAim::UpdateX(int target, float aim, int clockTimer) {
    if (timeX_ >= 0) {
        timeX_ -= clockTimer;
    }
    if (target != targetX_) {
        if (timeX_ < 0) {
            timeX_ = kAutoAimTime;
            targetX_ = target;
        } else {
            return;
        }
    }
    aimX_ = aim;
}

void AutoAim::Update(const AutoAimView& view) {
    int bestProp = -1;
    float bestX = 0.0f;
    float bestY = 0.0f;

    if (view.yEnabled && view.playerProp >= 0) {
        float bestScore = -1.0f;
        for (auto it = view.onScreen->rbegin(); it != view.onScreen->rend(); ++it) {
            const Prop& candidate = (*view.props)[*it];
            // Only characters are targets (another player is not part of a
            // single-player stage).
            if (candidate.type != PropType::Chr || candidate.object < 0) {
                continue;
            }
            const Chr& chr = (*view.chrs)[candidate.object];
            // A character holding no weapon is exempt.
            if (chr.weaponsHeld[HandRight] < 0 && chr.weaponsHeld[HandLeft] < 0) {
                continue;
            }
            Bounds bounds;
            if (!OnscreenRenderBounds(view, *it, &bounds)) {
                continue;
            }

            float candidateX = 0.0f;
            float candidateY = 0.0f;
            const float score = Score(view, *it, bounds, &candidateX, &candidateY);
            if (bestScore < score) {
                bestScore = score;
                bestProp = *it;
                bestX = candidateX;
                bestY = candidateY;
                if (1.0f <= score) {
                    break;
                }
            }
        }
    }

    const PlayerCamera& camera = *view.camera;
    if (bestProp >= 0) {
        UpdateY(bestProp, ((bestY - camera.ScreenTop()) / (camera.ScreenHeight() * 0.5f)) - 1.0f, view.clockTimer);
        if (view.xEnabled) {
            UpdateX(bestProp, ((bestX - camera.ScreenLeft()) / (camera.ScreenWidth() * 0.5f)) - 1.0f, view.clockTimer);
        }
    } else {
        UpdateY(-1, 0.0f, view.clockTimer);
        UpdateX(-1, 0.0f, view.clockTimer);
    }
}

} // namespace Frigate::Level
