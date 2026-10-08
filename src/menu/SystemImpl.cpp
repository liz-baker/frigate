#include "menu/SystemImpl.h"

#include <algorithm>

#include "fast/Fast3dWindow.h"
#include "audio/MasterVolume.h"
#include "game/ControlStyleOverride.h"
#include "gfx/DisplayAspect.h"
#include "menu/CVars.h"
#include "menu/SettingsModel.h"
#include "platform/Input.h"
#include "ship/audio/Audio.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/core/Context.h"
#include "ship/window/Window.h"

#ifndef __WIIU__
#include <SDL3/SDL.h>
#endif

#ifdef FRIGATE_VERSION
constexpr const char* kVersion = FRIGATE_VERSION;
#else
constexpr const char* kVersion = "";
#endif

namespace Frigate::ConfigMenu {

namespace {

const char* WindowBackendLabel(int id) {
    switch (id) {
        case Fast::FAST3D_DXGI_DX11:
            return "DirectX 11";
        case Fast::FAST3D_SDL_OPENGL:
            return "OpenGL";
        case Fast::FAST3D_SDL_METAL:
            return "Metal";
        case Fast::FAST3D_GX2:
            return "GX2";
        default:
            return "Unknown";
    }
}

const char* AudioBackendLabel(Ship::AudioBackend backend) {
    switch (backend) {
        case Ship::AudioBackend::WASAPI:
            return "WASAPI";
        case Ship::AudioBackend::SDL:
            return "SDL";
        case Ship::AudioBackend::COREAUDIO:
            return "Core Audio";
        case Ship::AudioBackend::AX:
            return "AX";
        case Ship::AudioBackend::NUL:
            return "Null";
    }
    return "Unknown";
}

// The config strings libultraship's Audio component reads back at startup.
const char* AudioBackendConfigName(Ship::AudioBackend backend) {
    switch (backend) {
        case Ship::AudioBackend::WASAPI:
            return "wasapi";
        case Ship::AudioBackend::SDL:
            return "sdl";
        case Ship::AudioBackend::COREAUDIO:
            return "coreaudio";
        case Ship::AudioBackend::AX:
            return "ax";
        case Ship::AudioBackend::NUL:
            return "null";
    }
    return "";
}

class ContextSystem : public System {
  public:
    explicit ContextSystem(std::shared_ptr<Ship::Context> context)
        : mContext(std::move(context)), mWindow(mContext->GetChildren().GetFirst<Ship::Window>()),
          mConfig(mContext->GetChildren().GetFirst<Ship::Config>()),
          mCvars(mContext->GetChildren().GetFirst<Ship::ConsoleVariable>()),
          mAudio(mContext->GetChildren().GetFirst<Ship::Audio>()) {
    }

    // The shape in effect right now: the setting during live play, and 4:3
    // everywhere else (menus, cutscenes, attract demos) and in Low-Resolution
    // Mode. A platform that can't resize its output always fits the window (a
    // fixed 16:9 there).
    int AspectSetting() const {
#ifdef __WIIU__
        return 0;
#else
        if (!Display::LivePlay() || mCvars->GetInteger(kLowResModeCVar, 0) == 1) {
            return 1;
        }
        return std::clamp(mCvars->GetInteger(kAspectRatioCVar, 0), 0, 2);
#endif
    }

    bool HasWindow() const override {
        return mWindow != nullptr;
    }
    bool CanChangeWindowMode() const override {
#ifdef __WIIU__
        return false;
#else
        return mWindow != nullptr;
#endif
    }
    bool IsFullscreen() override {
        return mWindow != nullptr && mWindow->IsFullscreen();
    }
    void SetFullscreen(bool fullscreen) override {
        if (mWindow != nullptr && mWindow->IsFullscreen() != fullscreen) {
            mWindow->SetFullscreen(fullscreen);
        }
    }
    bool SupportsWindowedFullscreen() const override {
        return mWindow != nullptr && mWindow->SupportsWindowedFullscreen();
    }
    bool CanDisableVsync() const override {
        return mWindow != nullptr && mWindow->CanDisableVerticalSync();
    }
    bool SupportsMsaa() const override {
        return mWindow != nullptr && mWindow->GetWindowBackend() != Fast::FAST3D_GX2;
    }
    void ApplyRenderSettings() override {
        if (mWindow == nullptr || mCvars == nullptr) {
            return;
        }
        mWindow->SetResolutionMultiplier(mCvars->GetFloat(kInternalResolutionCVar, 1.0f));
        mWindow->SetMsaaLevel(static_cast<uint32_t>(std::max(1, mCvars->GetInteger(kMsaaCVar, 1))));
        if (auto fast = std::dynamic_pointer_cast<Fast::Fast3dWindow>(mWindow)) {
            fast->SetTextureFilter(static_cast<Fast::FilteringMode>(mCvars->GetInteger(kTextureFilterCVar, 0)));
        }
        // libultraship pillar/letterboxes to this ratio itself, in black.
        const int aspect = AspectSetting();
        mCvars->SetInteger("gAdvancedResolution.Enabled", aspect != 0 ? 1 : 0);
        mCvars->SetFloat("gAdvancedResolution.AspectRatioX", aspect == 1 ? 4.0f : 16.0f);
        mCvars->SetFloat("gAdvancedResolution.AspectRatioY", aspect == 1 ? 3.0f : 9.0f);
    }
    bool IsLowResolution() override {
        return mCvars != nullptr && mCvars->GetInteger(kLowResModeCVar, 0) == 1;
    }
    float TargetAspectRatio() override {
        switch (AspectSetting()) {
            case 1:
                return 4.0f / 3.0f;
            case 2:
                return 16.0f / 9.0f;
            default:
                return 0.0f;
        }
    }
    std::vector<Choice> RenderBackends() override {
        std::vector<Choice> choices;
        if (mWindow != nullptr) {
            for (const int id : *mWindow->GetAvailableWindowBackends()) {
                choices.push_back({ id, WindowBackendLabel(id) });
            }
        }
        return choices;
    }
    int RenderBackend() override {
        // Window::SetWindowBackend writes the running backend to the config, so
        // this reads back a pending change too.
        return mConfig != nullptr ? mConfig->GetInt("Window.Backend.Id", mWindow->GetWindowBackend()) : 0;
    }
    void SetRenderBackend(int id) override {
        if (mConfig == nullptr) {
            return;
        }
        mConfig->SetInt("Window.Backend.Id", id);
        mConfig->SetString("Window.Backend.Name", WindowBackendLabel(id));
        mConfig->Save();
    }

