#include "level/StageSetup.h"

#include <cstring>

namespace Frigate::Level {

namespace {

constexpr size_t kHeaderWords = 10;
constexpr size_t kPadSize = 0x2C;
constexpr size_t kBoundPadSize = 0x44;
constexpr float kFixedPointOne = 65536.0f;

// Bounds-checked big-endian reads. A read past the end sets `failed`, after
// which every read returns 0.
class Reader {
  public:
    explicit Reader(const std::vector<uint8_t>& file) : file_(file) {
    }

    uint32_t U32(size_t at) {
        if (at > file_.size() || file_.size() - at < 4) {
            failed_ = true;
            return 0;
        }
        return (uint32_t{ file_[at] } << 24) | (uint32_t{ file_[at + 1] } << 16) | (uint32_t{ file_[at + 2] } << 8) |
               uint32_t{ file_[at + 3] };
    }
    int32_t S32(size_t at) {
        return static_cast<int32_t>(U32(at));
    }
    uint16_t U16(size_t at) {
        return static_cast<uint16_t>(U32(at) >> 16);
    }
    float F32(size_t at) {
        const uint32_t bits = U32(at);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    Vec3 Vector(size_t at) {
        return { F32(at), F32(at + 4), F32(at + 8) };
    }
    // The C string at `at`.
    std::string String(size_t at) {
        std::string text;
        for (; at < file_.size(); at++) {
            if (file_[at] == 0) {
                return text;
            }
            text.push_back(static_cast<char>(file_[at]));
        }
        failed_ = true;
        return text;
    }
    bool Failed() const {
        return failed_;
    }

  private:
    const std::vector<uint8_t>& file_;
    bool failed_ = false;
};

Vec3 Scaled(const Vec3& value, float scale) {
    return { value[0] * scale, value[1] * scale, value[2] * scale };
}

// The pad's fields at `at`; the position is scaled by `roomScale`.
Pad ReadPad(Reader& in, size_t at, float roomScale) {
    Pad pad;
    pad.position = Scaled(in.Vector(at), roomScale);
    pad.up = in.Vector(at + 0x0C);
    pad.look = in.Vector(at + 0x18);
    pad.tileName = in.String(in.U32(at + 0x24));
    return pad;
}

std::vector<std::string> ReadNames(Reader& in, uint32_t list) {
    std::vector<std::string> names;
    if (list == 0) {
        return names;
    }
    for (size_t at = list; !in.Failed(); at += 4) {
        const uint32_t offset = in.U32(at);
        if (offset == 0) {
            break;
        }
        names.push_back(in.String(offset));
    }
    return names;
}

// bondviewLoadSetupIntroSection's walk, which also converts the fixed-point
// fields the records store.
std::vector<IntroRecord> ReadIntro(Reader& in, size_t at) {
    std::vector<IntroRecord> records;
    while (!in.Failed()) {
        const auto type = static_cast<IntroType>(in.S32(at));
        if (type == IntroType::End) {
            break;
        }
        switch (type) {
            case IntroType::Spawn:
                records.push_back(IntroSpawn{ in.S32(at + 4), in.S32(at + 8) });
                at += 0x0C;
                break;
            case IntroType::Item:
                records.push_back(IntroItem{ in.S32(at + 4), in.S32(at + 8), in.S32(at + 12) });
                at += 0x10;
                break;
            case IntroType::Ammo:
                records.push_back(IntroAmmo{ in.S32(at + 4), in.S32(at + 8), in.S32(at + 12) });
                at += 0x10;
                break;
            case IntroType::Swirl:
                records.push_back(IntroSwirl{
                    in.S32(at + 4),
                    { static_cast<float>(in.S32(at + 8)) / kFixedPointOne,
                      static_cast<float>(in.S32(at + 12)) / kFixedPointOne,
                      static_cast<float>(in.S32(at + 16)) / kFixedPointOne },
                    static_cast<float>(in.S32(at + 20)) / kFixedPointOne,
                    static_cast<float>(in.S32(at + 24)) / kFixedPointOne,
                    in.S32(at + 28),
                });
                at += 0x20;
                break;
            case IntroType::Anim:
                records.push_back(IntroAnim{ in.S32(at + 4) });
                at += 0x08;
                break;
            case IntroType::Cuff:
                records.push_back(IntroCuff{ in.S32(at + 4) });
                at += 0x08;
                break;
            case IntroType::Camera:
                records.push_back(IntroCamera{
                    { static_cast<float>(in.S32(at + 4)) / 100.0f, static_cast<float>(in.S32(at + 8)) / 100.0f,
                      static_cast<float>(in.S32(at + 12)) / 100.0f },
                    static_cast<float>(in.S32(at + 16)) / kFixedPointOne,
                    static_cast<float>(in.S32(at + 20)) / kFixedPointOne,
                    in.S32(at + 24),
                    { in.U16(at + 28), in.U16(at + 30) },
                    in.S32(at + 32),
                });
                at += 0x28;
                break;
            case IntroType::Watch:
                records.push_back(IntroWatch{ in.S32(at + 4), in.S32(at + 8) });
                at += 0x0C;
                break;
            case IntroType::Credits:
                records.push_back(IntroCredits{ in.U32(at + 4) });
                at += 0x08;
                break;
            default:
                // An unknown type is skipped as a bare type word.
                at += 4;
                break;
        }
    }
    return records;
}

} // namespace

std::optional<StageSetup> StageSetup::Parse(const std::vector<uint8_t>& file, float roomScale, std::string* error) {
    Reader in(file);
    StageSetup setup;
    for (size_t i = 0; i < kHeaderWords; i++) {
        setup.header_[i] = in.U32(i * 4);
    }
    const uint32_t padsAt = setup.header_[6];
    const uint32_t boundPadsAt = setup.header_[7];

    for (size_t at = padsAt; !in.Failed() && in.U32(at + 0x24) != 0; at += kPadSize) {
        setup.pads_.push_back(ReadPad(in, at, roomScale));
    }
    for (size_t at = boundPadsAt; !in.Failed() && in.U32(at + 0x24) != 0; at += kBoundPadSize) {
        BoundPad bound;
        bound.pad = ReadPad(in, at, roomScale);
        for (size_t i = 0; i < bound.box.size(); i++) {
            bound.box[i] = in.F32(at + 0x2C + i * 4) * roomScale;
        }
        setup.boundPads_.push_back(std::move(bound));
    }
    setup.padNames_ = ReadNames(in, setup.header_[8]);
    setup.boundPadNames_ = ReadNames(in, setup.header_[9]);
    setup.intro_ = ReadIntro(in, setup.header_[2]);

    if (in.Failed()) {
        if (error != nullptr) {
            *error = "setup file is truncated";
        }
        return std::nullopt;
    }
    return setup;
}

} // namespace Frigate::Level
