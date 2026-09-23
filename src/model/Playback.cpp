#include "model/Playback.h"

#include <cmath>

#include "model/RootMotion.h"

namespace Frigate {

namespace {

constexpr float kTau = 6.2831855f;

bool gAnimMergingEnabled = true;

int32_t FloorToInt(float value) {
    return static_cast<int32_t>(std::floor(value));
}

int32_t CeilToInt(float value) {
    return static_cast<int32_t>(std::ceil(value));
}

void InterpolateVec3(Vec3& v, const Vec3& w, float frac) {
    v.x += (w.x - v.x) * frac;
    v.y += (w.y - v.y) * frac;
    v.z += (w.z - v.z) * frac;
}

} // namespace

AnimationPose MakePose(const ModelPlayback& playback) {
    AnimationPose pose;
    pose.clip = playback.anim;
    pose.joints = playback.joints;
    pose.mirror = playback.mirror;
    pose.frameA = static_cast<uint32_t>(playback.frameA);
    pose.frameB = static_cast<uint32_t>(playback.frameB);
    pose.frameFrac = playback.frameFrac;

    if (playback.mergeWeight != 0.0f) {
        pose.secondaryClip = playback.anim2;
        pose.secondaryMirror = playback.mirror2;
        pose.secondaryFrameA = static_cast<uint32_t>(playback.frame2A);
        pose.secondaryFrameB = static_cast<uint32_t>(playback.frame2B);
        pose.secondaryFrameFrac = playback.frame2Frac;
        pose.blend = playback.mergeWeight;
    }
    return pose;
}

RootPlacement MakePlacement(const ModelPlayback& playback) {
    RootPlacement placement;
    placement.position[0] = playback.root.pos.x;
    placement.position[1] = playback.root.pos.y;
    placement.position[2] = playback.root.pos.z;
    placement.heading = playback.root.heading;
    if (playback.root.headingOverrideBlend != 0.0f) {
        placement.heading =
            LerpAngle(placement.heading, playback.root.headingOverride, playback.root.headingOverrideBlend);
    }
    placement.scale = playback.scale;
    return placement;
}

void SetAnimMergingEnabled(bool enabled) {
    gAnimMergingEnabled = enabled;
}

bool AnimMergingEnabled() {
    return gAnimMergingEnabled;
}

int32_t ModelPlayback::ConstrainOrWrapFrame(int32_t frame, const AnimationClip* clip, float end) const {
    // A clip with no frames has nothing to wrap onto.
    const int32_t count = static_cast<int32_t>(clip->frameCount);
    if (frame < 0) {
        frame = (clip->loop && count > 0) ? count - ((-frame) % count) : 0;
    } else if (0.0f <= end && static_cast<int32_t>(end) < frame) {
        frame = CeilToInt(end);
    } else if (frame >= count) {
        frame = (clip->loop && count > 0) ? frame % count : count - 1;
    }
    return frame;
}

void ModelPlayback::CopyAnimForMerge(float mergeTime) {
    if (0.0f < mergeTime && anim != nullptr) {
        anim2 = anim;
        frame2 = frame;
        frame2Frac = frameFrac;
        mirror2 = mirror;
        frame2A = frameA;
        frame2B = frameB;
        speed2 = speed;
        newSpeed2 = newSpeed;
        oldSpeed2 = oldSpeed;
        timeSpeed2 = timeSpeed;
        elapseSpeed2 = elapseSpeed;
        endFrame2 = endFrame;

        if (hasRoot) {
            root.hasMergeSource = true;
            root.mergeStartPos = root.startPos;
            root.mergeTargetPos = root.targetPos;
        }
        return;
    }

    anim2 = nullptr;
}

void ModelPlayback::SetAnimation2(const AnimationClip* clip, bool flip, float startFrame, float newSpeed_,
                                  float merge) {
    const bool hadNoAnim = anim == nullptr;

    if (anim2 != nullptr) {
        mergeDuration = merge;
        mergeElapsed = 0.0f;
        mergeWeight = 1.0f;
    } else {
        mergeDuration = 0.0f;
        mergeWeight = 0.0f;
    }

    anim = clip;
    mirror = flip;
    endFrame = -1.0f;
    speed = newSpeed_;
    timeSpeed = 0.0f;

    SetAnimFrame(startFrame);

    animLooping = false;

    if (!hasRoot) {
        return;
    }

    const float rootScale = scale * animTranslationScale;

    RootMotionSample sample = SampleRootMotion(*anim, *joints, rootJoint, mirror, frameB);
    Vec3 pos{ sample.pos[0], sample.pos[1], sample.pos[2] };
    const float angleDelta = sample.angle;

    if (rootScale != 1.0f) {
        pos.x *= rootScale;
        pos.y *= rootScale;
        pos.z *= rootScale;
    }

    const float cosAngle = std::cos(root.heading);
    const float sinAngle = std::sin(root.heading);

    if (frameFrac == 0.0f) {
        root.startPos.x = root.pos.x;
        root.startPos.y = root.pos.y - root.ground;
        root.startPos.z = root.pos.z;

        root.startHeading = root.heading;

        root.targetPos.x = root.startPos.x + (pos.x * cosAngle) + (pos.z * sinAngle);
        root.targetPos.y = pos.y;
        root.targetPos.z = root.startPos.z - (pos.x * sinAngle) + (pos.z * cosAngle);

        if (root.headingOverrideBlend == 0.0f) {
            root.targetHeading = root.startHeading + angleDelta;

            if (root.targetHeading >= kTau) {
                root.targetHeading -= kTau;
            }
        }

        root.hasTarget = true;
    } else {
        const float x = pos.x * cosAngle + pos.z * sinAngle;
        const float y = pos.y;
        const float z = -pos.x * sinAngle + pos.z * cosAngle;

        root.targetPos.x = root.pos.x + x * (1.0f - frameFrac);
        root.targetPos.y = y;
        root.targetPos.z = root.pos.z + z * (1.0f - frameFrac);

        root.startPos.x = root.targetPos.x - x;
        root.startPos.y =
            (root.pos.y - root.ground) - (y - (root.pos.y - root.ground)) * frameFrac / (1.0f - frameFrac);
        root.startPos.z = root.targetPos.z - z;

        float previousHeading = root.heading - angleDelta;

        if (previousHeading < 0.0f) {
            previousHeading += kTau;
        }

        root.startHeading = LerpAngle(root.heading, previousHeading, frameFrac);

        if (root.headingOverrideBlend == 0.0f) {
            root.targetHeading = root.startHeading + angleDelta;

            if (root.targetHeading >= kTau) {
                root.targetHeading -= kTau;
            }
        }

        root.hasTarget = true;
    }

    if (hadNoAnim) {
        root.startPos.y = root.targetPos.y;
    }
}

void ModelPlayback::SetAnimationWithMerge(const AnimationClip* clip, bool flip, float startFrame, float speed_,
                                          float merge, bool doMerge) {
    if (doMerge) {
        CopyAnimForMerge(merge);
    }
    SetAnimation2(clip, flip, startFrame, speed_, merge);
}

void ModelPlayback::SetAnimation(const AnimationClip* clip, bool flip, float startFrame, float speed_, float merge) {
    CopyAnimForMerge(merge);
    SetAnimation2(clip, flip, startFrame, speed_, merge);
}

void ModelPlayback::SetAnimLooping(float loopFrame, float loopMerge) {
    animLooping = true;
    animLoopFrame = loopFrame;
    animLoopMerge = loopMerge;
}

void ModelPlayback::SetAnimEndFrame(float end) {
    if (anim != nullptr && end < static_cast<float>(static_cast<int32_t>(anim->frameCount) - 1)) {
        endFrame = end;
    } else {
        endFrame = -1.0f;
    }
}

void ModelPlayback::SetAnimSpeed(float newSpeed_, float rampTime) {
    if (rampTime > 0.0f) {
        timeSpeed = rampTime;
        newSpeed = newSpeed_;
        elapseSpeed = 0.0f;
        oldSpeed = speed;
        return;
    }

    speed = newSpeed_;
    timeSpeed = 0.0f;
}

void ModelPlayback::SetAnimPlaySpeed(float rate, float rampTime) {
    if (rampTime > 0.0f) {
        playSpeedRampTime = rampTime;
        animRate = rate;
        playSpeedRampElapsed = 0.0f;
        oldPlaySpeed = playSpeed;
        return;
    }

    playSpeed = rate;
    playSpeedRampTime = 0.0f;
}

void ModelPlayback::SetAnimFrame(float newFrame) {
    const int32_t framea = FloorToInt(newFrame);

    const bool forwards = speed >= 0.0f;
    const int32_t frameb = forwards ? framea + 1 : framea - 1;

    frameA = static_cast<int16_t>(ConstrainOrWrapFrame(framea, anim, endFrame));
    frameB = static_cast<int16_t>(ConstrainOrWrapFrame(frameb, anim, endFrame));

    if (frameA == frameB) {
        frameFrac = 0.0f;
        frame = frameA;
    } else if (forwards) {
        const float tmp = newFrame - static_cast<float>(framea);
        frameFrac = tmp;
        frame = frameA + tmp;
    } else {
        const float tmp = 1.0f - (newFrame - static_cast<float>(frameb));
        frameFrac = tmp;
        frame = frameB + (1.0f - tmp);
    }
}

void ModelPlayback::SetAnimFrame2(float newFrame, float newFrame2) {
    SetAnimFrame(newFrame);

    if (anim2 != nullptr) {
        const int32_t framea = FloorToInt(newFrame2);

        const bool forwards = speed2 >= 0.0f;
        const int32_t frameb = forwards ? (framea + 1) : (framea - 1);

        frame2A = static_cast<int16_t>(ConstrainOrWrapFrame(framea, anim2, endFrame2));
        frame2B = static_cast<int16_t>(ConstrainOrWrapFrame(frameb, anim2, endFrame2));

        if (frame2A == frame2B) {
            frame2Frac = 0.0f;
            frame2 = frame2A;
        } else if (forwards) {
            const float tmp = newFrame2 - static_cast<float>(framea);
            frame2Frac = tmp;
            frame2 = frame2A + tmp;
        } else {
            const float tmp = 1.0f - (newFrame2 - static_cast<float>(frameb));
            frame2Frac = tmp;
            frame2 = frame2B + (1.0f - tmp);
        }
    }
}

void ModelPlayback::SetAnimFrame2WithRoot(float fromFrame, float toFrame, float fromFrame2, float toFrame2) {
    if (!hasRoot || root.motionDisabled) {
        SetAnimFrame2(toFrame, toFrame2);
        return;
    }

    const float rootScale = scale * animTranslationScale;

    Vec3 startPos = root.startPos;
    float currentHeading = root.startHeading;
    Vec3 targetPos = root.targetPos;
    float targetHeading = root.targetHeading;
    bool hasTarget = root.hasTarget;

    float absSpeed = speed;
    if (absSpeed < 0.0f) {
        absSpeed = -absSpeed;
    }

    float absSpeed2 = speed2;
    if (absSpeed2 < 0.0f) {
        absSpeed2 = -absSpeed2;
    }

    const bool forward = fromFrame <= toFrame;

    int32_t curFrame;
    int32_t endFrameStep;
    if (forward) {
        curFrame = FloorToInt(fromFrame) + 1;
        endFrameStep = FloorToInt(toFrame);
    } else {
        curFrame = CeilToInt(fromFrame) - 1;
        endFrameStep = CeilToInt(toFrame);
    }

    Vec3 pos;
    float angleDelta = 0.0f;

    while (true) {
        if (forward) {
            if (endFrameStep < curFrame) {
                break;
            }
        } else {
            if (curFrame < endFrameStep) {
                break;
            }
        }

        int32_t frameNumber = ConstrainOrWrapFrame(curFrame, anim, endFrame);
        frameA = static_cast<int16_t>(frameNumber);

        if (hasTarget) {
            startPos = targetPos;

            if (root.headingOverrideBlend == 0.0f) {
                currentHeading = targetHeading;
            }
        } else {
            RootMotionSample sample = SampleRootMotion(*anim, *joints, rootJoint, mirror, frameNumber);
            pos = Vec3{ sample.pos[0], sample.pos[1], sample.pos[2] };
            angleDelta = sample.angle;

            if (rootScale != 1.0f) {
                pos.x *= rootScale;
                pos.y *= rootScale;
                pos.z *= rootScale;
            }

            if (!forward) {
                pos.x = -pos.x;
                pos.z = -pos.z;

                if (0.0f < angleDelta) {
                    angleDelta = kTau - angleDelta;
                }
            }

            const float cosAngle = std::cos(root.heading);
            const float sinAngle = std::sin(root.heading);

            startPos.x += (pos.x * cosAngle) + (pos.z * sinAngle);
            startPos.y = pos.y;
            startPos.z += (-pos.x * sinAngle) + (pos.z * cosAngle);

            if (root.headingOverrideBlend == 0.0f) {
                currentHeading += angleDelta;

                if (kTau <= currentHeading) {
                    currentHeading -= kTau;
                }
            }
        }

        if (forward) {
            curFrame += 1;
        } else {
            curFrame -= 1;
        }

        frameNumber = ConstrainOrWrapFrame(curFrame, anim, endFrame);
        frameB = static_cast<int16_t>(frameNumber);

        if (frameB != frameA) {
            RootMotionSample sample = SampleRootMotion(*anim, *joints, rootJoint, mirror, frameNumber);
            pos = Vec3{ sample.pos[0], sample.pos[1], sample.pos[2] };
            angleDelta = sample.angle;
            hasTarget = true;

            if (rootScale != 1.0f) {
                pos.x *= rootScale;
                pos.y *= rootScale;
                pos.z *= rootScale;
            }

            if (!forward) {
                pos.x = -pos.x;
                pos.z = -pos.z;

                if (0.0f < angleDelta) {
                    angleDelta = kTau - angleDelta;
                }
            }

            const float cosAngle = std::cos(root.startHeading);
            const float sinAngle = std::sin(root.startHeading);

            if (gAnimMergingEnabled && anim2 != nullptr) {
                targetPos.x = (pos.z * sinAngle) + (pos.x * cosAngle);
                targetPos.z = (pos.z * cosAngle) + (-pos.x * sinAngle);

                if (0.0f < absSpeed) {
                    float t = mergeWeight - (playSpeed / (absSpeed * mergeDuration));

                    if (t < 0.0f) {
                        t = 0.0f;
                    }

                    t = (mergeWeight + t) * 0.5f;

                    const float vx = ((root.mergeTargetPos.x - root.mergeStartPos.x) * absSpeed2) / absSpeed;
                    const float vz = ((root.mergeTargetPos.z - root.mergeStartPos.z) * absSpeed2) / absSpeed;

                    targetPos.x += (vx - targetPos.x) * t;
                    targetPos.z += (vz - targetPos.z) * t;
                } else {
                    targetPos.x += (root.mergeTargetPos.x - root.mergeStartPos.x) * mergeWeight;
                    targetPos.z += (root.mergeTargetPos.z - root.mergeStartPos.z) * mergeWeight;
                }

                targetPos.x += startPos.x;
                targetPos.z += startPos.z;
                targetPos.y = pos.y;
            } else {
                targetPos.x = (startPos.x + (pos.x * cosAngle)) + (pos.z * sinAngle);
                targetPos.y = pos.y;
                targetPos.z = (startPos.z - (pos.x * sinAngle)) + (pos.z * cosAngle);
            }

            if (0.0f < root.headingCorrectionTime) {
                if (0.0f < absSpeed) {
                    float inv = 1.0f / absSpeed;

                    if (root.headingCorrectionTime < inv) {
                        inv = root.headingCorrectionTime;
                        root.headingCorrectionTime = 0.0f;
                    } else {
                        root.headingCorrectionTime -= inv;
                    }

                    angleDelta += root.headingCorrection * inv;

                    if (angleDelta < 0.0f) {
                        angleDelta += kTau;
                    } else if (kTau <= angleDelta) {
                        angleDelta -= kTau;
                    }
                }
            }

            if (root.headingOverrideBlend == 0.0f) {
                targetHeading = currentHeading + angleDelta;

                if (kTau <= targetHeading) {
                    targetHeading -= kTau;
                }
            }
        }
    }

    root.startPos = startPos;
    root.startHeading = currentHeading;
    root.targetPos = targetPos;
    root.targetHeading = targetHeading;

    if (frameB == frameA) {
        frameFrac = 0.0f;
        frame = frameA;
    } else if (forward) {
        frameFrac = toFrame - static_cast<float>(endFrameStep);
        frame = static_cast<float>(frameA) + frameFrac;
    } else {
        frameFrac = static_cast<float>(endFrameStep) - toFrame;
        frame = static_cast<float>(frameB) + (1.0f - frameFrac);
    }

    if (anim2 != nullptr) {
        const int32_t cur2 = FloorToInt(fromFrame2);
        const int32_t end2 = FloorToInt(toFrame2);

        if ((forward && (cur2 < end2)) || (!forward && (end2 < cur2))) {
            if (root.hasMergeSource) {
                root.mergeStartPos.y = root.mergeTargetPos.y;
            } else {
                root.mergeStartPos.y = root.startPos.y;
            }

            frame2A = static_cast<int16_t>(ConstrainOrWrapFrame(end2, anim2, endFrame2));
            const int32_t frameNumber2 = ConstrainOrWrapFrame(end2 + 1, anim2, endFrame2);
            frame2B = static_cast<int16_t>(frameNumber2);

            RootMotionSample sample = SampleRootMotion(*anim2, *joints, rootJoint, mirror2, frameNumber2);
            float y = sample.pos[1];

            if (rootScale != 1.0f) {
                y *= rootScale;
            }

            root.mergeTargetPos.y = y;
            root.hasMergeSource = true;
        }

        if (forward) {
            frame2Frac = toFrame2 - static_cast<float>(end2);
            frame2 = static_cast<float>(frame2A) + frame2Frac;
        } else {
            frame2Frac = 1.0f - (toFrame2 - static_cast<float>(end2));
            frame2 = static_cast<float>(frame2B) + (1.0f - frame2Frac);
        }
    } else {
        root.hasMergeSource = false;
    }
}

void ModelPlayback::TickAnim(int32_t ticks, bool updateRoot) {
    float curFrame = frame;
    float curFrame2 = frame2;

    if (ticks <= 0) {
        return;
    }

    while (ticks > 0) {
        if (playSpeedRampTime > 0.0f) {
            playSpeedRampElapsed += 1.0f;

            if (playSpeedRampElapsed < playSpeedRampTime) {
                playSpeed = oldPlaySpeed + ((animRate - oldPlaySpeed) * playSpeedRampElapsed) / playSpeedRampTime;
            } else {
                playSpeedRampTime = 0.0f;
                playSpeed = animRate;
            }
        }

        float step = playSpeed;

        if (mergeDuration > 0.0f) {
            mergeElapsed += step;

            if (mergeElapsed == 0.0f) {
                mergeWeight = 1.0f;
                step = playSpeed;
            } else if (mergeElapsed < mergeDuration) {
                mergeWeight = (mergeDuration - mergeElapsed) / mergeDuration;
                step = playSpeed;
            } else {
                mergeDuration = 0.0f;
                mergeWeight = 0.0f;
                anim2 = nullptr;
                step = playSpeed;
            }
        }

        if (timeSpeed > 0.0f) {
            elapseSpeed += step;

            if (elapseSpeed < timeSpeed) {
                speed = oldSpeed + ((newSpeed - oldSpeed) * elapseSpeed) / timeSpeed;
                step = playSpeed;
            } else {
                timeSpeed = 0.0f;
                step = playSpeed;
                speed = newSpeed;
            }
        }

        const float currentSpeed = speed;
        curFrame += step * currentSpeed;

        if (anim2 != nullptr) {
            if (timeSpeed2 > 0.0f) {
                elapseSpeed2 += step;

                if (elapseSpeed2 < timeSpeed2) {
                    speed2 = oldSpeed2 + ((newSpeed2 - oldSpeed2) * elapseSpeed2) / timeSpeed2;
                    step = playSpeed;
                } else {
                    timeSpeed2 = 0.0f;
                    step = playSpeed;
                    speed2 = newSpeed2;
                }
            }

            curFrame2 += step * speed2;
        }

        if (animLooping) {
            const float animLast = static_cast<float>(static_cast<int32_t>(anim->frameCount) - 1);
            const float savedEndFrame = endFrame;
            float limit;
            float loopFrame;

            if (currentSpeed >= 0.0f) {
                limit = animLast;
                loopFrame = animLoopFrame;

                if (savedEndFrame >= 0.0f && savedEndFrame < animLast) {
                    limit = savedEndFrame;
                }
            } else {
                limit = animLoopFrame;
                loopFrame = animLast;

                if (savedEndFrame >= 0.0f && savedEndFrame < animLast) {
                    loopFrame = savedEndFrame;
                }
            }

            if ((currentSpeed >= 0.0f && curFrame >= limit) || (currentSpeed < 0.0f && curFrame <= limit)) {
                const float savedNewSpeed = newSpeed;
                const float savedOldSpeed = oldSpeed;
                const float savedTimeSpeed = timeSpeed;
                const float savedElapseSpeed = elapseSpeed;

                if (updateRoot) {
                    SetAnimFrame2WithRoot(frame, limit, 0.0f, 0.0f);
                } else {
                    SetAnimFrame2(limit, 0.0f);
                }

                SetAnimation(anim, mirror, loopFrame, speed, animLoopMerge);

                animLooping = true;
                endFrame = savedEndFrame;
                newSpeed = savedNewSpeed;
                oldSpeed = savedOldSpeed;
                timeSpeed = savedTimeSpeed;
                elapseSpeed = savedElapseSpeed;

                curFrame2 = curFrame;
                curFrame = loopFrame + curFrame - limit;

                if (animFlipFunc) {
                    animFlipFunc();
                }
            }
        }

        ticks--;
    }

    if (updateRoot) {
        if (anim2 != nullptr) {
            SetAnimFrame2WithRoot(frame, curFrame, frame2, curFrame2);
        } else {
            SetAnimFrame2WithRoot(frame, curFrame, 0.0f, 0.0f);
        }
    } else {
        if (anim2 != nullptr) {
            SetAnimFrame2(curFrame, curFrame2);
        } else {
            SetAnimFrame2(curFrame, 0.0f);
        }
    }
}

void ModelPlayback::CalcPos() {
    if (!hasRoot || root.motionDisabled) {
        return;
    }

    Vec3 pos = root.startPos;
    root.heading = root.startHeading;

    if (frameFrac != 0.0f) {
        if (root.hasTarget) {
            InterpolateVec3(pos, root.targetPos, frameFrac);
            root.heading = LerpAngle(root.startHeading, root.targetHeading, frameFrac);
        }
    }

    if (anim2 != nullptr || mergeWeight != 0.0f) {
        if (root.hasMergeSource) {
            float y = root.mergeStartPos.y;

            if (frame2Frac != 0.0f) {
                y += (root.mergeTargetPos.y - y) * frame2Frac;
            }

            pos.y += (y - pos.y) * mergeWeight;
        }
    }

    Vec3 destination = pos;

    if (moveFilter && !moveFilter(root.pos, destination, root.ground)) {
        return;
    }

    pos.x = destination.x - pos.x;
    pos.z = destination.z - pos.z;

    root.pos.x = destination.x;
    root.pos.y = destination.y + root.ground;
    root.pos.z = destination.z;

    root.startPos.x += pos.x;
    root.startPos.z += pos.z;

    if (root.hasTarget) {
        root.targetPos.x += pos.x;
        root.targetPos.z += pos.z;
    }

    if (root.hasMergeSource) {
        root.mergeStartPos.x += pos.x;
        root.mergeStartPos.z += pos.z;
        root.mergeTargetPos.x += pos.x;
        root.mergeTargetPos.z += pos.z;
    }
}

void ModelPlayback::SetPosition(const Vec3& position) {
    if (!hasRoot) {
        return;
    }

    const float dx = position.x - root.pos.x;
    const float dz = position.z - root.pos.z;

    root.pos = position;

    root.targetPos.x += dx;
    root.targetPos.z += dz;
    root.startPos.x += dx;
    root.startPos.z += dz;
    root.mergeTargetPos.x += dx;
    root.mergeTargetPos.z += dz;
    root.mergeStartPos.x += dx;
    root.mergeStartPos.z += dz;
}

void ModelPlayback::SetHeading(float angle) {
    if (!hasRoot) {
        return;
    }

    float diff = angle - root.heading;

    if (diff < 0.0f) {
        diff += kTau;
    }

    root.startHeading += diff;

    if (root.startHeading >= kTau) {
        root.startHeading -= kTau;
    }

    root.targetHeading += diff;

    if (root.targetHeading >= kTau) {
        root.targetHeading -= kTau;
    }

    root.heading = angle;
}

} // namespace Frigate
