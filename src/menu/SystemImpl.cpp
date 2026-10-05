#include "menu/SystemImpl.h"

#include <algorithm>

#include "fast/Fast3dWindow.h"
#include "audio/MasterVolume.h"
#include "menu/CVars.h"
#include "menu/SettingsModel.h"
#include "ship/audio/Audio.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
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
