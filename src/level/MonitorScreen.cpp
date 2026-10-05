#include "level/MonitorScreen.h"

#include "level/TextTable.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

// The commands of a list (TVCMD).
enum Command : uint32_t {
    StopScroll,
    ScrollRelativeX,
    ScrollRelativeY,
    ScrollAbsoluteX,
    ScrollAbsoluteY,
    ScaleAbsoluteX,
    ScaleAbsoluteY,
    SetTexture,
    Pause,
    SetList,
    RandomSetList,
    Restart,
    Yield,
    SetColour,
    RotateAbsolute,
    RotateRelative,
};

constexpr float kTau = 6.2831855f;
// M_U16_MAX_VALUE_F: a full turn in a list's angles.
constexpr float kFullTurn = 65536.0f;
constexpr float kFixedUnit = 1.0f / 1024.0f;

// A list that never holds would hang the game; a damaged one stops here.
constexpr int kMaxCommandsPerStep = 1 << 16;

} // namespace

std::shared_ptr<const MonitorLists> MonitorLists::Parse(std::string_view text) {
    struct Line {
        std::string name;
        std::vector<std::string_view> words;
    };
    std::vector<Line> lines;
    std::vector<std::pair<int, std::string>> images;
    std::string defaultName;

    for (const std::string_view line : Split(text, '\n')) {
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string_view> fields = Split(line, '\t');
        if (fields[0] == "list" && fields.size() >= 3) {
            Line entry;
            entry.name = std::string(fields[1]);
            entry.words.assign(fields.begin() + 2, fields.end());
            lines.push_back(std::move(entry));
        } else if (fields[0] == "default" && fields.size() == 2) {
            defaultName = std::string(fields[1]);
        } else if (fields[0] == "image" && fields.size() == 3) {
            int number = 0;
            if (!ParseNumber(fields[1], number) || number < 0) {
                return nullptr;
            }
            images.emplace_back(number, std::string(fields[2]));
        } else {
            return nullptr;
        }
    }
    if (lines.empty()) {
        return nullptr;
    }

    auto result = std::make_shared<MonitorLists>();
    size_t total = 0;
    for (const Line& line : lines) {
        result->starts_.emplace(line.name, total);
        total += line.words.size();
    }
    result->words_.reserve(total);
    for (const Line& line : lines) {
        for (const std::string_view word : line.words) {
            uint32_t value = 0;
            if (!word.empty() && word[0] == '@') {
                const auto found = result->starts_.find(std::string(word.substr(1)));
                if (found == result->starts_.end()) {
                    return nullptr;
                }
                value = static_cast<uint32_t>(found->second);
            } else if (!ParseNumber(word, value)) {
                return nullptr;
            }
            result->words_.push_back(value);
        }
    }

    const auto defaultStart = result->starts_.find(defaultName);
    if (defaultStart == result->starts_.end()) {
        return nullptr;
    }
    result->default_ = defaultStart->second;
    for (const auto& [number, name] : images) {
        const auto start = result->starts_.find(name);
        if (start == result->starts_.end()) {
            return nullptr;
        }
        if (result->images_.size() <= static_cast<size_t>(number)) {
            result->images_.resize(static_cast<size_t>(number) + 1, result->default_);
        }
        result->images_[static_cast<size_t>(number)] = start->second;
    }
    return result;
}

size_t MonitorLists::ListFor(int number) const {
    if (number < 0 || static_cast<size_t>(number) >= images_.size()) {
        return default_;
    }
    return images_[static_cast<size_t>(number)];
}

size_t MonitorLists::ListNamed(const std::string& name) const {
    const auto found = starts_.find(name);
    return found != starts_.end() ? found->second : default_;
}

MonitorScreen MonitorScreen::Make(const MonitorLists& lists, int number) {
    MonitorScreen screen;
    screen.StartList(lists.ListFor(number));
    return screen;
}

