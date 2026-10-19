#pragma once

// What the Settings tab needs from the window, the audio output and the host
// OS. The model library has no libultraship dependency, so the tab talks to
// this interface; SystemImpl.cpp backs it with the real Context. The defaults
// describe a system with none of it, which is what the unit tests get.

#include <cstdint>
#include <string>
#include <vector>

#include "platform/BindingCapture.h"
#include "platform/Bindings.h"
#include "platform/PortAssignment.h"

namespace Frigate::ConfigMenu {

// A selectable option backed by a library identifier.
struct Choice {
    int id = 0;
    std::string label;
};

// A setting with its range and default, for the controller settings below.
struct IntSetting {
    int value = 0;
    int defaultValue = 0;
    int min = 0;
    int max = 100;

    bool IsDefault() const {
        return value == defaultValue;
    }
};

// Which of a port's device settings the platform offers. A port reading a
// keyboard and mouse has none of them whatever the platform supports.
struct ControllerFeatures {
    // Rumble devices already mapped can be edited and tested.
    bool rumble = false;
    // A rumble device can be added by pressing a button on it.
    bool rumbleAdd = false;
    bool led = false;
    bool gyro = false;
};

enum class StickSetting : uint8_t { Sensitivity, Deadzone, NotchSnap };
enum class Stick : uint8_t { Left, Right };
enum class LedSource : uint8_t { Off, Set, Game };

struct RumbleDevice {
    std::string id;
    std::string name;
    IntSetting small;
    IntSetting large;
};

struct LedDevice {
    std::string id;
    std::string name;
    LedSource source = LedSource::Off;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct GyroDevice {
    bool present = false;
    std::string name;
    IntSetting sensitivity;
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
    // Pushes the internal resolution, MSAA, texture filter and aspect ratio
    // CVars to the renderer, so a change shows up without a restart.
    virtual void ApplyRenderSettings() {
    }
    // The width/height ratio the image is fitted to, with black bars filling
    // the rest of the window; 0 means it fills the window at whatever shape
    // it is. Low-Resolution Mode takes priority and is always 4:3.
    virtual float TargetAspectRatio() {
        return 0.0f;
    }
    virtual bool IsLowResolution() {
        return false;
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

    // Controller ports. What is attached.
    virtual Platform::Input::AttachedDevices Attached() {
        return {};
    }
    // The device chosen for a port (0 to 3). Kept in the config.
    virtual Platform::Input::PortDevice SelectedDevice(int port) {
        return mSelected[static_cast<size_t>(port)];
    }
    virtual void SelectDevice(int port, const Platform::Input::PortDevice& device) {
        mSelected[static_cast<size_t>(port)] = device;
        ApplyControllerPorts();
    }
    // Pushes the forced-connected port CVars and the port devices to the game's input.
    virtual void ApplyControllerPorts() {
    }
    // Whether a port counts as connected without a device (gPortNConnected).
    virtual bool PortConnected(int port) {
        return (mConnected >> port) & 1;
    }
    virtual void SetPortConnected(int port, bool connected) {
        mConnected = static_cast<uint8_t>(connected ? (mConnected | (1 << port)) : (mConnected & ~(1 << port)));
        ApplyControllerPorts();
    }

    // Device settings of a port, which only a controller has. Features says what
    // the platform offers; the rest are libultraship's controller settings.
    virtual ControllerFeatures Features(int) {
        return {};
    }
    virtual IntSetting StickValue(int, Stick, StickSetting) {
        return {};
    }
    virtual void SetStickValue(int, Stick, StickSetting, int) {
    }
    virtual void ResetStickValue(int, Stick, StickSetting) {
    }
    virtual std::vector<RumbleDevice> RumbleDevices(int) {
        return {};
    }
    virtual void SetRumbleIntensity(int, const std::string&, bool /*large*/, int) {
    }
    virtual void ResetRumbleIntensity(int, const std::string&, bool /*large*/) {
    }
    // Starts or stops the motor of one rumble device, for the Test button.
    virtual void TestRumble(int, const std::string&, bool /*on*/) {
    }
    virtual void RemoveRumbleDevice(int, const std::string&) {
    }
    // Maps the rumble motor of the gamepad whose button is pressed now; true when one was found.
    virtual bool AddRumbleDeviceFromPress(int) {
        return false;
    }
    virtual std::vector<LedDevice> LedDevices(int) {
        return {};
    }
    virtual void SetLed(int, const std::string&, LedSource, uint8_t, uint8_t, uint8_t) {
    }
    virtual void RemoveLedDevice(int, const std::string&) {
    }
    virtual bool AddLedDeviceFromPress(int) {
        return false;
    }
    virtual GyroDevice Gyro(int) {
        return {};
    }
    virtual void SetGyroSensitivity(int, int) {
    }
    virtual void ResetGyroSensitivity(int) {
    }
    virtual void RecalibrateGyro(int) {
    }
    virtual void RemoveGyro(int) {
    }
    virtual bool AddGyroFromPress(int) {
        return false;
    }

    // Resets every port's bindings and mode to the defaults.
    virtual void SetDefaultBindings() {
    }
    // Saves the binding store (Platform::Input::Bindings()) to the config after
    // the editor changed it; the game picks the change up on its next frame.
    virtual void BindingsChanged() {
    }
    // How the port's selected gamepad labels its face buttons.
    virtual Platform::Input::FaceLayout PortFaceLayout(int) {
        return Platform::Input::FaceLayout::Xbox;
    }
    // Everything held on any device right now, for Platform::Input::Capture;
    // see PollRawInputs in platform/BindingsApply.h.
    virtual std::vector<Platform::Input::RawInput> PollRawInputs(bool) {
        return {};
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

  private:
    Platform::Input::PortDevices mSelected{};
    uint8_t mConnected = 0;
};

} // namespace Frigate::ConfigMenu
