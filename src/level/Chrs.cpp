#include "level/Chrs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

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

std::vector<int> ResolveNames(const Game::ChrRecords& records, const std::string_view* names, size_t count) {
    std::vector<int> slots;
    for (size_t i = 0; i < count; i++) {
        slots.push_back(records.Slot(names[i]));
    }
    return slots;
}

} // namespace

void Chrs::Reset(Stan* stan, PropList* props, std::shared_ptr<const Game::ChrRecords> records) {
    stan_ = stan;
    props_ = props;
    records_ = std::move(records);
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
    // TODO(https://github.com/liz-baker/frigate/issues/200): makeonebody's model
    // instance for the body and head, which retrieve_header_for_body_and_head
    // returns.

    const int prop = props_->Allocate();
    if (prop < 0) {
        return false;
    }
    const int slot = InitChr(prop, pad.position, tile, atan2f(pad.look[0], pad.look[2]), timing);
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
    chr.aiListId = record.guardAiList;
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

int Chrs::InitChr(int prop, const Vec3& position, int tile, float angle, const ChrTiming& timing) {
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
    // TODO(https://github.com/liz-baker/frigate/issues/200): the model's
    // placement (setsuboffset, setsubroty(angle), the animation speed) and the
    // model back-pointer.
    (void)angle;
    // TODO(https://github.com/liz-baker/frigate/issues/192):
    // set_color_shading_from_tile's nextcol, shadecol and fadealpha.

    // sub_GAME_7F01FC10 with the source and destination both the prop's
    // position.
    UpdateGround(slot, &record.position, &record.position, timing);
    DetectRooms(slot);
    return slot;
}

void Chrs::UpdateGround(int chrSlot, const Vec3* from, Vec3* to, const ChrTiming& timing) {
    Chr& chr = chrs_[static_cast<size_t>(chrSlot)];
    Prop& prop = (*props_)[chr.prop];
    bool moved = false;
    float ground = 0.0f;
    const Vec3* groundPosition = from;

    if (prop.stan != kNoTile) {
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
                // TODO(https://github.com/liz-baker/frigate/issues/200): the
                // model root's rwdata, whose previous position takes its
                // current one.
            }
            // TODO(https://github.com/liz-baker/frigate/issues/166): the fall
            // and ground smoothing of a chr that is not initialising
            // (sub_GAME_7F057D44 and the FALLSPEED_DECAY loop).
            (*to)[1] += chr.manGround - ground;
        } else {
            ground = chr.ground;
        }
    }
    chr.ground = ground;
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
