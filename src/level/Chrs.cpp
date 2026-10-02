#include "level/Chrs.h"
#include "model/GameMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace Frigate::Level {

namespace {

// list_of_bodies, random_male_heads and random_female_heads.
constexpr std::array<std::string_view, 42> kBodies = { "BODY_Jungle_Commando",
                                                       "BODY_St_Petersburg_Guard",
                                                       "BODY_Russian_Soldier",
                                                       "BODY_Russian_Infantry",
                                                       "BODY_Janus_Special_Forces",
                                                       "BODY_Brosnan_Tuxedo",
                                                       "BODY_Boris",
                                                       "BODY_Ourumov",
                                                       "BODY_Trevelyan_Janus",
                                                       "BODY_Valentin_",
                                                       "BODY_Xenia",
                                                       "BODY_Baron_Samedi",
                                                       "BODY_Jaws",
                                                       "BODY_Mayday",
                                                       "BODY_Oddjob",
                                                       "BODY_Natalya_Skirt",
                                                       "BODY_Janus_Marine",
                                                       "BODY_Russian_Commandant",
                                                       "BODY_Siberian_Guard_1_Mishkin",
                                                       "BODY_Naval_Officer",
                                                       "BODY_Siberian_Special_Forces",
                                                       "BODY_Special_Operations_Uniform",
                                                       "BODY_Formal_Wear",
                                                       "BODY_Jungle_Fatigues",
                                                       "BODY_Unused_Female",
                                                       "BODY_Rosika",
                                                       "BODY_Scientist_2_Female",
                                                       "BODY_Civilian_1_Female",
                                                       "BODY_Unused_Male_1",
                                                       "BODY_Unused_Male_2",
                                                       "BODY_Civilian_4",
                                                       "BODY_Civilian_2",
                                                       "BODY_Civilian_3",
                                                       "BODY_Scientist_1_Male",
                                                       "BODY_Brosnan_Tuxedo",
                                                       "BODY_Brosnan_Tuxedo",
                                                       "BODY_Brosnan_Tuxedo",
                                                       "BODY_Helicopter_Pilot",
                                                       "BODY_Siberian_Guard_2",
                                                       "BODY_Arctic_Commando",
                                                       "BODY_Moonraker_Elite_1_Male",
                                                       "BODY_Moonraker_Elite_2_Female" };
constexpr std::array<std::string_view, 25> kMaleHeads = {
    "HEAD_Male_Jim",    "HEAD_Male_Chris", "HEAD_Male_Lee",         "HEAD_Male_Graeme", "HEAD_Male_Steve_H",
    "HEAD_Male_Neil",   "HEAD_Male_Robin", "HEAD_Male_Des",         "HEAD_Male_Grant",  "HEAD_Male_Dave_Dr_Doak",
    "HEAD_Male_Karl",   "HEAD_Male_Alan",  "HEAD_Male_Pete",        "HEAD_Male_Martin", "HEAD_Male_Mark",
    "HEAD_Male_Duncan", "HEAD_Male_Shaun", "HEAD_Male_Dwayne",      "HEAD_Male_B",      "HEAD_Male_Steve_Ellis",
    "HEAD_Male_Joel",   "HEAD_Male_Scott", "HEAD_Male_Joe_Altered", "HEAD_Male_Ken",    "HEAD_Male_Joe"
};
constexpr std::array<std::string_view, 4> kFemaleHeads = { "HEAD_Female_Sally", "HEAD_Female_Marion_Rosika",
                                                           "HEAD_Female_Mandy", "HEAD_Female_Vivien" };

constexpr float kTau = 6.2831855f;
// getposstan's radius for a guard's start position.
constexpr float kSpawnRadius = 20.0f;
// The tests init_GUARDdata_with_set_values and the character moves run: objects,
// doors, players, characters and path blockers.
constexpr int kMoveCdTypes = CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker;
// The chr's own size before a record changes it.
constexpr float kDefaultVisionRange = 250.0f;
// get_007_health_mod: the 007 difficulty's health slider, which the front end
// leaves at 1.
constexpr float kHealthModifier = 1.0f;
// chrDetectRooms' box around the chr and the radius it searches.
constexpr float kRoomBoxHalfWidth = 50.0f;
constexpr float kRoomBoxHeight = 100.0f;
// GROUND_SMOOTH_FACTOR (NTSC).
constexpr float kGroundSmoothFactor = 0.100000024f;
// FALLSPEED_DECAY, and the speed under which a chr's sideways fall speed stops.
constexpr float kFallSpeedDecay = 0.9f;
constexpr float kFallSpeedFloor = 0.1f;
// sub_GAME_7F057D44's per-tick gravity, 0.2777778.
constexpr float kGravity = 0.2777778f;
// makeonebody scales a body's record scale by this.
constexpr float kBodyScale = 0.10000001f;

std::vector<int> ResolveNames(const Game::ChrRecords& records, const std::string_view* names, size_t count) {
    std::vector<int> slots;
    for (size_t i = 0; i < count; i++) {
        slots.push_back(records.Slot(names[i]));
    }
    return slots;
}

} // namespace