void MonitorScreen::Advance(const MonitorLists& lists, int32_t clockTimer, float timerDelta, const RandomFn& random) {
    bool yielding = false;
    for (int commands = 0; !yielding && commands < kMaxCommandsPerStep; commands++) {
        const size_t at = list + offset;
        const uint32_t type = lists.Word(at);
        const int32_t time = static_cast<int32_t>(lists.Word(at + 1));
        const uint32_t arg2 = lists.Word(at + 2);

        switch (type) {
            case StopScroll:
                xmidInc = 0.0f;
                ymidInc = 0.0f;
                offset++;
                break;
            case ScrollRelativeX:
                xmidFrac = 0.0f;
                xmidInc = 1.0f / static_cast<float>(arg2);
                xmidOld = xmid;
                xmidNew = xmid + static_cast<float>(time) * kFixedUnit;
                offset += 3;
                break;
            case ScrollRelativeY:
                ymidFrac = 0.0f;
                ymidInc = 1.0f / static_cast<float>(arg2);
                ymidOld = ymid;
                ymidNew = ymid + static_cast<float>(time) * kFixedUnit;
                offset += 3;
                break;
            case ScrollAbsoluteX:
                xmidFrac = 0.0f;
                xmidInc = 1.0f / static_cast<float>(arg2);
                xmidOld = xmid;
                xmidNew = static_cast<float>(time) * kFixedUnit;
                offset += 3;
                break;
            case ScrollAbsoluteY:
                ymidFrac = 0.0f;
                ymidInc = 1.0f / static_cast<float>(arg2);
                ymidOld = ymid;
                ymidNew = static_cast<float>(time) * kFixedUnit;
                offset += 3;
                break;
            case ScaleAbsoluteX:
                xscaleFrac = 0.0f;
                xscaleInc = 1.0f / static_cast<float>(arg2);
                xscaleOld = xscale;
                xscaleNew = static_cast<float>(time) * kFixedUnit;
                offset += 3;
                break;
            case ScaleAbsoluteY:
                yscaleFrac = 0.0f;
                yscaleInc = 1.0f / static_cast<float>(arg2);
                yscaleOld = yscale;
                yscaleNew = static_cast<float>(time) * kFixedUnit;
                offset += 3;
                break;
            case SetTexture:
                image = time;
                imageOffset = -1;
                offset += 2;
                break;
            case Pause:
                if (pause >= 0) {
                    pause = static_cast<int16_t>(pause - clockTimer);
                    if (pause >= 0) {
                        yielding = true;
                    } else {
                        offset += 2;
                    }
                } else {
                    yielding = true;
                    pause = static_cast<int16_t>(time);
                }
                break;
            case SetList:
                StartList(static_cast<size_t>(time));
                break;
            case RandomSetList:
                if ((random() >> 16) < arg2) {
                    StartList(static_cast<size_t>(time));
                } else {
                    offset += 3;
                }
                break;
            case Restart:
                offset = 0;
                break;
            case SetColour:
                colFrac = 0.0f;
                colInc = 1.0f / static_cast<float>(arg2);
                redOld = red;
                redNew = static_cast<uint8_t>((static_cast<uint32_t>(time) >> 24) & 0xFF);
                greenOld = green;
                greenNew = static_cast<uint8_t>((static_cast<uint32_t>(time) >> 16) & 0xFF);
                blueOld = blue;
                blueNew = static_cast<uint8_t>((static_cast<uint32_t>(time) >> 8) & 0xFF);
                alphaOld = alpha;
                alphaNew = static_cast<uint8_t>(static_cast<uint32_t>(time) & 0xFF);
                offset += 3;
                break;
            case RotateAbsolute:
                rot = static_cast<float>(time) * kTau / kFullTurn;
                offset += 2;
                break;
            case RotateRelative:
                rot += timerDelta * static_cast<float>(time) * kTau / kFullTurn;
                if (rot >= kTau) {
                    rot -= kTau;
                }
                if (rot < 0.0f) {
                    rot += kTau;
                }
                offset += 2;
                break;
            case Yield:
            default:
                yielding = true;
                break;
        }
    }

    if (xscaleInc > 0.0f) {
        xscaleFrac += xscaleInc * timerDelta;
        if (xscaleFrac < 1.0f) {
            xscale = xscaleOld + (xscaleNew - xscaleOld) * xscaleFrac;
        } else {
            xscaleFrac = 1.0f;
            xscaleInc = 0.0f;
            xscale = xscaleNew;
        }
    }
    if (yscaleInc > 0.0f) {
        yscaleFrac += yscaleInc * timerDelta;
        if (yscaleFrac < 1.0f) {
            yscale = yscaleOld + (yscaleNew - yscaleOld) * yscaleFrac;
        } else {
            yscaleFrac = 1.0f;
            yscaleInc = 0.0f;
            yscale = yscaleNew;
        }
    }
    if (xmidInc > 0.0f) {
        xmidFrac += xmidInc * timerDelta;
        if (xmidFrac < 1.0f) {
            xmid = xmidOld + (xmidNew - xmidOld) * xmidFrac;
        } else {
            xmidFrac = 1.0f;
            xmidInc = 0.0f;
            xmid = xmidNew;
        }
    }
    if (ymidInc > 0.0f) {
        ymidFrac += ymidInc * timerDelta;
        if (ymidFrac < 1.0f) {
            ymid = ymidOld + (ymidNew - ymidOld) * ymidFrac;
        } else {
            ymidFrac = 1.0f;
            ymidInc = 0.0f;
            ymid = ymidNew;
        }
    }
    if (colInc > 0.0f) {
        colFrac += colInc * timerDelta;
        if (colFrac < 1.0f) {
            red = static_cast<uint8_t>(redOld + static_cast<int32_t>((redNew - redOld) * colFrac));
            green = static_cast<uint8_t>(greenOld + static_cast<int32_t>((greenNew - greenOld) * colFrac));
            blue = static_cast<uint8_t>(blueOld + static_cast<int32_t>((blueNew - blueOld) * colFrac));
            alpha = static_cast<uint8_t>(alphaOld + static_cast<int32_t>((alphaNew - alphaOld) * colFrac));
        } else {
            colFrac = 1.0f;
            colInc = 0.0f;
            red = redNew;
            green = greenNew;
            blue = blueNew;
            alpha = alphaNew;
        }
    }
}

