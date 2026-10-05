#pragma once

// What the Settings tab needs from the window, the audio output and the host
// OS. The model library has no libultraship dependency, so the tab talks to
// this interface; SystemImpl.cpp backs it with the real Context. The defaults
// describe a system with none of it, which is what the unit tests get.

#include <string>
#include <vector>

namespace Frigate::ConfigMenu {

// A selectable option backed by a library identifier.
struct Choice {
    int id = 0;
    std::string label;
};

class System {
  public:
    virtual ~System() = default;

    // Window and renderer.
    virtual bool HasWindow() const {
        return false;
    }
    virtual bool CanChangeWindowMode() const {
        return false;
    }
    virtual bool IsFullscreen() {
        return false;
    }
    virtual void SetFullscreen(bool) {
    }
    virtual bool SupportsWindowedFullscreen() const {
        return false;
    }
    virtual bool CanDisableVsync() const {
        return false;
    }
    virtual bool SupportsMsaa() const {
        return false;
    }
    // Pushes the internal resolution, MSAA and texture filter CVars to the
    // renderer, so a change shows up without a restart.
    virtual void ApplyRenderSettings() {
    }
    virtual std::vector<Choice> RenderBackends() {
        return {};
    }
    // The backend the next launch will use (not necessarily the running one).
    virtual int RenderBackend() {
        return 0;
    }
    virtual void SetRenderBackend(int) {
    }

    // Audio output.
    virtual std::vector<Choice> AudioBackends() {
        return {};
    }
    // The backend the next launch will use.
    virtual int AudioBackend() {
        return 0;
    }
    virtual void SetAudioBackend(int) {
    }

    // Pushes the master volume CVar to the audio output.
    virtual void ApplyAudioSettings() {
    }

    // Host.
    virtual std::string AppFolder() {
        return {};
    }
    // Empty when opening the folder works here, otherwise why it doesn't.
    virtual std::string OpenAppFolderUnavailableReason() {
        return "Not available in this build.";
    }
    virtual void OpenAppFolder() {
    }
    // Build identification for the About column; empty when unknown.
    virtual std::string Version() {
        return {};
    }
};

} // namespace Frigate::ConfigMenu