void Chrs::Reset(Stan* stan, PropList* props, std::shared_ptr<const Game::ChrRecords> records,
                 std::shared_ptr<const Game::ChrHats> hats, ModelFactory* models) {
    stan_ = stan;
    props_ = props;
    records_ = std::move(records);
    hats_ = std::move(hats);
    models_ = models;
    animationRate_ = 1.0f;
    timing_ = ChrTiming{};
    drawn_ = 0;
    headStart_ = -1;
    femaleHeadStart_ = -1;
    chrs_.clear();
    byDef_.clear();
    nextChrnum_ = kFirstAnonymousChrnum;
    randomBody_ = 0;
    randomMaleHead_ = 0;
    randomFemaleHead_ = 0;
    bodies_.clear();
    maleHeads_.clear();
    femaleHeads_.clear();
    if (records_) {
        ResolveTables();
    }
}

void Chrs::ResolveTables() {
    headStart_ = records_->Slot("HEAD_Male_Karl");
    femaleHeadStart_ = records_->Slot("HEAD_Female_Sally");
    bodies_ = ResolveNames(*records_, kBodies.data(), kBodies.size());
    maleHeads_ = ResolveNames(*records_, kMaleHeads.data(), kMaleHeads.size());
    femaleHeads_ = ResolveNames(*records_, kFemaleHeads.data(), kFemaleHeads.size());
}

void Chrs::AllocateSlots(int count) {
    // Ten more than the setup's guards, for the characters spawned during play.
    chrs_.assign(static_cast<size_t>(count + 10), Chr{});
}

void Chrs::BodiesReset(const Random& random) {
    // The decomp's models are unloaded here too; nothing is loaded yet.
    randomMaleHead_ = random() % static_cast<uint32_t>(kMaleHeads.size());
    randomFemaleHead_ = random() % static_cast<uint32_t>(kFemaleHeads.size());
    randomBody_ = random() % static_cast<uint32_t>(kBodies.size());
}

int Chrs::CurrentRandomBody() const {
    return randomBody_ < bodies_.size() ? bodies_[randomBody_] : -1;
}

int Chrs::ChooseHead(int body, const Random& random) {
    const Game::ChrRecord* record = records_->At(body);
    if (record != nullptr && record->isMale) {
        int index = static_cast<int32_t>(random() & 3);
        index = (static_cast<int32_t>(randomMaleHead_) + index) % static_cast<int32_t>(kMaleHeads.size());
        return maleHeads_[static_cast<size_t>(index)];
    }
    return randomFemaleHead_ < femaleHeads_.size() ? femaleHeads_[randomFemaleHead_] : -1;
}

bool Chrs::GetPosStan(Stan& stan, const Vec3& position, int tile, float radius, int* tileOut) {
    *tileOut = tile;
    if (tile == kNoTile) {
        return false;
    }
    return !(radius > 0.0f &&
             stan.TestVolume(tileOut, position[0], position[2], radius, kMoveCdTypes, 0.0f, 1.0f) >= 0);
}

