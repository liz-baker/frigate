#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frigate::Level {

// The animation lists of the monitors' screens (`monitor/lists`,
// docs/formats/goldeneye-monitor-lists.md), held end to end in the order the
// game keeps them in memory, so a list that runs off its end carries on into
// the next.
class MonitorLists {
  public:
    // Null when the text is malformed.
    static std::shared_ptr<const MonitorLists> Parse(std::string_view text);

    // monitorSetImageByNum: where the list a number picks starts.
    size_t ListFor(int number) const;
    // Where a list starts; the default list if there is none by that name.
    size_t ListNamed(const std::string& name) const;
    // The word at `index`; the end of the lists reads as a command that stops.
    uint32_t Word(size_t index) const {
        return index < words_.size() ? words_[index] : kStop;
    }

  private:
    static constexpr uint32_t kStop = 12;

    std::vector<uint32_t> words_;
    std::unordered_map<std::string, size_t> starts_;
    std::vector<size_t> images_;
    size_t default_ = 0;
};

// MonitorRecord: the state of one monitor screen.
struct MonitorScreen {
    using RandomFn = std::function<uint32_t()>;

    // cmdlist and offset: where in MonitorLists the screen's list starts, and
    // how far into it the screen is.
    size_t list = 0;
    uint16_t offset = 0;
    int16_t pause = -1;
    // tconfig: the screen's image, a number in the monitor image table.
    int32_t image = 0;
    float rot = 0.0f;
    float xscale = 1.0f;
    float xscaleFrac = 0.0f;
    float xscaleInc = 0.0f;
    float xscaleOld = 1.0f;
    float xscaleNew = 1.0f;
    float yscale = 1.0f;
    float yscaleFrac = 0.0f;
    float yscaleInc = 0.0f;
    float yscaleOld = 1.0f;
    float yscaleNew = 1.0f;
    float xmid = 0.5f;
    float xmidFrac = 0.0f;
    float xmidInc = 0.0f;
    float xmidOld = 0.5f;
    float xmidNew = 0.5f;
    float ymid = 0.5f;
    float ymidFrac = 0.0f;
    float ymidInc = 0.0f;
    float ymidOld = 0.5f;
    float ymidNew = 0.5f;
    uint8_t red = 0xFF;
    uint8_t redOld = 0xFF;
    uint8_t redNew = 0xFF;
    uint8_t green = 0xFF;
    uint8_t greenOld = 0xFF;
    uint8_t greenNew = 0xFF;
    uint8_t blue = 0xFF;
    uint8_t blueOld = 0xFF;
    uint8_t blueNew = 0xFF;
    uint8_t alpha = 0xFF;
    uint8_t alphaOld = 0xFF;
    uint8_t alphaNew = 0xFF;
    float colFrac = 1.0f;
    float colInc = 0.0f;

    // g_InitialMonitorAnimController, then monitorSetImageByNum(number): how
    // setupSingleMonitor and setupMultiMonitor make a screen.
    static MonitorScreen Make(const MonitorLists& lists, int number);

    // save_ptr_monitor_ani_code_to_obj_ani_slot: start the list from its top.
    void StartList(size_t start) {
        list = start;
        offset = 0;
    }

    // The state half of process_monitor_animation_microcode: runs the screen's
    // commands until one holds, then moves the scroll, zoom and colour on by
    // `timerDelta`. `clockTimer` is g_ClockTimer.
    void Advance(const MonitorLists& lists, int32_t clockTimer, float timerDelta, const RandomFn& random);
};

// The screens of a monitor object (one for a monitor, four for a multi-monitor),
// made when the object is first drawn.
struct MonitorScreens {
    // The image numbers the setup gave each screen.
    std::array<int, 4> numbers{};
    std::vector<MonitorScreen> screens;
};

} // namespace Frigate::Level
