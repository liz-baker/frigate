#include <cstdio>

#include "menu/CVars.h"
#include "menu/SettingsModel.h"
#include "audio/MasterVolume.h"
#include "menu/Tabs.h"
#include "platform/FrameRate.h"

using namespace Frigate::ConfigMenu;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// A System with a window, a fullscreen toggle and two backends of each kind.
class FakeSystem : public System {
  public:
    bool HasWindow() const override {
        return window;
    }
    bool CanChangeWindowMode() const override {
        return true;
    }
    bool IsFullscreen() override {
        return fullscreen;
    }
    void SetFullscreen(bool value) override {
        fullscreen = value;
    }
    bool SupportsMsaa() const override {
        return msaa;
    }
    void ApplyAudioSettings() override {
        audioApplied++;
    }
    int audioApplied = 0;
    void ApplyRenderSettings() override {
        applied++;
    }
    std::vector<Choice> RenderBackends() override {
        return { { 2, "OpenGL" }, { 1, "DirectX 11" } };
    }
    int RenderBackend() override {
        return renderBackend;
    }
    void SetRenderBackend(int id) override {
        renderBackend = id;
    }
    std::vector<Choice> AudioBackends() override {
        return { { 0, "WASAPI" }, { 1, "SDL" } };
    }
    int AudioBackend() override {
        return audioBackend;
    }
    void SetAudioBackend(int id) override {
        audioBackend = id;
    }
    std::string OpenAppFolderUnavailableReason() override {
        return {};
    }
    void OpenAppFolder() override {
        opened++;
    }
    std::string Version() override {
        return "abc123";
    }

    bool window = true;
    bool fullscreen = false;
    bool msaa = true;
    int applied = 0;
    int renderBackend = 1;
    int audioBackend = 1;
    int opened = 0;
};

Widget* Find(Registry& registry, const char* sidebar, const std::string& name) {
    Sidebar* side = registry.FindTab("Settings")->FindSidebar(sidebar);
    for (auto& widget : side->widgets) {
        if (widget->name == name) {
            return widget.get();
        }
    }
    return nullptr;
}

void TestChoices() {
    const std::vector<Choice> choices = { { 5, "A" }, { 9, "B" } };
    Check(ChoiceLabels(choices) == std::vector<std::string>({ "A", "B" }), "labels follow the choices");
    Check(ChoiceIndex(choices, 9) == 1, "index of a present id");
    Check(ChoiceIndex(choices, 7) == 0, "an absent id selects the first");
    Check(ChoiceId(choices, 1, -1) == 9, "id at an index");
    Check(ChoiceId(choices, 2, -1) == -1 && ChoiceId(choices, -1, -1) == -1, "out-of-range index gives the fallback");
}

void TestFileUrl() {
    Check(FileUrlFromPath("/home/a b/frigate") == "file:///home/a%20b/frigate", "a space is encoded");
    Check(FileUrlFromPath("C:\\Users\\me\\AppData") == "file:///C:/Users/me/AppData", "a Windows path gets slashes");
    Check(FileUrlFromPath("/x/é") == "file:///x/%C3%A9", "UTF-8 bytes are encoded");
    Check(VersionLabel("") == "Version unknown" && VersionLabel("v1") == "Version v1", "version line");
}