void Chrs::WidthHeight(const Chr& chr, float* width, float* height, float* always20) {
    *width = chr.chrWidth;
    *height = chr.chrHeight - 20.0f;
    *always20 = 20.0f;
}

bool Chrs::Spawn(const StageSetup& setup, size_t def, const ChrTiming& timing, const Random& random) {
    const PropDef& record = setup.PropDefs()[def];
    if (!records_ || record.guardPad >= setup.Pads().size()) {
        return false;
    }
    const Pad& pad = setup.Pads()[record.guardPad];
    int tile = kNoTile;
    if (!GetPosStan(*stan_, pad.position, pad.stanTile, kSpawnRadius, &tile)) {
        return false;
    }

    int head = -1;
    const int body = record.guardBody == 0xFFFF ? CurrentRandomBody() : record.guardBody;
    const Game::ChrRecord* bodyRecord = records_->At(body);
    if (bodyRecord == nullptr) {
        return false;
    }
    if (!bodyRecord->hasHead) {
        head = record.guardHead >= 0 ? record.guardHead : ChooseHead(body, random);
    }

    // retrieve_header_for_body_and_head.
    bool sunglasses = false;
    if ((record.guardFlags & GuardSetupFlag::Sunglasses) != 0) {
        sunglasses = true;
    } else if ((record.guardFlags & GuardSetupFlag::RandomSunglasses) != 0) {
        sunglasses = (random() & 1) == 0;
    }
    std::shared_ptr<ModelHandle> model;
    if (models_ != nullptr) {
        model = MakeOneBody(body, head, sunglasses);
        if (!model) {
            return false;
        }
    }

    const int prop = props_->Allocate();
    if (prop < 0) {
        return false;
    }
    const int slot = InitChr(prop, pad.position, tile, GameAtan2f(pad.look[0], pad.look[2]), timing, std::move(model),
                             bodyRecord->radius);
    if (slot < 0) {
        props_->Free(prop);
        return false;
    }
    props_->Activate(prop);
    props_->Enable(prop);

    Chr& chr = chrs_[static_cast<size_t>(slot)];
    chr.chrnum = static_cast<int16_t>(record.guardChrnum);
    chr.hearingScale = static_cast<float>(record.guardHealth) / 1000.0f;
    chr.visionRange = static_cast<float>(record.guardReactionTime);
    chr.padPreset1 = static_cast<int16_t>(record.guardPreset);
    chr.chrPreset1 = static_cast<int16_t>(record.guardChrPreset1);
    chr.headnum = static_cast<int8_t>(head);
    chr.bodynum = static_cast<int8_t>(body);
    chr.sunglasses = sunglasses;
    chr.aiList = aiLists_ != nullptr ? aiLists_->FindById(record.guardAiList) : nullptr;
    if ((record.guardFlags & GuardSetupFlag::Clone) != 0) {
        chr.chrflags |= ChrFlag::Clone;
    }
    if ((record.guardFlags & GuardSetupFlag::Invincible) != 0) {
        chr.chrflags |= ChrFlag::Invincible;
    }
    if (byDef_.size() <= def) {
        byDef_.resize(setup.PropDefs().size(), -1);
    }
    byDef_[def] = slot;
    return true;
}

