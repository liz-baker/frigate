#include "level/GaitHead.h"

#include <cmath>

#include <spdlog/spdlog.h>

#include "model/Random.h"
#include "model/RootMotion.h"
#include "resource/type/ModelSkeleton.h"
#include "resource/type/ModelTree.h"

namespace Frigate::Level {

namespace {

// US values (DAMPVAL, HEADSUM, CURRENTPLAYERUPDATEHEADPOS_SCALE).
constexpr float kDamp = 0.93f;
constexpr float kHeadSum = 14.285716f;
constexpr float kHeadPosScale = 0.93f;
constexpr float kModelScale = 0.10000001f;
constexpr int kTicksPerSecond = 60;

// g_BondMoveAnimationSetup's fixed fields: loop and end frame, and the top
// speed each animation is used up to (times its speed multiplier).
constexpr float kWalkLoop = 9.5f;
constexpr float kWalkEnd = 27.0f;
constexpr float kWalkThreshold = 1.5f;
constexpr float kSprintLoop = 7.5f;
constexpr float kSprintEnd = 17.0f;
constexpr float kSprintThreshold = 100.0f;

// The clips of g_bondviewBondDeathAnimations.
constexpr const char* kDeathClips[] = {
    "death_forward_face_down",           "death_forward_spin_face_up",
    "death_backward_fall_face_up1",      "death_backward_spin_face_down_right",
    "death_backward_spin_face_up_right", "death_backward_spin_face_down_left",
    "death_backward_spin_face_up_left",  "death_forward_face_down_hard",
    "death_forward_face_down_soft",      "death_fetal_position_right",
    "death_fetal_position_left",         "death_backward_fall_face_up2",
};

Vec3 Lerp(const Vec3& from, const Vec3& to, float fraction) {
    return { from[0] + (to[0] - from[0]) * fraction, from[1] + (to[1] - from[1]) * fraction,
             from[2] + (to[2] - from[2]) * fraction };
}

// The gait model's node tree (player_gait_hdr and its three position groups)
// and joint table (JOINTLIST(player_gait_object)).
std::shared_ptr<ModelTree> BuildTree() {
    auto tree = std::make_shared<ModelTree>();
    ModelNode header;
    header.offset = 0x00;
    header.type = 0x01;
    header.child = 1;
    header.jointId = 0;

    auto group = [](uint32_t offset, uint16_t joint, int16_t matrix, float x, float y, int32_t parent, int32_t child) {
        ModelNode node;
        node.offset = offset;
        node.type = 0x02;
        node.matrix = matrix;
        node.parent = parent;
        node.child = child;
        node.origin[0] = x;
        node.origin[1] = y;
        node.jointId = joint;
        return node;
    };
    tree->Nodes = { header, group(0x10, 1, 2, 0.0f, 0.0f, 0, 2), group(0x20, 2, 3, 1.177982f, 41.14437f, 1, 3),
                    group(0x30, 3, 0, -2.576027f, 480.42902f, 2, kNoNode) };
    tree->MatrixCount = 4;
    return tree;
}

std::shared_ptr<ModelSkeleton> BuildSkeleton() {
    auto skeleton = std::make_shared<ModelSkeleton>();
    skeleton->Joints = { { 0x0401, 0x0000, 0x0000 }, { 0x0002, 0x0000, 0x0000 }, { 0x0002, 0x0003, 0x0003 },
                         { 0x0002, 0x0006, 0x0006 }, { 0x0002, 0x0009, 0x000C }, { 0x0002, 0x000C, 0x0009 },
                         { 0x0002, 0x000F, 0x0012 }, { 0x0002, 0x0012, 0x000F }, { 0x0002, 0x0015, 0x0018 },
                         { 0x0002, 0x0018, 0x0015 }, { 0x0002, 0x001B, 0x001E }, { 0x0002, 0x001E, 0x001B },
                         { 0x0002, 0x0021, 0x0024 }, { 0x0002, 0x0024, 0x0021 }, { 0x0002, 0x0027, 0x002A },
                         { 0x0002, 0x002A, 0x0027 } };
    return skeleton;
}

} // namespace

std::unique_ptr<GaitHead> GaitHead::Create(const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    std::unique_ptr<GaitHead> head(new GaitHead());
    head->model_ = Model::ModelInstance::FromParts("player_gait_object", BuildTree(), BuildSkeleton());
    head->idle_ = head->model_->LoadClip(resourceManager, "idle");
    head->moves_[0].clip = head->model_->LoadClip(resourceManager, "bond_eye_walk");
    head->moves_[1].clip = head->model_->LoadClip(resourceManager, "sprinting");
    if (head->idle_ == nullptr || head->moves_[0].clip == nullptr || head->moves_[1].clip == nullptr) {
        spdlog::error("head: the gait animations are missing");
        return nullptr;
    }
    for (const char* name : kDeathClips) {
        const AnimationClip* clip = head->model_->LoadClip(resourceManager, name);
        if (clip == nullptr) {
            spdlog::error("head: the death animations are missing");
            return nullptr;
        }
        head->deathClips_.push_back(clip);
    }
    head->moves_[0].loopFrame = kWalkLoop;
    head->moves_[0].endFrame = kWalkEnd;
    head->moves_[0].threshold = kWalkThreshold;
    head->moves_[1].loopFrame = kSprintLoop;
    head->moves_[1].endFrame = kSprintEnd;
    head->moves_[1].threshold = kSprintThreshold;
    head->Init();
    return head;
}

// bheadFlipAnimation. The player's animFlipFlag is the gunhand byte of its
// embedded model, which is the animation's mirror flag: each loop of the gait
// clip swaps the foot, and a rebind carries the current mirror over.
std::function<void()> GaitHead::FlipMirror() {
    return [this] { model_->playback.mirror = !model_->playback.mirror; };
}

void GaitHead::BindAnimation(int index, float startFrame) {
    ModelPlayback& playback = model_->playback;
    const MoveAnimation& move = moves_[static_cast<size_t>(index)];
    playback.SetAnimation(move.clip, playback.mirror, startFrame, 0.5f, 12.0f);
    playback.SetAnimLooping(move.loopFrame, 0.0f);
    playback.SetAnimEndFrame(move.endFrame);
    playback.animFlipFunc = FlipMirror();
}

// sets_a_bunch_of_BONDdata_values_to_default's head half.
void GaitHead::Init() {
    ModelPlayback& playback = model_->playback;
    playback.scale = kModelScale;

    headAnim_ = 0;
    headDamp_ = kDamp;
    headWalkingTime60_ = 0;
    headAmplitude_ = 1.0f;
    sideAmplitude_ = 1.0f;
    headPos_ = { 0, 0, 0 };
    headLook_ = { 0, 0, 0 };
    headUp_ = { 0, 0, 0 };
    headPosSum_ = { 0, 0, 0 };
    headLookSum_ = { 0, 0, kHeadSum };
    headUpSum_ = { 0, kHeadSum, 0 };
    resetHeadPos_ = true;
    resetHeadRot_ = true;
    standFrac_ = 0;
    standLook_ = { Vec3{ 0, 0, 1 }, Vec3{ 0, 0, 1 } };
    standUp_ = { Vec3{ 0, 1, 0 }, Vec3{ 0, 1, 0 } };
    standCount_ = 0;
    startNewDeath_ = true;

    // How far each animation carries the player per unit of animation speed:
    // the root's forward travel over its loop, in the model's scale.
    const std::vector<JointChannels>& joints = *playback.joints;
    for (MoveAnimation& move : moves_) {
        int32_t sum = 0;
        for (int32_t frame = static_cast<int32_t>(move.loopFrame); frame < static_cast<int32_t>(move.endFrame);
             frame++) {
            sum += static_cast<int16_t>(std::lround(SampleRootMotion(*move.clip, joints, 0, false, frame).pos[2]));
        }
        move.speedMultiplier = (static_cast<float>(sum) * kModelScale) / (move.endFrame - move.loopFrame);
    }

    // The standing pose sets the head's height.
    playback.SetAnimation(idle_, false, 0.0f, 0.5f, 0.0f);
    playback.CalcPos();
    model_->Pose(IdentityMat4());
    const auto& matrices = model_->Walk().matrices;
    standHeight_ = matrices[0].m[3][1];
    standBodyOffset_ = { 0.0f, matrices[1].m[3][1] - matrices[0].m[3][1], matrices[1].m[3][2] - matrices[0].m[3][2] };

    playback.SetAnimation(moves_[0].clip, false, moves_[0].loopFrame, 0.5f, 0.0f);
    playback.SetAnimLooping(moves_[0].loopFrame, 0.0f);
    playback.SetAnimEndFrame(moves_[0].endFrame);
    playback.animFlipFunc = FlipMirror();
    UpdateIdleRoll();
}

void GaitHead::UpdateIdleRoll() {
    const float multiplier = 1.0f / 4294967295.0f;
    auto random = [&] { return static_cast<float>(Frigate::RandomGetNext()) * multiplier; };

    Vec3& look = standLook_[static_cast<size_t>(standCount_)];
    Vec3& up = standUp_[static_cast<size_t>(standCount_)];
    look[0] = (random() - 0.5f) * 0.02f;
    look[2] = 1;
    up[0] = (random() - 0.5f) * 0.02f;
    up[1] = 1;

    if (standCount_ != 0) {
        look[1] = random() * 0.01f;
        up[2] = random() * -0.01f;
    } else {
        look[1] = random() * -0.01f;
        up[2] = random() * 0.01f;
    }
    standCount_ = 1 - standCount_;
}

void GaitHead::UpdatePos(const Vec3& velocity, const MoveContext& context) {
    if (resetHeadPos_) {
        headPosSum_ = { 0.0f, velocity[1] / (1.0f - kHeadPosScale), 0.0f };
        resetHeadPos_ = false;
    }
    for (int i = 0; i < context.clockTimer; i++) {
        for (size_t axis = 0; axis < 3; axis++) {
            headPosSum_[axis] = (kHeadPosScale * headPosSum_[axis]) + velocity[axis];
        }
    }
    for (size_t axis = 0; axis < 3; axis++) {
        headPos_[axis] = headPosSum_[axis] * (1.0f - kHeadPosScale);
    }
}

void GaitHead::UpdateRot(const Vec3& lookVelocity, const Vec3& upVelocity, const MoveContext& context) {
    if (resetHeadRot_) {
        for (size_t axis = 0; axis < 3; axis++) {
            headLookSum_[axis] = lookVelocity[axis] / (1.0f - headDamp_);
            headUpSum_[axis] = upVelocity[axis] / (1.0f - headDamp_);
        }
        resetHeadRot_ = false;
    }
    for (int i = 0; i < context.clockTimer; i++) {
        for (size_t axis = 0; axis < 3; axis++) {
            headLookSum_[axis] = headDamp_ * headLookSum_[axis] + lookVelocity[axis];
            headUpSum_[axis] = headDamp_ * headUpSum_[axis] + upVelocity[axis];
        }
    }
    for (size_t axis = 0; axis < 3; axis++) {
        headLook_[axis] = headLookSum_[axis] * (1.0f - headDamp_);
        headUp_[axis] = headUpSum_[axis] * (1.0f - headDamp_);
    }
}

void GaitHead::SetDamp(float damp) {
    if (damp != headDamp_) {
        const float divisor = 1.0f - damp;
        for (size_t axis = 0; axis < 3; axis++) {
            headLookSum_[axis] = (headLookSum_[axis] * (1.0f - headDamp_)) / divisor;
            headUpSum_[axis] = (headUpSum_[axis] * (1.0f - headDamp_)) / divisor;
        }
        headDamp_ = damp;
    }
}

void GaitHead::AdjustAnimation(float speed) {
    ModelPlayback& playback = model_->playback;
    speed *= moves_[1].speedMultiplier;

    for (int i = 0; i < 2; i++) {
        const MoveAnimation& move = moves_[static_cast<size_t>(i)];
        if (speed <= move.threshold * move.speedMultiplier) {
            if (i != headAnim_) {
                float startFrame = 0.0f;
                if (headAnim_ >= 0) {
                    const MoveAnimation& current = moves_[static_cast<size_t>(headAnim_)];
                    startFrame = (playback.frame - current.loopFrame) / (current.endFrame - current.loopFrame);
                    startFrame = move.loopFrame + ((move.endFrame - move.loopFrame) * startFrame);
                }
                BindAnimation(i, startFrame);
                headAnim_ = i;
            }

            speed /= move.speedMultiplier;
            playback.SetAnimSpeed(speed * 0.5f, 0.0f);
            return;
        }
    }
}

bool GaitHead::DeathAnimationFinished() const {
    return model_->playback.frame >= model_->playback.AnimEndFrame();
}

float GaitHead::BreathingValue(float breathing) const {
    if (headAnim_ >= 0) {
        const float base = breathing * 0.012500001f + 0.004166667f;
        const float animSpeed = std::fabs(model_->playback.speed);
        if (animSpeed > 0) {
            const MoveAnimation& move = moves_[static_cast<size_t>(headAnim_)];
            float calculated = animSpeed / (move.endFrame - move.loopFrame);
            if (calculated < base) {
                calculated = base;
            }
            return calculated;
        }
        return base;
    }
    return 0;
}

Vec3 GaitHead::Tick(float speed, float speedForwards, float speedSideways, float breathing,
                    const MoveContext& context) {
    ModelPlayback& playback = model_->playback;
    const float delta = context.globalTimerDelta;

    // bondviewMoveAnimationTick.
    float percentSpeed = 0.0f;
    if (!context.dead) {
        AdjustAnimation(speed);
        if (speed != 0.0f) {
            percentSpeed = speedForwards / speed;
        }
    } else {
        if (startNewDeath_) {
            // bheadStartDeathAnimation.
            const size_t clip = Frigate::RandomGetNext() % deathClips_.size();
            const bool flip = (Frigate::RandomGetNext() & 1) != 0;
            playback.SetAnimation(deathClips_[clip], flip, 0.0f, 1.0f * 0.5f, 12.0f);
            headAnim_ = -1;
            startNewDeath_ = false;
        }
        // bheadSetSpeed(0.5f).
        playback.SetAnimSpeed(0.5f * 0.5f, 0.0f);
        speedSideways = 0.0f;
    }
    const float sideways = speedSideways * (moves_[1].speedMultiplier * 0.5f * delta);

    // bheadUpdate.
    Vec3 headPos = { 0, 0, 0 };
    Vec3 lookVelocity = { 0, 0, 1 };
    Vec3 upVelocity = { 0, 1, 0 };

    const float animSpeed = std::fabs(playback.speed);
    if (headAnim_ == 0) {
        if (animSpeed > 0.7f) {
            headAmplitude_ = 1.0f;
        } else if (animSpeed > 0.1f) {
            headAmplitude_ = (((animSpeed - 0.1f) * 0.6f) / 0.59999996f) + 0.4f;
        } else {
            headAmplitude_ = 0.4f;
        }
        sideAmplitude_ = headAmplitude_;
    } else if (headAnim_ == 1) {
        headAmplitude_ = 0.9f;
        sideAmplitude_ = 0.5f;
    } else {
        headAmplitude_ = 1.0f;
        sideAmplitude_ = headAmplitude_;
    }

    const bool merging = AnimMergingEnabled();
    SetAnimMergingEnabled(false);
    playback.TickAnim(context.clockTimer, true);
    SetAnimMergingEnabled(merging);

    playback.CalcPos();
    model_->Pose(IdentityMat4());
    Mat4 head = model_->Walk().matrices[0];

    // Keep the model where the tick started: what the head matrix moved by is
    // this tick's motion.
    Frigate::Vec3 offset = playback.root.pos;
    offset.x -= head.m[3][0];
    offset.z -= head.m[3][2];
    playback.SetPosition(offset);

    if (animSpeed > 0.0f) {
        head.m[3][0] += sideways;
        head.m[3][2] *= percentSpeed;

        if (context.clockTimer > 0) {
            head.m[3][0] /= delta;
            head.m[3][2] /= delta;
        }

        headPos[0] = head.m[3][0] * headAmplitude_;
        headPos[1] = ((head.m[3][1] - standHeight_) * headAmplitude_) + standHeight_;
        headPos[2] = head.m[3][2] * headAmplitude_;

        if (headAnim_ >= 0) {
            lookVelocity[0] = head.m[2][0] * sideAmplitude_;
            lookVelocity[1] = head.m[2][1] * headAmplitude_;
            lookVelocity[2] = ((head.m[2][2] - 1.0f) * headAmplitude_) + 1.0f;

            upVelocity[0] = head.m[1][0] * headAmplitude_;
            upVelocity[1] = ((head.m[1][1] - 1.0f) * headAmplitude_) + 1.0f;
            upVelocity[2] = head.m[1][2] * headAmplitude_;

            headWalkingTime60_ += context.clockTimer;
            SetDamp(headWalkingTime60_ > kTicksPerSecond ? 0.93f : 0.99f);
        } else {
            lookVelocity = { head.m[2][0], head.m[2][1], head.m[2][2] };
            upVelocity = { head.m[1][0], head.m[1][1], head.m[1][2] };
            SetDamp(0.85f);
        }
    } else {
        headPos = { 0.0f, standHeight_, 0.0f };
        headWalkingTime60_ = 0;
        SetDamp(0.99f);

        standFrac_ += (0.008333334f + (0.025000002f * breathing)) * delta;
        if (standFrac_ >= 1.0f) {
            UpdateIdleRoll();
            standFrac_ -= 1.0f;
        }

        lookVelocity = Lerp(standLook_[static_cast<size_t>(standCount_)],
                            standLook_[static_cast<size_t>(1 - standCount_)], standFrac_);
        lookVelocity[0] *= (1.0f + (5.0f * breathing));
        lookVelocity[1] *= (1.0f + (5.0f * breathing));

        upVelocity = Lerp(standUp_[static_cast<size_t>(standCount_)], standUp_[static_cast<size_t>(1 - standCount_)],
                          standFrac_);
        upVelocity[0] *= (1.0f + (5.0f * breathing));
        upVelocity[2] *= (1.0f + (5.0f * breathing));
    }

    UpdatePos(headPos, context);
    UpdateRot(lookVelocity, upVelocity, context);
    return headPos_;
}

} // namespace Frigate::Level