void TestRegistered() {
    auto system = std::make_shared<FakeSystem>();
    Registry registry;
    RegisterTabs(registry, system);
    Tab* tab = registry.FindTab("Settings");
    Check(tab != nullptr && tab->sidebars.size() == 3, "Settings has General, Audio and Graphics");

    MemoryValueStore store;
    registry.Refresh();

    Widget* mode = Find(registry, "Graphics", "Window Mode");
    Check(mode != nullptr && !mode->disabled && mode->intValue == 0, "window mode follows the window");
    mode->intValue = 1;
    mode->callback();
    Check(system->fullscreen, "choosing fullscreen reaches the system");

    Widget* internal = Find(registry, "Graphics", "Internal Resolution");
    internal->SetFloat(store, 2.0f);
    Check(store.GetFloat(kInternalResolutionCVar, 0.0f) == 2.0f && system->applied == 1,
          "resolution is stored and applied");

    Widget* renderer = Find(registry, "Graphics", std::string("Renderer") + kRestartSuffix);
    Check(renderer->intValue == 1 && !renderer->disabled, "renderer shows the pending backend");
    renderer->intValue = 0;
    renderer->callback();
    Check(system->renderBackend == 2, "renderer choice maps back to the id");

    Widget* audio = Find(registry, "Audio", std::string("Audio Backend") + kRestartSuffix);
    audio->intValue = 0;
    audio->callback();
    Check(system->audioBackend == 0, "audio backend choice maps back to the id");

    Widget* folder = Find(registry, "General", "Open App Files Folder");
    Check(!folder->disabled, "the folder button is enabled when it can work");
    folder->callback();
    Check(system->opened == 1, "the folder button opens the folder");

    Check(Find(registry, "General", "Version abc123") != nullptr, "About shows the version");
}

void TestVolumeAndFps() {
    auto system = std::make_shared<FakeSystem>();
    Registry registry;
    RegisterTabs(registry, system);
    registry.Refresh();
    MemoryValueStore store;

    Widget* volume = Find(registry, "Audio", "Master Volume");
    Check(volume != nullptr && !volume->disabled && volume->cvar == kMasterVolumeCVar,
          "master volume is a live CVar slider");
    volume->SetInt(store, 40);
    Check(store.GetInt(kMasterVolumeCVar, 100) == 40 && system->audioApplied == 1, "volume is stored and applied");

    int16_t samples[4] = { 1000, -1000, 32767, -32768 };
    Frigate::Audio::ApplyVolume(samples, 4, 50);
    Check(samples[0] == 500 && samples[1] == -500 && samples[2] == 16383 && samples[3] == -16384,
          "volume scales samples");
    int16_t same[1] = { 123 };
    Frigate::Audio::ApplyVolume(same, 1, 100);
    Check(same[0] == 123, "full volume is untouched");
    Frigate::Audio::SetMasterVolume(250);
    Check(Frigate::Audio::MasterVolumePercent() == 100, "volume is clamped");

    Widget* limit = Find(registry, "Graphics", "FPS Limit");
    Check(limit != nullptr && !limit->disabled && limit->cvar == Frigate::Platform::kFpsLimitCVar,
          "FPS limit is a live CVar slider");
    Check(Frigate::Platform::LimitedFps(60, 30) == 30 && Frigate::Platform::LimitedFps(30, 60) == 30 &&
              Frigate::Platform::LimitedFps(60, 0) == 60,
          "the limit only lowers the wanted rate");
}

void TestTheme() {
    Registry registry;
    RegisterTabs(registry, std::make_shared<System>());
    registry.Refresh();
    Widget* theme = Find(registry, "General", "Menu Theme");
    Check(theme != nullptr && theme->cvar == kThemeCVar && theme->options.size() == kMenuThemes.size(),
          "the theme combo lists every theme");
}

void TestUnavailable() {
    auto system = std::make_shared<FakeSystem>();
    system->msaa = false;
    system->window = false;
    Registry registry;
    RegisterTabs(registry, system);
    registry.Refresh();

    Widget* msaa = Find(registry, "Graphics", "Anti-Aliasing (MSAA)");
    Check(msaa->disabled && !msaa->disabledReason.empty(), "MSAA is disabled with a reason");
    Widget* mode = Find(registry, "Graphics", "Window Mode");
    Check(mode->disabled && !mode->disabledReason.empty(), "window mode is disabled without a window");

    Registry bare;
    RegisterTabs(bare, std::make_shared<System>());
    bare.Refresh();
    Widget* folder = Find(bare, "General", "Open App Files Folder");
    Check(folder->disabled && !folder->disabledReason.empty(), "no host: the folder button says why");
    Widget* audio = Find(bare, "Audio", std::string("Audio Backend") + kRestartSuffix);
    Check(audio->disabled, "no backends to choose between disables the combo");
}

} // namespace

int main() {
    TestChoices();
    TestFileUrl();
    TestRegistered();
    TestVolumeAndFps();
    TestTheme();
    TestUnavailable();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    return 0;
}