bool Chrs::AdjustPosForSpawn(Vec3* position, int* tile, float facing, bool allowOnScreen,
                             const OffScreenFn& offScreen) {
    constexpr int kSpawnCdTypes = CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker;
    constexpr int kLineCdTypes = CdType::Objs | CdType::Doors | CdType::PathBlocker;
    int current = *tile;
    if (stan_->TestVolume(&current, (*position)[0], (*position)[2], kSpawnRadius, kSpawnCdTypes, 0.0f, 1.0f) < 0 &&
        (allowOnScreen || offScreen(*position, *tile))) {
        return true;
    }

    for (int i = 0; i < 8; i++) {
        const Vec3 test = { (*position)[0] + (GameSinf(facing) * 60.0f), (*position)[1],
                            (*position)[2] + (GameCosf(facing) * 60.0f) };
        current = *tile;
        if (stan_->TestLineUnobstructed(&current, (*position)[0], (*position)[2], test[0], test[2], kLineCdTypes, 0.0f,
                                        1.0f, 0.0f, 1.0f) != 0 &&
            stan_->TestVolume(&current, test[0], test[2], kSpawnRadius, kSpawnCdTypes, 0.0f, 1.0f) < 0 &&
            (allowOnScreen || offScreen(test, current))) {
            *tile = current;
            (*position)[0] = test[0];
            (*position)[2] = test[2];
            return true;
        }

        facing += 0.7853982f;
        if (facing >= kTau) {
            facing -= kTau;
        }
    }
    return false;
}

int Chrs::SpawnAtCoord(int body, int head, const Vec3& position, int tile, float angle, const AiList* list,
                       uint32_t flags, const ChrTiming& timing, const Random& random, const OffScreenFn& offScreen) {
    // chrSpawnAtCoord keeps three slots in reserve.
    if (FreeSlots() < 3 || records_ == nullptr) {
        return -1;
    }
    if (head < 0) {
        head = ChooseHead(body, random);
    }

    Vec3 spawnPosition = position;
    int spawnTile = tile;
    if (!AdjustPosForSpawn(&spawnPosition, &spawnTile, angle, (flags & 0x10) != 0, offScreen)) {
        return -1;
    }

    const Game::ChrRecord* bodyRecord = records_->At(body);
    if (bodyRecord == nullptr) {
        return -1;
    }
    // retrieve_header_for_body_and_head.
    bool sunglasses = false;
    if ((flags & GuardSetupFlag::Sunglasses) != 0) {
        sunglasses = true;
    } else if ((flags & GuardSetupFlag::RandomSunglasses) != 0) {
        sunglasses = (random() & 1) == 0;
    }
    std::shared_ptr<ModelHandle> model;
    if (models_ != nullptr) {
        model = MakeOneBody(body, bodyRecord->hasHead ? -1 : head, sunglasses);
        if (!model) {
            return -1;
        }
    }

    const int prop = props_->Allocate();
    if (prop < 0) {
        return -1;
    }
    const int slot = InitChr(prop, spawnPosition, spawnTile, angle, timing, std::move(model), bodyRecord->radius);
    if (slot < 0) {
        props_->Free(prop);
        return -1;
    }
    props_->ActivateThisFrame(prop);
    props_->Enable(prop);

    Chr& chr = chrs_[static_cast<size_t>(slot)];
    chr.headnum = static_cast<int8_t>(head);
    chr.bodynum = static_cast<int8_t>(body);
    chr.sunglasses = sunglasses;
    chr.aiList = list;
    return slot;
}

std::shared_ptr<ModelHandle> Chrs::MakeOneBody(int body, int head, bool sunglasses) {
    const Game::ChrRecord* record = records_->At(body);

    // A body that carries its own head takes no other.
    std::string headPath;
    if (!record->hasHead && head >= 0) {
        const Game::ChrRecord* headRecord = records_->At(head);
        if (headRecord == nullptr) {
            return nullptr;
        }
        headPath = headRecord->modelPath;
    }
    auto model = models_->CreateBody(record->modelPath, headPath, sunglasses);
    if (!model) {
        return nullptr;
    }

    // TODO(https://github.com/liz-baker/frigate/issues/136): the DK Mode cheat,
    // which scales every body chrCanUseDKModeScaling names by 0.8.
    model->Playback().scale = record->scale * kBodyScale;
    model->Playback().animTranslationScale = record->pov;
    return model;
}