    void ApplyControlStyle() override {
        if (mCvars != nullptr) {
            Game::ControlStyleOverride() = std::clamp(mCvars->GetInteger(kControlStyleCVar, 0), 0, 8) - 1;
        }
    }

    std::vector<std::string> GamepadNames() override {
        return Platform::Input::GamepadNames(mContext->GetChildren().GetFirst<Ship::ControlDeck>().get());
    }
    void ApplyControllerPorts() override {
        if (mCvars == nullptr) {
            return;
        }
        uint8_t forced = 0;
        for (size_t port = 0; port < Platform::Input::kPortCount; port++) {
            if (mCvars->GetInteger(kPortConnectedCVars[port], 0) != 0) {
                forced |= static_cast<uint8_t>(1 << port);
            }
            Platform::Input::Devices()[port] = std::max(0, mCvars->GetInteger(kPortDeviceCVars[port], 0));
        }
        Platform::Input::ForcedPorts() = forced;
    }
    void SwapControllerPorts(int a, int b) override {
        if (mCvars == nullptr) {
            return;
        }
        ApplyControllerPorts();
        Platform::Input::SwapPorts(Platform::Input::Devices(), a, b);
        for (size_t port = 0; port < Platform::Input::kPortCount; port++) {
            mCvars->SetInteger(kPortDeviceCVars[port], Platform::Input::Devices()[port]);
        }
        mCvars->Save();
    }

    void ApplyAudioSettings() override {
        if (mCvars != nullptr) {
            Audio::SetMasterVolume(mCvars->GetInteger(kMasterVolumeCVar, 100));
        }
    }
    std::vector<Choice> AudioBackends() override {
        std::vector<Choice> choices;
        if (mAudio != nullptr) {
            for (const auto backend : *mAudio->GetAvailableAudioBackends()) {
                choices.push_back({ static_cast<int>(backend), AudioBackendLabel(backend) });
            }
        }
        return choices;
    }
    int AudioBackend() override {
        if (mAudio == nullptr) {
            return 0;
        }
        const std::string saved = mConfig != nullptr ? mConfig->GetString("Window.AudioBackend") : "";
        for (const auto backend : *mAudio->GetAvailableAudioBackends()) {
            if (saved == AudioBackendConfigName(backend)) {
                return static_cast<int>(backend);
            }
        }
        return static_cast<int>(mAudio->GetCurrentAudioBackend());
    }
    void SetAudioBackend(int id) override {
        if (mConfig == nullptr) {
            return;
        }
        mConfig->SetString("Window.AudioBackend", AudioBackendConfigName(static_cast<Ship::AudioBackend>(id)));
        mConfig->Save();
    }

    std::string AppFolder() override {
        return Ship::Context::GetAppDirectoryPath("frigate");
    }
    std::string OpenAppFolderUnavailableReason() override {
#ifdef __WIIU__
        return "There is no file browser on this platform.";
#else
        return {};
#endif
    }
    void OpenAppFolder() override {
#ifndef __WIIU__
        SDL_OpenURL(FileUrlFromPath(AppFolder()).c_str());
#endif
    }
    std::string Version() override {
        return kVersion;
    }

  private:
    std::shared_ptr<Ship::Context> mContext;
    std::shared_ptr<Ship::Window> mWindow;
    std::shared_ptr<Ship::Config> mConfig;
    std::shared_ptr<Ship::ConsoleVariable> mCvars;
    std::shared_ptr<Ship::Audio> mAudio;
};

} // namespace

std::shared_ptr<System> MakeSystem(const std::shared_ptr<Ship::Context>& context) {
    return std::make_shared<ContextSystem>(context);
}

} // namespace Frigate::ConfigMenu
