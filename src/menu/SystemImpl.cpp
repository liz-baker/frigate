#include "menu/SystemImpl.h"

#include <algorithm>

#include "fast/Fast3dWindow.h"
#include "audio/MasterVolume.h"
#include "gfx/DisplayAspect.h"
#include "menu/CVars.h"
#include "menu/SettingsModel.h"
#include "platform/BindingsApply.h"
#include "platform/Input.h"
#include "ship/audio/Audio.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/core/Context.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"

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

    Platform::Input::AttachedDevices Attached() override {
        return Platform::Input::AttachedDevicesOf(mContext->GetChildren().GetFirst<Ship::ControlDeck>().get());
    }
    Platform::Input::PortDevice SelectedDevice(int port) override {
        if (mCvars == nullptr) {
            return {};
        }
        return Platform::Input::ParseDeviceKey(mCvars->GetString(kPortDeviceCVars[static_cast<size_t>(port)], ""));
    }
    void SelectDevice(int port, const Platform::Input::PortDevice& device) override {
        if (mCvars == nullptr) {
            return;
        }
        mCvars->SetString(kPortDeviceCVars[static_cast<size_t>(port)], Platform::Input::DeviceKey(device).c_str());
        Platform::Input::SetDeviceChosen();
        if (mWindow != nullptr && mWindow->GetGui() != nullptr) {
            mWindow->GetGui()->SaveConsoleVariablesNextFrame();
        }
        ApplyControllerPorts();
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
        }
        Platform::Input::ForcedPorts() = forced;
        for (int port = 0; port < Platform::Input::kPortCount; port++) {
            Platform::Input::Devices()[static_cast<size_t>(port)] = SelectedDevice(port);
        }
    }
    Ship::ControlDeck* Deck() {
        return mContext->GetChildren().GetFirst<Ship::ControlDeck>().get();
    }
    void SetDefaultBindings() override {
        Platform::Input::SetDefaultBindings(Deck());
        Platform::Input::Bindings().ResetAll();
        BindingsChanged();
    }
    void BindingsChanged() override {
        Platform::Input::SaveBindings();
    }
    Platform::Input::FaceLayout PortFaceLayout(int port) override {
        return Platform::Input::FaceLayoutOf(Deck(), SelectedDevice(port));
    }
    std::vector<Platform::Input::RawInput> PollRawInputs(bool mouseOverUi) override {
        return Platform::Input::PollRawInputs(Deck(), mouseOverUi);
    }

    bool PortConnected(int port) override {
        return mCvars != nullptr && mCvars->GetInteger(kPortConnectedCVars[static_cast<size_t>(port)], 0) != 0;
    }
    void SetPortConnected(int port, bool connected) override {
        if (mCvars == nullptr) {
            return;
        }
        mCvars->SetInteger(kPortConnectedCVars[static_cast<size_t>(port)], connected ? 1 : 0);
        if (mWindow != nullptr && mWindow->GetGui() != nullptr) {
            mWindow->GetGui()->SaveConsoleVariablesNextFrame();
        }
        ApplyControllerPorts();
    }

    IntSetting MouseSensitivity(int port) override {
        return SensitivityOf(kPortMouseSensitivityCVars, port);
    }
    void SetMouseSensitivity(int port, int percent) override {
        SetSensitivity(kPortMouseSensitivityCVars, port, percent);
    }
    IntSetting LookSensitivity(int port) override {
        return SensitivityOf(kPortLookSensitivityCVars, port);
    }
    void SetLookSensitivity(int port, int percent) override {
        SetSensitivity(kPortLookSensitivityCVars, port, percent);
    }

    bool CrouchToggle(int port) override {
        return mCvars != nullptr && mCvars->GetInteger(kPortCrouchToggleCVars[static_cast<size_t>(port)], 0) != 0;
    }
    void SetCrouchToggle(int port, bool toggle) override {
        if (mCvars == nullptr) {
            return;
        }
        mCvars->SetInteger(kPortCrouchToggleCVars[static_cast<size_t>(port)], toggle ? 1 : 0);
        if (mWindow != nullptr && mWindow->GetGui() != nullptr) {
            mWindow->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }

    std::shared_ptr<Ship::Controller> PortController(int port) {
        auto* deck = Deck();
        return deck != nullptr ? deck->GetControllerByPort(static_cast<uint8_t>(port)) : nullptr;
    }
    std::shared_ptr<Ship::ControllerStick> StickOf(int port, Stick stick) {
        const auto controller = PortController(port);
        if (controller == nullptr) {
            return nullptr;
        }
        return stick == Stick::Left ? controller->GetLeftStick() : controller->GetRightStick();
    }
    ControllerFeatures Features(int port) override {
        ControllerFeatures features;
#ifdef __WIIU__
        // There, rumble can't be added by pressing a button and there is no LED or gyro mapping.
        features.rumble = !RumbleDevices(port).empty();
#else
        features.rumble = true;
        features.rumbleAdd = true;
        features.led = true;
        features.gyro = true;
#endif
        return features;
    }
    IntSetting StickValue(int port, Stick stick, StickSetting setting) override {
        const auto controllerStick = StickOf(port, stick);
        IntSetting value;
        switch (setting) {
            case StickSetting::Sensitivity:
                value = { 0, DEFAULT_STICK_SENSITIVITY_PERCENTAGE, 0, 200 };
                if (controllerStick) {
                    value.value = controllerStick->GetSensitivityPercentage();
                }
                break;
            case StickSetting::Deadzone:
                value = { 0, DEFAULT_STICK_DEADZONE_PERCENTAGE, 0, 100 };
                if (controllerStick) {
                    value.value = controllerStick->GetDeadzonePercentage();
                }
                break;
            case StickSetting::NotchSnap:
                value = { 0, DEFAULT_NOTCH_SNAP_ANGLE, 0, 45 };
                if (controllerStick) {
                    value.value = controllerStick->GetNotchSnapAngle();
                }
                break;
        }
        return value;
    }
    void SetStickValue(int port, Stick stick, StickSetting setting, int value) override {
        const auto controllerStick = StickOf(port, stick);
        if (controllerStick == nullptr) {
            return;
        }
        const IntSetting range = StickValue(port, stick, setting);
        const auto clamped = static_cast<uint8_t>(std::clamp(value, range.min, range.max));
        switch (setting) {
            case StickSetting::Sensitivity:
                controllerStick->SetSensitivity(clamped);
                break;
            case StickSetting::Deadzone:
                controllerStick->SetDeadzone(clamped);
                break;
            case StickSetting::NotchSnap:
                controllerStick->SetNotchSnapAngle(clamped);
                break;
        }
    }
    void ResetStickValue(int port, Stick stick, StickSetting setting) override {
        const auto controllerStick = StickOf(port, stick);
        if (controllerStick == nullptr) {
            return;
        }
        switch (setting) {
            case StickSetting::Sensitivity:
                controllerStick->ResetSensitivityToDefault();
                break;
            case StickSetting::Deadzone:
                controllerStick->ResetDeadzoneToDefault();
                break;
            case StickSetting::NotchSnap:
                controllerStick->ResetNotchSnapAngleToDefault();
                break;
        }
    }

    std::shared_ptr<Ship::ControllerRumbleMapping> RumbleOf(int port, const std::string& id) {
        const auto controller = PortController(port);
        if (controller == nullptr) {
            return nullptr;
        }
        const auto all = controller->GetRumble()->GetAllRumbleMappings();
        const auto found = all.find(id);
        return found != all.end() ? found->second : nullptr;
    }
    std::vector<RumbleDevice> RumbleDevices(int port) override {
        std::vector<RumbleDevice> devices;
        if (const auto controller = PortController(port)) {
            for (const auto& [id, mapping] : controller->GetRumble()->GetAllRumbleMappings()) {
                RumbleDevice device;
                device.id = id;
                device.name = mapping->GetPhysicalDeviceName();
                device.smallMotor = { mapping->GetHighFrequencyIntensityPercentage(),
                                      DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE, 0, 100 };
                device.largeMotor = { mapping->GetLowFrequencyIntensityPercentage(),
                                      DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE, 0, 100 };
                devices.push_back(std::move(device));
            }
            // The map's order isn't stable, which would shuffle the rows.
            std::sort(devices.begin(), devices.end(),
                      [](const RumbleDevice& a, const RumbleDevice& b) { return a.id < b.id; });
        }
        return devices;
    }
    void SetRumbleIntensity(int port, const std::string& id, bool large, int value) override {
        if (const auto mapping = RumbleOf(port, id)) {
            const auto clamped = static_cast<uint8_t>(std::clamp(value, 0, 100));
            large ? mapping->SetLowFrequencyIntensity(clamped) : mapping->SetHighFrequencyIntensity(clamped);
            mapping->SaveToConfig();
        }
    }
    void ResetRumbleIntensity(int port, const std::string& id, bool large) override {
        if (const auto mapping = RumbleOf(port, id)) {
            large ? mapping->ResetLowFrequencyIntensityToDefault() : mapping->ResetHighFrequencyIntensityToDefault();
        }
    }
    void TestRumble(int port, const std::string& id, bool on) override {
        if (const auto mapping = RumbleOf(port, id)) {
            on ? mapping->StartRumble() : mapping->StopRumble();
        }
    }
    void RemoveRumbleDevice(int port, const std::string& id) override {
        if (const auto controller = PortController(port)) {
            controller->GetRumble()->ClearRumbleMapping(id);
        }
    }
    bool AddRumbleDeviceFromPress(int port) override {
        const auto controller = PortController(port);
        return controller != nullptr && controller->GetRumble()->AddRumbleMappingFromRawPress();
    }

    std::vector<LedDevice> LedDevices(int port) override {
        std::vector<LedDevice> devices;
        if (const auto controller = PortController(port)) {
            for (const auto& [id, mapping] : controller->GetLED()->GetAllLEDMappings()) {
                const Color_RGB8 color = mapping->GetSavedColor();
                devices.push_back({ id, mapping->GetPhysicalDeviceName(),
                                    static_cast<LedSource>(mapping->GetColorSource()), color.r, color.g, color.b });
            }
            std::sort(devices.begin(), devices.end(),
                      [](const LedDevice& a, const LedDevice& b) { return a.id < b.id; });
        }
        return devices;
    }
    void SetLed(int port, const std::string& id, LedSource source, uint8_t r, uint8_t g, uint8_t b) override {
        const auto controller = PortController(port);
        if (controller == nullptr) {
            return;
        }
        const auto all = controller->GetLED()->GetAllLEDMappings();
        if (const auto found = all.find(id); found != all.end()) {
            found->second->SetColorSource(static_cast<uint8_t>(source));
            found->second->SetSavedColor(Color_RGB8{ r, g, b });
        }
    }
    void RemoveLedDevice(int port, const std::string& id) override {
        if (const auto controller = PortController(port)) {
            controller->GetLED()->ClearLEDMapping(id);
        }
    }
    bool AddLedDeviceFromPress(int port) override {
        const auto controller = PortController(port);
        return controller != nullptr && controller->GetLED()->AddLEDMappingFromRawPress();
    }

    GyroDevice Gyro(int port) override {
        GyroDevice gyro;
        if (const auto controller = PortController(port)) {
            if (const auto mapping = controller->GetGyro()->GetGyroMapping()) {
                gyro.present = true;
                gyro.name = mapping->GetPhysicalDeviceName();
                gyro.sensitivity = { mapping->GetSensitivityPercent(), GYRO_SENSITIVITY_DEFAULT, 0, 100 };
            }
        }
        return gyro;
    }
    void SetGyroSensitivity(int port, int value) override {
        if (const auto controller = PortController(port)) {
            if (const auto mapping = controller->GetGyro()->GetGyroMapping()) {
                mapping->SetSensitivity(static_cast<uint8_t>(std::clamp(value, 0, 100)));
                mapping->SaveToConfig();
            }
        }
    }
    void ResetGyroSensitivity(int port) override {
        if (const auto controller = PortController(port)) {
            if (const auto mapping = controller->GetGyro()->GetGyroMapping()) {
                mapping->ResetSensitivityToDefault();
            }
        }
    }
    void RecalibrateGyro(int port) override {
        if (const auto controller = PortController(port)) {
            if (const auto mapping = controller->GetGyro()->GetGyroMapping()) {
                mapping->Recalibrate();
                mapping->SaveToConfig();
            }
        }
    }
    void RemoveGyro(int port) override {
        if (const auto controller = PortController(port)) {
            controller->GetGyro()->ClearGyroMapping();
        }
    }
    bool AddGyroFromPress(int port) override {
        const auto controller = PortController(port);
        return controller != nullptr && controller->GetGyro()->SetGyroMappingFromRawPress();
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
    IntSetting SensitivityOf(const std::array<const char*, 4>& cvars, int port) {
        IntSetting setting{ kDefaultSensitivityPercent, kDefaultSensitivityPercent, kMinSensitivityPercent,
                            kMaxSensitivityPercent };
        if (mCvars != nullptr) {
            setting.value = std::clamp(mCvars->GetInteger(cvars[static_cast<size_t>(port)], setting.defaultValue),
                                       setting.min, setting.max);
        }
        return setting;
    }
    void SetSensitivity(const std::array<const char*, 4>& cvars, int port, int percent) {
        if (mCvars == nullptr) {
            return;
        }
        mCvars->SetInteger(cvars[static_cast<size_t>(port)],
                           std::clamp(percent, kMinSensitivityPercent, kMaxSensitivityPercent));
        if (mWindow != nullptr && mWindow->GetGui() != nullptr) {
            mWindow->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }

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