int Chrs::InitChr(int prop, const Vec3& position, int tile, float angle, const ChrTiming& timing,
                  std::shared_ptr<ModelHandle> model, float boundingRadius) {
    const auto free = std::find_if(chrs_.begin(), chrs_.end(), [](const Chr& chr) { return !chr.inUse; });
    if (free == chrs_.end()) {
        return -1;
    }
    const int slot = static_cast<int>(free - chrs_.begin());
    Prop& record = (*props_)[prop];
    record.type = PropType::Chr;
    record.object = slot;
    record.position = position;
    record.stan = tile;

    Chr& chr = *free;
    chr = Chr{};
    chr.inUse = true;
    chr.chrnum = static_cast<int16_t>(nextChrnum_++);
    chr.prop = prop;
    chr.lastMoveOk60 = timing.globalTimer;
    chr.visionRange = kDefaultVisionRange;
    chr.maxDamage = kHealthModifier * 4.0f;
    chr.chrflags = ChrFlag::Init;
    chr.prevPosition = position;
    chr.chrWidth = 20.0f;
    chr.chrHeight = 185.0f;
    chr.boundingRadius = boundingRadius;
    chr.model = std::move(model);
    if (chr.model) {
        ModelPlayback& playback = chr.model->Playback();
        // sub_GAME_7F06FF5C: the model's root moves run through
        // sub_GAME_7F01FC10.
        playback.moveFilter = [this, slot](const Frigate::Vec3& from, Frigate::Vec3& to, float& ground) {
            const Vec3 source = { from.x, from.y, from.z };
            Vec3 destination = { to.x, to.y, to.z };
            UpdateGround(slot, &source, &destination, &ground, timing_);
            to = { destination[0], destination[1], destination[2] };
            return true;
        };
        // setsuboffset, setsubroty and modelSetAnimPlaySpeed.
        playback.SetPosition({ position[0], position[1], position[2] });
        playback.SetHeading(angle);
        playback.SetAnimPlaySpeed(animationRate_, 0.0f);
    }
    chr.nextColour = TileShading(tile);
    chr.shadeColour = chr.nextColour;

    // sub_GAME_7F01FC10 with the source and destination both the prop's
    // position.
    timing_ = timing;
    UpdateGround(slot, &record.position, &record.position, &chr.ground, timing);
    DetectRooms(slot);
    return slot;
}

ShadeColour Chrs::TileShading(int tile) const {
    if (tile == kNoTile || stan_ == nullptr) {
        return {};
    }
    return ShadeFromTile(stan_->Tile(tile));
}