MonitorScreen::Quad MonitorScreen::MakeQuad(int width, int height) const {
    // The image is sampled about (xmid, ymid), a screen's width and height of it
    // across, turned by `rot` about the middle.
    float xfrac1 = xscale / 2.0f;
    float yfrac1 = yscale / 2.0f;
    float xfrac2 = xfrac1;
    float yfrac2 = yfrac1;

    if (rot != 0.0f) {
        constexpr float kRotateScale = 1.4141999f;
        float cosrot = GameCosf(rot);
        cosrot *= kRotateScale;
        float sinrot = GameSinf(rot);
        xfrac1 *= cosrot;
        sinrot *= kRotateScale;
        yfrac1 *= sinrot;
        xfrac2 *= sinrot;
        yfrac2 *= cosrot;
    }

    // Stored as the game stores a float in a short.
    const auto fixed = [](float value) { return static_cast<int16_t>(static_cast<int32_t>(value)); };
    const auto w = static_cast<float>(width);
    const auto h = static_cast<float>(height);
    Quad quad;
    quad.s = { fixed(w * (xmid + xfrac1) * 32.0f), fixed(w * (xmid - xfrac2) * 32.0f),
               fixed(w * (xmid - xfrac1) * 32.0f), fixed(w * (xmid + xfrac2) * 32.0f) };
    quad.t = { fixed(h * (ymid + yfrac1) * 32.0f), fixed(h * (ymid + yfrac2) * 32.0f),
               fixed(h * (ymid - yfrac1) * 32.0f), fixed(h * (ymid - yfrac2) * 32.0f) };
    quad.rgba = { red, green, blue, alpha };
    return quad;
}

} // namespace Frigate::Level