void Chrs::UpdateGround(int chrSlot, const Vec3* from, Vec3* to, float* groundOut, const ChrTiming& timing) {
    Chr& chr = chrs_[static_cast<size_t>(chrSlot)];
    Prop& prop = (*props_)[chr.prop];
    bool moved = false;
    float ground = 0.0f;
    const Vec3* groundPosition = from;

    if (prop.stan != kNoTile) {
        if (chr.actionType == ChrAction::Die && chr.actDie.timeExtra > ground) {
            // The knock-back of the shot that killed the character, spent over
            // timeExtra ticks.
            const float playSpeed = chr.model ? chr.model->Playback().playSpeed : 1.0f;
            const float scale =
                ((playSpeed * timing.globalTimerDelta) * (chr.actDie.timeExtra - chr.actDie.elapseExtra)) /
                chr.actDie.timeExtra;
            (*to)[0] += chr.actDie.extraSpeed[0] * scale;
            (*to)[1] += chr.actDie.extraSpeed[1] * scale;
            (*to)[2] += chr.actDie.extraSpeed[2] * scale;
            chr.actDie.elapseExtra += timing.globalTimerDelta * playSpeed;
            if (chr.actDie.timeExtra < chr.actDie.elapseExtra) {
                chr.actDie.timeExtra = 0.0f;
            }
        }

        (*to)[0] += chr.fallSpeed[0] * timing.globalTimerDelta;
        (*to)[2] += chr.fallSpeed[2] * timing.globalTimerDelta;

        const int tile = TestMove(chrSlot, prop.stan, from, to, true, timing);
        if (tile != kNoTile) {
            prop.stan = tile;
            groundPosition = to;
            moved = true;
        }

        if ((chr.chrflags & ChrFlag::LockYPos) == 0) {
            ground = stan_->PositionY(prop.stan, (*groundPosition)[0], (*groundPosition)[2]);
            chr.ground = ground;
            if ((chr.chrflags & ChrFlag::Init) != 0) {
                chr.chrflags &= ~ChrFlag::Init;
                chr.manGround = chr.ground;
                chr.sumGround = chr.ground / kGroundSmoothFactor;
                // The root's step starts where it ends.
                if (chr.model) {
                    RootState& root = chr.model->Playback().root;
                    root.startPos.y = root.targetPos.y;
                }
            } else {
                if (chr.fallSpeed[1] != 0.0f || chr.manGround > chr.ground) {
                    // sub_GAME_7F057D44.
                    const float speed = chr.fallSpeed[1];
                    const float next = speed - (timing.globalTimerDelta * kGravity);
                    chr.manGround += (timing.globalTimerDelta * (speed + next)) * 0.5f;
                    chr.fallSpeed[1] = next;

                    if (chr.manGround <= chr.ground) {
                        chr.manGround = chr.ground;
                        chr.sumGround = chr.ground / kGroundSmoothFactor;
                        chr.fallSpeed[1] = 0.0f;
                    }
                }

                if (chr.manGround <= chr.ground) {
                    for (int i = 0; i < timing.clockTimer; i++) {
                        chr.sumGround = (chr.sumGround * kFallSpeedDecay) + chr.ground;
                        chr.fallSpeed[0] *= kFallSpeedDecay;
                        chr.fallSpeed[2] *= kFallSpeedDecay;
                    }

                    chr.manGround = chr.sumGround * kGroundSmoothFactor;

                    if (chr.fallSpeed[0] < kFallSpeedFloor && -kFallSpeedFloor < chr.fallSpeed[0] &&
                        chr.fallSpeed[2] < kFallSpeedFloor && -kFallSpeedFloor < chr.fallSpeed[2]) {
                        chr.fallSpeed[2] = 0.0f;
                        chr.fallSpeed[0] = 0.0f;
                    }
                }
            }
            (*to)[1] += chr.manGround - ground;
        } else {
            ground = chr.ground;
        }
    }
    *groundOut = ground;
    if (!moved) {
        (*to)[0] = (*from)[0];
        (*to)[2] = (*from)[2];
    }
}

int Chrs::TestMove(int chrSlot, int stanTile, const Vec3* src, Vec3* dst, bool updateLastMoveOk,
                   const ChrTiming& timing) {
    Chr& chr = chrs_[static_cast<size_t>(chrSlot)];
    float width;
    float height;
    float always20;
    WidthHeight(chr, &width, &height, &always20);
    chr.hidden |= ChrHidden::Moving;
    stan_->ResetHits();

    int ret = kNoTile;
    // Whether the step to (x, z) is clear; `tile` ends on the tile it reached.
    const auto clear = [&](float x, float z) {
        int tile = stanTile;
        if (!stan_->TestLineUnobstructed(&tile, (*src)[0], (*src)[2], x, z, 0x1f, height, always20, 0.0f, 1.0f)) {
            return kNoTile;
        }
        if (stan_->TestVolume(&tile, x, z, width, 0x1f, height, always20) >= 0) {
            return kNoTile;
        }
        return tile;
    };

    if (const int tile = clear((*dst)[0], (*dst)[2]); tile != kNoTile) {
        if (updateLastMoveOk) {
            chr.invalidMove = 0;
            chr.lastMoveOk60 = timing.globalTimer;
        }
        ret = tile;
    } else {
        Vec3 edgeA{};
        Vec3 edgeB{};
        if (stan_->CollisionEdge(&edgeA, &edgeB)) {
            const float deltaX = (*dst)[0] - (*src)[0];
            const float deltaZ = (*dst)[2] - (*src)[2];
            float newX = 0.0f;
            float newZ = 0.0f;
            bool hasProjection = false;
            if (edgeA[0] != edgeB[0] || edgeA[2] != edgeB[2]) {
                hasProjection = true;
                float dirX = edgeB[0] - edgeA[0];
                float dirZ = edgeB[2] - edgeA[2];
                float scale = 1.0f / std::sqrt((dirZ * dirZ) + (dirX * dirX));
                dirX *= scale;
                dirZ *= scale;
                scale = (dirZ * deltaZ) + (deltaX * dirX);
                newX = (dirX * scale) + (*src)[0];
                newZ = (dirZ * scale) + (*src)[2];
            }
            // Whether sliding to (newX, newZ) is clear; when it is, the
            // move ends there.
            const auto slide = [&](int invalidMove) {
                if (const int tile = clear(newX, newZ); tile != kNoTile) {
                    (*dst)[0] = newX;
                    (*dst)[2] = newZ;
                    chr.invalidMove = invalidMove;
                    ret = tile;
                    return true;
                }
                return false;
            };
            // The slide that turns about a corner the circle reaches: at right
            // angles to the line from `src` to the corner.
            const auto slideAroundCorner = [&](const Vec3& corner) {
                float dirX = -(corner[2] - (*src)[2]);
                float dirZ = corner[0] - (*src)[0];
                float scale = 1.0f / std::sqrt((dirZ * dirZ) + (dirX * dirX));
                dirX *= scale;
                dirZ *= scale;
                scale = (dirZ * deltaZ) + (deltaX * dirX);
                newX = (dirX * scale) + (*src)[0];
                newZ = (dirZ * scale) + (*src)[2];
                slide(2);
            };

            if (!(hasProjection && slide(2))) {
                const float widthSquared = width * width;
                float toX = edgeA[0] - (*dst)[0];
                float toZ = edgeA[2] - (*dst)[2];
                if (((toZ * toZ) + (toX * toX)) <= widthSquared) {
                    if (!(edgeA[0] == (*src)[0] && edgeA[2] == (*src)[2])) {
                        slideAroundCorner(edgeA);
                    }
                } else {
                    toX = edgeB[0] - (*dst)[0];
                    toZ = edgeB[2] - (*dst)[2];
                    if (((toZ * toZ) + (toX * toX)) <= widthSquared) {
                        if (!(edgeB[0] == (*src)[0] && edgeB[2] == (*src)[2])) {
                            slideAroundCorner(edgeB);
                        }
                    }
                }
            }
        }
    }

    chr.hidden &= ~ChrHidden::Moving;
    if (ret == kNoTile) {
        chr.invalidMove = 1;
    }
    return ret;
}

void Chrs::DetectRooms(int chrSlot) {
    const Chr& chr = chrs_[static_cast<size_t>(chrSlot)];
    const Prop& prop = (*props_)[chr.prop];
    const Vec3 lower = { prop.position[0] - kRoomBoxHalfWidth, chr.ground - 1.0f,
                         prop.position[2] - kRoomBoxHalfWidth };
    const Vec3 upper = { prop.position[0] + kRoomBoxHalfWidth, prop.position[1] + kRoomBoxHeight,
                         prop.position[2] + kRoomBoxHalfWidth };
    props_->DeregisterRooms(chr.prop);
    props_->UpdateRoomList(chr.prop, lower, upper, kRoomBoxHalfWidth);
    props_->RegisterRooms(chr.prop);
}

void Chrs::ApplyAttribute(const PropDef& def) {
    for (Chr& chr : chrs_) {
        if (chr.inUse && chr.chrnum == def.attributeChrnum) {
            chr.grenadeProb = def.grenadeProb;
            return;
        }
    }
}

const Chr* Chrs::FindByLiteralId(int chrnum) const {
    for (const Chr& chr : chrs_) {
        if (chr.inUse && chr.chrnum == chrnum) {
            return &chr;
        }
    }
    return nullptr;
}

Chr* Chrs::FindByLiteralId(int chrnum) {
    return const_cast<Chr*>(std::as_const(*this).FindByLiteralId(chrnum));
}

void Chrs::CreateBackgroundChrs() {
    background_.clear();
    if (aiLists_ == nullptr) {
        return;
    }
    for (const AiList& list : aiLists_->Local()) {
        if (!IsBackgroundAiListId(list.id)) {
            continue;
        }
        Chr chr;
        chr.chrnum = kBackgroundChrnum;
        chr.aiList = &list;
        chr.aiOffset = 0;
        chr.aiReturnList = -1;
        chr.actionType = ChrAction::Null;
        // The record is copied from an all-zero block, so the fields a spawned
        // character starts from (a free shot and death slot, no vision limit)
        // read zero here.
        chr.padPreset1 = 0;
        chr.chrPreset1 = 0;
        chr.chrSeeShot = 0;
        chr.chrSeeDie = 0;
        chr.hearingScale = 0.0f;
        chr.visionRange = 0.0f;
        chr.chrWidth = 0.0f;
        chr.chrHeight = 0.0f;
        chr.flinchCount = 0;
        background_.push_back(std::move(chr));
    }
}

int Chrs::ResolveId(const Chr& self, int id) {
    // The ids arrive as the unsigned bytes of an AI command.
    if (id == static_cast<uint8_t>(kChrSeeShot)) {
        return self.chrSeeShot;
    }
    if (id == static_cast<uint8_t>(kChrSeeDie)) {
        return self.chrSeeDie;
    }
    if (id == static_cast<uint8_t>(kChrPreset)) {
        return self.chrPreset1;
    }
    if (id == static_cast<uint8_t>(kChrSelf)) {
        return self.chrnum;
    }
    if (id == static_cast<uint8_t>(kChrClone)) {
        return self.chrnum + 0x2710;
    }
    // TODO(https://github.com/liz-baker/frigate/issues/140): CHR_BOND_CINEMA,
    // the player's chrnum while Bond has a third-person model (the intro and
    // exit cutscenes).
    return id;
}

Chr* Chrs::FindById(const Chr& self, int id) {
    id = ResolveId(self, id);
    if (Chr* chr = FindByLiteralId(id)) {
        return chr;
    }
    for (Chr& chr : background_) {
        if (chr.chrnum == id) {
            return &chr;
        }
    }
    return nullptr;
}

int Chrs::ResolvePadId(const Chr& self, int padId) {
    return padId == kPadPreset1 ? self.padPreset1 : padId;
}

const Chr* Chrs::ForDef(size_t def) const {
    return def < byDef_.size() && byDef_[def] >= 0 ? &chrs_[static_cast<size_t>(byDef_[def])] : nullptr;
}

int Chrs::FreeSlots() const {
    return static_cast<int>(std::count_if(chrs_.begin(), chrs_.end(), [](const Chr& chr) { return !chr.inUse; }));
}

bool Chrs::Collider(int prop, int cdTypes, PropCollider* out) const {
    const Prop& record = (*props_)[prop];
    if (record.type != PropType::Chr || record.object < 0 || (cdTypes & CdType::Chrs) == 0) {
        return false;
    }
    const Chr& chr = chrs_[static_cast<size_t>(record.object)];
    // chrUpdateCollisionBounds: a diamond around the chr.
    if (chr.actionType == ChrAction::Die || chr.actionType == ChrAction::Dead ||
        (chr.chrflags & (ChrFlag::Unnamed0x10000 | ChrFlag::Hidden)) != 0 || (chr.hidden & ChrHidden::Moving) != 0) {
        return false;
    }
    const float x = record.position[0];
    const float z = record.position[2];
    const float w = chr.chrWidth;
    out->polygon[0] = { x + w, z };
    out->polygon[1] = { x, z + w };
    out->polygon[2] = { x - w, z };
    out->polygon[3] = { x, z - w };
    out->edges = 4;
    out->bottom = chr.ground;
    out->top = chr.ground + chr.chrHeight;
    out->prop = prop;
    return true;
}

} // namespace Frigate::Level
