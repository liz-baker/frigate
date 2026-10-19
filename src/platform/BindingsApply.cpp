#include "platform/BindingsApply.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <imgui.h>

#include "libultraship/libultra/controller.h"
#include "platform/Input.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/mouse/MouseButtonToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/mouse/MouseButtonToButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/mouse/MouseWheelToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/mouse/MouseWheelToButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLButtonToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLButtonToButtonMapping.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include "ship/core/Context.h"
#include "ship/window/Window.h"

#ifdef __WIIU__
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUAxisDirectionToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUAxisDirectionToButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUButtonToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUButtonToButtonMapping.h"
#include "ship/port/wiiu/WiiUInput.h"
#else
#include <SDL3/SDL.h>
#endif

#ifndef CVAR_PREFIX_CONTROLLERS
#define CVAR_PREFIX_CONTROLLERS "gControllers"
#endif

namespace Frigate::Platform::Input {

namespace {

// Per port, the BindingStore::Serialize string; unset until the player edits.
constexpr const char* kPortBindingsCVars[kPortCount] = { "gPort1Bindings", "gPort2Bindings", "gPort3Bindings",
                                                         "gPort4Bindings" };
// Set once the first launch has carried libultraship's mappings over.
constexpr const char* kMigratedCVar = "gBindingsMigrated";

// How far a stick or trigger must go to count as pressed while capturing.
constexpr float kAxisPressThreshold = 0.7f;
constexpr float kMouseMotionPixels = 4.0f;

struct Environment {
    std::shared_ptr<Ship::ControlDeck> deck;
    std::shared_ptr<Ship::Window> window;
    std::shared_ptr<Ship::ConsoleVariable> cvars;
};

Environment& Env() {
    static Environment env;
    return env;
}

struct ActionTarget {
    // A button bitmask, or 0 for a stick direction.
    CONTROLLERBUTTONS_T bitmask = 0;
    Ship::Direction direction = Ship::LEFT;
};

bool N64Target(Action action, ActionTarget& out) {
    struct Entry {
        Action action;
        CONTROLLERBUTTONS_T bitmask;
    };
    static const Entry buttons[] = {
        { Action::N64A, BTN_A },           { Action::N64B, BTN_B },           { Action::N64Start, BTN_START },
        { Action::N64L, BTN_L },           { Action::N64R, BTN_R },           { Action::N64Z, BTN_Z },
        { Action::N64DUp, BTN_DUP },       { Action::N64DDown, BTN_DDOWN },   { Action::N64DLeft, BTN_DLEFT },
        { Action::N64DRight, BTN_DRIGHT }, { Action::N64CUp, BTN_CUP },       { Action::N64CDown, BTN_CDOWN },
        { Action::N64CLeft, BTN_CLEFT },   { Action::N64CRight, BTN_CRIGHT },
    };
    for (const Entry& entry : buttons) {
        if (entry.action == action) {
            out = { entry.bitmask, Ship::LEFT };
            return true;
        }
    }
    switch (action) {
        case Action::N64StickUp:
            out = { 0, Ship::UP };
            return true;
        case Action::N64StickDown:
            out = { 0, Ship::DOWN };
            return true;
        case Action::N64StickLeft:
            out = { 0, Ship::LEFT };
            return true;
        case Action::N64StickRight:
            out = { 0, Ship::RIGHT };
            return true;
        default:
            return false;
    }
}

Action ActionForButton(CONTROLLERBUTTONS_T bitmask) {
    for (const Action action : ActionsOf(BindingMode::N64)) {
        ActionTarget target;
        if (N64Target(action, target) && target.bitmask == bitmask) {
            return action;
        }
    }
    return Action::Count;
}

Action ActionForDirection(Ship::Direction direction) {
    switch (direction) {
        case Ship::UP:
            return Action::N64StickUp;
        case Ship::DOWN:
            return Action::N64StickDown;
        case Ship::LEFT:
            return Action::N64StickLeft;
        case Ship::RIGHT:
            return Action::N64StickRight;
    }
    return Action::Count;
}

Ship::WheelDirection ToLusWheel(WheelDir direction) {
    switch (direction) {
        case WheelDir::Up:
            return Ship::LUS_WHEEL_UP;
        case WheelDir::Down:
            return Ship::LUS_WHEEL_DOWN;
        case WheelDir::Left:
            return Ship::LUS_WHEEL_LEFT;
        case WheelDir::Right:
            return Ship::LUS_WHEEL_RIGHT;
    }
    return Ship::LUS_WHEEL_UNKNOWN;
}

bool FromLusWheel(int lus, WheelDir& out) {
    switch (lus) {
        case Ship::LUS_WHEEL_UP:
            out = WheelDir::Up;
            return true;
        case Ship::LUS_WHEEL_DOWN:
            out = WheelDir::Down;
            return true;
        case Ship::LUS_WHEEL_LEFT:
            out = WheelDir::Left;
            return true;
        case Ship::LUS_WHEEL_RIGHT:
            out = WheelDir::Right;
            return true;
        default:
            return false;
    }
}

// The store's stick axes are positive up; SDL's Y axes are positive down. The
// Wii U's normalized axes are positive up, like the store's.
int PlatformAxisSign(PadAxis axis, int sign) {
#ifndef __WIIU__
    if (axis == PadAxis::LeftY || axis == PadAxis::RightY) {
        return -sign;
    }
#else
    (void)axis;
#endif
    return sign;
}

#ifndef __WIIU__

int SdlButton(PadButton button) {
    switch (button) {
        case PadButton::South:
            return SDL_GAMEPAD_BUTTON_SOUTH;
        case PadButton::East:
            return SDL_GAMEPAD_BUTTON_EAST;
        case PadButton::West:
            return SDL_GAMEPAD_BUTTON_WEST;
        case PadButton::North:
            return SDL_GAMEPAD_BUTTON_NORTH;
        case PadButton::Back:
            return SDL_GAMEPAD_BUTTON_BACK;
        case PadButton::Guide:
            return SDL_GAMEPAD_BUTTON_GUIDE;
        case PadButton::Start:
            return SDL_GAMEPAD_BUTTON_START;
        case PadButton::LeftStick:
            return SDL_GAMEPAD_BUTTON_LEFT_STICK;
        case PadButton::RightStick:
            return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
        case PadButton::LeftShoulder:
            return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
        case PadButton::RightShoulder:
            return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        case PadButton::DpadUp:
            return SDL_GAMEPAD_BUTTON_DPAD_UP;
        case PadButton::DpadDown:
            return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        case PadButton::DpadLeft:
            return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
        case PadButton::DpadRight:
            return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        case PadButton::Count:
            break;
    }
    return -1;
}

bool FromSdlButton(int sdl, PadButton& out) {
    for (int i = 0; i < static_cast<int>(PadButton::Count); i++) {
        if (SdlButton(static_cast<PadButton>(i)) == sdl) {
            out = static_cast<PadButton>(i);
            return true;
        }
    }
    return false;
}

int SdlAxis(PadAxis axis) {
    switch (axis) {
        case PadAxis::LeftX:
            return SDL_GAMEPAD_AXIS_LEFTX;
        case PadAxis::LeftY:
            return SDL_GAMEPAD_AXIS_LEFTY;
        case PadAxis::RightX:
            return SDL_GAMEPAD_AXIS_RIGHTX;
        case PadAxis::RightY:
            return SDL_GAMEPAD_AXIS_RIGHTY;
        case PadAxis::LeftTrigger:
            return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        case PadAxis::RightTrigger:
            return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        case PadAxis::Count:
            break;
    }
    return -1;
}

bool FromSdlAxis(int sdl, PadAxis& out) {
    for (int i = 0; i < static_cast<int>(PadAxis::Count); i++) {
        if (SdlAxis(static_cast<PadAxis>(i)) == sdl) {
            out = static_cast<PadAxis>(i);
            return true;
        }
    }
    return false;
}

#else

uint32_t WiiUButtonOf(PadButton button) {
    using namespace Ship::WiiU;
    switch (button) {
        case PadButton::South:
            return WIIU_BUTTON_B;
        case PadButton::East:
            return WIIU_BUTTON_A;
        case PadButton::West:
            return WIIU_BUTTON_Y;
        case PadButton::North:
            return WIIU_BUTTON_X;
        case PadButton::Back:
            return WIIU_BUTTON_MINUS;
        case PadButton::Guide:
            return WIIU_BUTTON_HOME;
        case PadButton::Start:
            return WIIU_BUTTON_PLUS;
        case PadButton::LeftStick:
            return WIIU_BUTTON_STICK_L;
        case PadButton::RightStick:
            return WIIU_BUTTON_STICK_R;
        case PadButton::LeftShoulder:
            return WIIU_BUTTON_L;
        case PadButton::RightShoulder:
            return WIIU_BUTTON_R;
        case PadButton::DpadUp:
            return WIIU_BUTTON_UP;
        case PadButton::DpadDown:
            return WIIU_BUTTON_DOWN;
        case PadButton::DpadLeft:
            return WIIU_BUTTON_LEFT;
        case PadButton::DpadRight:
            return WIIU_BUTTON_RIGHT;
        case PadButton::Count:
            break;
    }
    return 0;
}

bool FromWiiUButton(uint32_t wiiu, PadButton& out) {
    for (int i = 0; i < static_cast<int>(PadButton::Count); i++) {
        if (WiiUButtonOf(static_cast<PadButton>(i)) == wiiu) {
            out = static_cast<PadButton>(i);
            return true;
        }
    }
    return false;
}

// The Wii U's triggers are buttons: ZL and ZR stand in for the store's trigger axes.
uint32_t WiiUTriggerButton(PadAxis axis) {
    return axis == PadAxis::LeftTrigger ? Ship::WiiU::WIIU_BUTTON_ZL : Ship::WiiU::WIIU_BUTTON_ZR;
}

int WiiUAxisOf(PadAxis axis) {
    switch (axis) {
        case PadAxis::LeftX:
            return Ship::WiiU::WIIU_AXIS_LEFT_X;
        case PadAxis::LeftY:
            return Ship::WiiU::WIIU_AXIS_LEFT_Y;
        case PadAxis::RightX:
            return Ship::WiiU::WIIU_AXIS_RIGHT_X;
        default:
            return Ship::WiiU::WIIU_AXIS_RIGHT_Y;
    }
}

bool FromWiiUAxis(int wiiu, PadAxis& out) {
    for (const PadAxis axis : { PadAxis::LeftX, PadAxis::LeftY, PadAxis::RightX, PadAxis::RightY }) {
        if (WiiUAxisOf(axis) == wiiu) {
            out = axis;
            return true;
        }
    }
    return false;
}

#endif

// One input of an action as a libultraship button mapping; null when the
// input can't drive an N64 button.
std::shared_ptr<Ship::ControllerButtonMapping> MakeButtonMapping(int port, CONTROLLERBUTTONS_T bitmask,
                                                                 const PhysicalInput& input, int32_t padId) {
    const Environment& env = Env();
    const auto p = static_cast<uint8_t>(port);
    (void)padId;
    switch (input.kind) {
        case InputKind::Key:
            return std::make_shared<Ship::KeyboardKeyToButtonMapping>(
                p, bitmask, static_cast<Ship::KbScancode>(input.code), env.deck, env.window, env.cvars);
        case InputKind::MouseButton:
            return std::make_shared<Ship::MouseButtonToButtonMapping>(
                p, bitmask, static_cast<Ship::MouseBtn>(input.code), env.deck, env.cvars);
        case InputKind::MouseWheel:
            return std::make_shared<Ship::MouseWheelToButtonMapping>(
                p, bitmask, ToLusWheel(static_cast<WheelDir>(input.code)), env.deck, env.cvars);
#ifndef __WIIU__
        case InputKind::PadButton:
            return std::make_shared<Ship::SDLButtonToButtonMapping>(
                p, bitmask, SdlButton(static_cast<PadButton>(input.code)), env.deck, env.cvars);
        case InputKind::PadAxis:
            return std::make_shared<Ship::SDLAxisDirectionToButtonMapping>(
                p, bitmask, SdlAxis(static_cast<PadAxis>(input.code)),
                PlatformAxisSign(static_cast<PadAxis>(input.code), input.sign), env.deck, env.cvars);
#else
        case InputKind::PadButton:
            return std::make_shared<Ship::WiiUButtonToButtonMapping>(
                p, bitmask, padId, WiiUButtonOf(static_cast<PadButton>(input.code)), env.deck, env.cvars);
        case InputKind::PadAxis: {
            const auto axis = static_cast<PadAxis>(input.code);
            if (axis == PadAxis::LeftTrigger || axis == PadAxis::RightTrigger) {
                return std::make_shared<Ship::WiiUButtonToButtonMapping>(p, bitmask, padId, WiiUTriggerButton(axis),
                                                                         env.deck, env.cvars);
            }
            return std::make_shared<Ship::WiiUAxisDirectionToButtonMapping>(p, bitmask, padId, WiiUAxisOf(axis),
                                                                            input.sign, env.deck, env.cvars);
        }
#endif
        default:
            return nullptr;
    }
}

// The same for a direction of the N64 stick.
std::shared_ptr<Ship::ControllerAxisDirectionMapping> MakeStickMapping(int port, Ship::Direction direction,
                                                                       const PhysicalInput& input, int32_t padId) {
    const Environment& env = Env();
    const auto p = static_cast<uint8_t>(port);
    (void)padId;
    switch (input.kind) {
        case InputKind::Key:
            return std::make_shared<Ship::KeyboardKeyToAxisDirectionMapping>(p, Ship::LEFT_STICK, direction,
                                                                             static_cast<Ship::KbScancode>(input.code),
                                                                             env.deck, env.window, env.cvars);
        case InputKind::MouseButton:
            return std::make_shared<Ship::MouseButtonToAxisDirectionMapping>(
                p, Ship::LEFT_STICK, direction, static_cast<Ship::MouseBtn>(input.code), env.deck, env.cvars);
        case InputKind::MouseWheel:
            return std::make_shared<Ship::MouseWheelToAxisDirectionMapping>(
                p, Ship::LEFT_STICK, direction, ToLusWheel(static_cast<WheelDir>(input.code)), env.deck, env.cvars);
#ifndef __WIIU__
        case InputKind::PadButton:
            return std::make_shared<Ship::SDLButtonToAxisDirectionMapping>(
                p, Ship::LEFT_STICK, direction, SdlButton(static_cast<PadButton>(input.code)), env.deck, env.cvars);
        case InputKind::PadAxis:
            return std::make_shared<Ship::SDLAxisDirectionToAxisDirectionMapping>(
                p, Ship::LEFT_STICK, direction, SdlAxis(static_cast<PadAxis>(input.code)),
                PlatformAxisSign(static_cast<PadAxis>(input.code), input.sign), env.deck, env.cvars);
#else
        case InputKind::PadButton:
            return std::make_shared<Ship::WiiUButtonToAxisDirectionMapping>(
                p, Ship::LEFT_STICK, direction, padId, WiiUButtonOf(static_cast<PadButton>(input.code)), env.deck,
                env.cvars);
        case InputKind::PadAxis: {
            const auto axis = static_cast<PadAxis>(input.code);
            if (axis == PadAxis::LeftTrigger || axis == PadAxis::RightTrigger) {
                return std::make_shared<Ship::WiiUButtonToAxisDirectionMapping>(
                    p, Ship::LEFT_STICK, direction, padId, WiiUTriggerButton(axis), env.deck, env.cvars);
            }
            return std::make_shared<Ship::WiiUAxisDirectionToAxisDirectionMapping>(
                p, Ship::LEFT_STICK, direction, padId, WiiUAxisOf(axis), input.sign, env.deck, env.cvars);
        }
#endif
        default:
            return nullptr;
    }
}

ProfileDevice ProfileDeviceOf(const PortDevice& device) {
    return device.kind == DeviceKind::KeyboardMouse ? ProfileDevice::KeyboardMouse : ProfileDevice::Gamepad;
}

void ApplyPort(Ship::ControlDeck* deck, const BindingStore& store, int port, const PortDevice& device) {
    const auto controller = deck->GetControllerByPort(static_cast<uint8_t>(port));
    if (controller == nullptr) {
        return;
    }
    const AttachedDevices attached = AttachedDevicesOf(deck);
    const ProfileDevice profileDevice = ProfileDeviceOf(device);
    int32_t padId = 0;
    if (profileDevice == ProfileDevice::Gamepad) {
        const auto index = FindGamepad(device, attached);
        if (!index) {
            // Its mappings stay as they are until the device is back.
            return;
        }
        padId = attached.gamepads[*index].id;
    }

    if (profileDevice == ProfileDevice::KeyboardMouse) {
        controller->ClearAllMappingsForDeviceType(Ship::PhysicalDeviceType::Keyboard);
        controller->ClearAllMappingsForDeviceType(Ship::PhysicalDeviceType::Mouse);
    } else {
        controller->ClearAllMappingsForDeviceType(PHYSICAL_DEVICE_TYPE_GAMEPAD);
    }

    const Profile& profile = store.Get({ port, BindingMode::N64, profileDevice });
    const auto stick = controller->GetLeftStick();
    for (const Action action : ActionsOf(BindingMode::N64)) {
        ActionTarget target;
        if (!N64Target(action, target)) {
            continue;
        }
        for (const PhysicalInput& input : profile.Of(action)) {
            if (target.bitmask != 0) {
                const auto button = controller->GetButton(target.bitmask);
                if (const auto mapping = MakeButtonMapping(port, target.bitmask, input, padId); mapping && button) {
                    button->AddButtonMapping(mapping);
                    mapping->SaveToConfig();
                }
            } else if (const auto mapping = MakeStickMapping(port, target.direction, input, padId); mapping && stick) {
                stick->AddAxisDirectionMapping(target.direction, mapping);
                mapping->SaveToConfig();
            }
        }
    }
    for (const auto& [bitmask, button] : controller->GetAllButtons()) {
        button->SaveButtonMappingIdsToConfig();
    }
    if (stick) {
        stick->SaveAxisDirectionMappingIdsToConfig();
    }
    if (Env().cvars) {
        const std::string hasConfig =
            std::string(CVAR_PREFIX_CONTROLLERS) + ".Port" + std::to_string(port + 1) + ".HasConfig";
        Env().cvars->SetInteger(hasConfig.c_str(), true);
        Env().cvars->Save();
    }
}

int ConfigInt(const std::string& key, int fallback) {
    return Env().cvars ? Env().cvars->GetInteger(key.c_str(), fallback) : fallback;
}

std::string ConfigString(const std::string& key) {
    return Env().cvars ? Env().cvars->GetString(key.c_str(), "") : std::string();
}

// A pad button or axis as read from a mapping's saved settings, `mapping` being
// the cvar prefix of that mapping.
bool ReadPadInput(const std::string& mapping, const std::string& cls, PhysicalInput& out) {
#ifndef __WIIU__
    if (cls.rfind("SDLButton", 0) == 0) {
        PadButton button;
        if (!FromSdlButton(ConfigInt(mapping + ".SDLControllerButton", -1), button)) {
            return false;
        }
        out = PadInput(button);
        return true;
    }
    if (cls.rfind("SDLAxisDirection", 0) == 0) {
        PadAxis axis;
        const int direction = ConfigInt(mapping + ".AxisDirection", 0);
        if (!FromSdlAxis(ConfigInt(mapping + ".SDLControllerAxis", -1), axis) || (direction != -1 && direction != 1)) {
            return false;
        }
        // SDL's Y axes are positive down.
        const bool flipped = axis == PadAxis::LeftY || axis == PadAxis::RightY;
        out = AxisInput(axis, flipped ? -direction : direction);
        return true;
    }
#else
    if (cls.rfind("WiiUButton", 0) == 0) {
        PadButton button;
        const auto wiiu = static_cast<uint32_t>(ConfigInt(mapping + ".WiiUButton", 0));
        if (wiiu == Ship::WiiU::WIIU_BUTTON_ZL || wiiu == Ship::WiiU::WIIU_BUTTON_ZR) {
            out = AxisInput(wiiu == Ship::WiiU::WIIU_BUTTON_ZL ? PadAxis::LeftTrigger : PadAxis::RightTrigger, 1);
            return true;
        }
        if (!FromWiiUButton(wiiu, button)) {
            return false;
        }
        out = PadInput(button);
        return true;
    }
    if (cls.rfind("WiiUAxisDirection", 0) == 0) {
        PadAxis axis;
        const int direction = ConfigInt(mapping + ".AxisDirection", 0);
        if (!FromWiiUAxis(ConfigInt(mapping + ".WiiUAxis", -1), axis) || (direction != -1 && direction != 1)) {
            return false;
        }
        out = AxisInput(axis, direction);
        return true;
    }
#endif
    return false;
}

// Reads one saved mapping, whatever it maps to, as an input.
bool ReadInput(const std::string& mapping, const std::string& cls, PhysicalInput& out) {
    if (cls.rfind("KeyboardKey", 0) == 0) {
        out = KeyInput(ConfigInt(mapping + ".KeyboardScancode", 0));
        return out.code > 0;
    }
    if (cls.rfind("MouseButton", 0) == 0) {
        out = MouseInput(ConfigInt(mapping + ".MouseButton", 0));
        return out.code < 5;
    }
    if (cls.rfind("MouseWheel", 0) == 0) {
        WheelDir direction;
        if (!FromLusWheel(ConfigInt(mapping + ".WheelDirection", 0), direction)) {
            return false;
        }
        out = WheelInput(direction);
        return true;
    }
    return ReadPadInput(mapping, cls, out);
}

struct KeyEntry {
    ImGuiKey imgui;
    int scancode;
};

const std::vector<KeyEntry>& KeyTable() {
    static const std::vector<KeyEntry> table = [] {
        std::vector<KeyEntry> keys = {
            { ImGuiKey_Escape, Ship::LUS_KB_ESCAPE },
            { ImGuiKey_Tab, Ship::LUS_KB_TAB },
            { ImGuiKey_Backspace, Ship::LUS_KB_BACKSPACE },
            { ImGuiKey_Enter, Ship::LUS_KB_ENTER },
            { ImGuiKey_Space, Ship::LUS_KB_SPACE },
            { ImGuiKey_CapsLock, Ship::LUS_KB_CAPSLOCK },
            { ImGuiKey_LeftCtrl, Ship::LUS_KB_CONTROL },
            { ImGuiKey_RightCtrl, Ship::LUS_KB_CONTROL },
            { ImGuiKey_LeftShift, Ship::LUS_KB_SHIFT },
            { ImGuiKey_RightShift, Ship::LUS_KB_RSHIFT },
            { ImGuiKey_LeftAlt, Ship::LUS_KB_ALT },
            { ImGuiKey_RightAlt, Ship::LUS_KB_ALT },
            { ImGuiKey_UpArrow, Ship::LUS_KB_ARROWKEY_UP },
            { ImGuiKey_DownArrow, Ship::LUS_KB_ARROWKEY_DOWN },
            { ImGuiKey_LeftArrow, Ship::LUS_KB_ARROWKEY_LEFT },
            { ImGuiKey_RightArrow, Ship::LUS_KB_ARROWKEY_RIGHT },
            { ImGuiKey_Minus, Ship::LUS_KB_OEM_MINUS },
            { ImGuiKey_Equal, Ship::LUS_KB_OEM_PLUS },
            { ImGuiKey_LeftBracket, Ship::LUS_KB_OEM_4 },
            { ImGuiKey_RightBracket, Ship::LUS_KB_OEM_6 },
            { ImGuiKey_Semicolon, Ship::LUS_KB_OEM_1 },
            { ImGuiKey_Apostrophe, Ship::LUS_KB_OEM_7 },
            { ImGuiKey_GraveAccent, Ship::LUS_KB_OEM_3 },
            { ImGuiKey_Backslash, Ship::LUS_KB_OEM_5 },
            { ImGuiKey_Comma, Ship::LUS_KB_OEM_COMMA },
            { ImGuiKey_Period, Ship::LUS_KB_OEM_PERIOD },
            { ImGuiKey_Slash, Ship::LUS_KB_OEM_2 },
        };
        const Ship::KbScancode letters[26] = {
            Ship::LUS_KB_A, Ship::LUS_KB_B, Ship::LUS_KB_C, Ship::LUS_KB_D, Ship::LUS_KB_E, Ship::LUS_KB_F,
            Ship::LUS_KB_G, Ship::LUS_KB_H, Ship::LUS_KB_I, Ship::LUS_KB_J, Ship::LUS_KB_K, Ship::LUS_KB_L,
            Ship::LUS_KB_M, Ship::LUS_KB_N, Ship::LUS_KB_O, Ship::LUS_KB_P, Ship::LUS_KB_Q, Ship::LUS_KB_R,
            Ship::LUS_KB_S, Ship::LUS_KB_T, Ship::LUS_KB_U, Ship::LUS_KB_V, Ship::LUS_KB_W, Ship::LUS_KB_X,
            Ship::LUS_KB_Y, Ship::LUS_KB_Z,
        };
        for (int i = 0; i < 26; i++) {
            keys.push_back({ static_cast<ImGuiKey>(ImGuiKey_A + i), letters[i] });
        }
        const Ship::KbScancode digits[10] = { Ship::LUS_KB_0, Ship::LUS_KB_1, Ship::LUS_KB_2, Ship::LUS_KB_3,
                                              Ship::LUS_KB_4, Ship::LUS_KB_5, Ship::LUS_KB_6, Ship::LUS_KB_7,
                                              Ship::LUS_KB_8, Ship::LUS_KB_9 };
        const Ship::KbScancode keypad[10] = { Ship::LUS_KB_NUMPAD0, Ship::LUS_KB_NUMPAD1, Ship::LUS_KB_NUMPAD2,
                                              Ship::LUS_KB_NUMPAD3, Ship::LUS_KB_NUMPAD4, Ship::LUS_KB_NUMPAD5,
                                              Ship::LUS_KB_NUMPAD6, Ship::LUS_KB_NUMPAD7, Ship::LUS_KB_NUMPAD8,
                                              Ship::LUS_KB_NUMPAD9 };
        for (int i = 0; i < 10; i++) {
            keys.push_back({ static_cast<ImGuiKey>(ImGuiKey_0 + i), digits[i] });
            keys.push_back({ static_cast<ImGuiKey>(ImGuiKey_Keypad0 + i), keypad[i] });
        }
        const Ship::KbScancode functionKeys[12] = { Ship::LUS_KB_F1,  Ship::LUS_KB_F2,  Ship::LUS_KB_F3,
                                                    Ship::LUS_KB_F4,  Ship::LUS_KB_F5,  Ship::LUS_KB_F6,
                                                    Ship::LUS_KB_F7,  Ship::LUS_KB_F8,  Ship::LUS_KB_F9,
                                                    Ship::LUS_KB_F10, Ship::LUS_KB_F11, Ship::LUS_KB_F12 };
        for (int i = 0; i < 12; i++) {
            keys.push_back({ static_cast<ImGuiKey>(ImGuiKey_F1 + i), functionKeys[i] });
        }
        return keys;
    }();
    return table;
}

void PollKeyboardAndMouse(bool mouseOverUi, std::vector<RawInput>& out) {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    const InputSource keyboard{ InputSource::Kind::Keyboard, 0 };
    for (const KeyEntry& key : KeyTable()) {
        if (ImGui::IsKeyDown(key.imgui)) {
            out.push_back({ keyboard, KeyInput(key.scancode) });
        }
    }

    const InputSource mouse{ InputSource::Kind::Mouse, 0 };
    // ImGui numbers the buttons left, right, middle, back, forward.
    static const int imguiButtons[5] = { 0, 2, 1, 3, 4 };
    if (!mouseOverUi) {
        for (int button = 0; button < 5; button++) {
            if (ImGui::IsMouseDown(imguiButtons[button])) {
                out.push_back({ mouse, MouseInput(button) });
            }
        }
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel != 0.0f) {
        out.push_back({ mouse, WheelInput(io.MouseWheel > 0.0f ? WheelDir::Up : WheelDir::Down) });
    }
    if (io.MouseWheelH != 0.0f) {
        out.push_back({ mouse, WheelInput(io.MouseWheelH > 0.0f ? WheelDir::Right : WheelDir::Left) });
    }
    for (int axis = 0; axis < 2; axis++) {
        const float delta = axis == 0 ? io.MouseDelta.x : io.MouseDelta.y;
        if (std::fabs(delta) >= kMouseMotionPixels) {
            out.push_back({ mouse, MotionInput(axis, delta < 0.0f ? -1 : 1) });
        }
    }
}

void PollGamepads(Ship::ControlDeck* deck, std::vector<RawInput>& out) {
    if (deck == nullptr) {
        return;
    }
    for (const Ship::ConnectedGamepad& pad : deck->GetConnectedPhysicalDeviceManager()->GetConnectedGamepads()) {
        const InputSource source{ InputSource::Kind::Gamepad, pad.id };
#ifndef __WIIU__
        SDL_Gamepad* gamepad = SDL_GetGamepadFromID(static_cast<SDL_JoystickID>(pad.id));
        if (gamepad == nullptr) {
            continue;
        }
        for (int i = 0; i < static_cast<int>(PadButton::Count); i++) {
            const auto button = static_cast<PadButton>(i);
            if (SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(SdlButton(button)))) {
                out.push_back({ source, PadInput(button) });
            }
        }
        for (int i = 0; i < static_cast<int>(PadAxis::Count); i++) {
            const auto axis = static_cast<PadAxis>(i);
            const float value = SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(SdlAxis(axis))) / 32767.0f;
            const int up = PlatformAxisSign(axis, 1);
            if (value >= kAxisPressThreshold) {
                out.push_back({ source, AxisInput(axis, up) });
            } else if (value <= -kAxisPressThreshold && axis < PadAxis::LeftTrigger) {
                out.push_back({ source, AxisInput(axis, -up) });
            }
        }
#else
        const uint32_t held = Ship::WiiU::GetButtonsHeld(pad.id);
        for (int i = 0; i < static_cast<int>(PadButton::Count); i++) {
            const auto button = static_cast<PadButton>(i);
            if ((held & WiiUButtonOf(button)) != 0) {
                out.push_back({ source, PadInput(button) });
            }
        }
        for (const PadAxis trigger : { PadAxis::LeftTrigger, PadAxis::RightTrigger }) {
            if ((held & WiiUTriggerButton(trigger)) != 0) {
                out.push_back({ source, AxisInput(trigger, 1) });
            }
        }
        for (const PadAxis axis : { PadAxis::LeftX, PadAxis::LeftY, PadAxis::RightX, PadAxis::RightY }) {
            const float value = Ship::WiiU::GetAxisValue(pad.id, WiiUAxisOf(axis));
            if (value >= kAxisPressThreshold) {
                out.push_back({ source, AxisInput(axis, 1) });
            } else if (value <= -kAxisPressThreshold) {
                out.push_back({ source, AxisInput(axis, -1) });
            }
        }
#endif
    }
}

} // namespace

void InitBindings(const std::shared_ptr<Ship::Context>& context) {
    InitBindings(context->GetChildren().GetFirst<Ship::ControlDeck>(), context->GetChildren().GetFirst<Ship::Window>(),
                 context->GetChildren().GetFirst<Ship::ConsoleVariable>());
}

void InitBindings(std::shared_ptr<Ship::ControlDeck> deck, std::shared_ptr<Ship::Window> window,
                  std::shared_ptr<Ship::ConsoleVariable> cvars) {
    Environment& env = Env();
    env.deck = std::move(deck);
    env.window = std::move(window);
    env.cvars = std::move(cvars);
}

std::vector<LegacyBinding> ReadLegacyBindings(Ship::ControlDeck* deck, int port) {
    std::vector<LegacyBinding> legacy;
    const auto controller = deck != nullptr ? deck->GetControllerByPort(static_cast<uint8_t>(port)) : nullptr;
    if (controller == nullptr) {
        return legacy;
    }
    for (const auto& [bitmask, button] : controller->GetAllButtons()) {
        const Action action = ActionForButton(bitmask);
        if (action == Action::Count) {
            continue;
        }
        for (const auto& [id, mapping] : button->GetAllButtonMappings()) {
            const std::string key =
                std::string(CVAR_PREFIX_CONTROLLERS) + ".ButtonMappings." + mapping->GetButtonMappingId();
            PhysicalInput input;
            if (ReadInput(key, ConfigString(key + ".ButtonMappingClass"), input)) {
                legacy.push_back({ action, input });
            }
        }
    }
    if (const auto stick = controller->GetLeftStick()) {
        for (const auto& [direction, mappings] : stick->GetAllAxisDirectionMappings()) {
            for (const auto& [id, mapping] : mappings) {
                const std::string key = std::string(CVAR_PREFIX_CONTROLLERS) + ".AxisDirectionMappings." +
                                        mapping->GetAxisDirectionMappingId();
                PhysicalInput input;
                if (ReadInput(key, ConfigString(key + ".AxisDirectionMappingClass"), input)) {
                    legacy.push_back({ ActionForDirection(direction), input });
                }
            }
        }
    }
    // Hash-map order is arbitrary; keep the migrated order stable.
    std::stable_sort(legacy.begin(), legacy.end(), [](const LegacyBinding& a, const LegacyBinding& b) {
        return a.action != b.action ? a.action < b.action : a.input < b.input;
    });
    return legacy;
}

void LoadBindings(Ship::ControlDeck* deck) {
    static bool loaded = false;
    if (loaded || !Env().cvars) {
        return;
    }
    loaded = true;
    BindingStore& store = Bindings();
    bool anySaved = false;
    for (int port = 0; port < kPortCount; port++) {
        const std::string saved = ConfigString(kPortBindingsCVars[port]);
        if (!saved.empty() && store.Deserialize(port, saved)) {
            anySaved = true;
        }
    }
    if (anySaved || ConfigInt(kMigratedCVar, 0) != 0) {
        return;
    }
    for (int port = 0; port < kPortCount; port++) {
        const std::vector<LegacyBinding> legacy = ReadLegacyBindings(deck, port);
        for (const ProfileDevice device : { ProfileDevice::KeyboardMouse, ProfileDevice::Gamepad }) {
            std::vector<LegacyBinding> forDevice;
            for (const LegacyBinding& binding : legacy) {
                if (Accepts(binding.action, binding.input, device)) {
                    forDevice.push_back(binding);
                }
            }
            if (forDevice.empty()) {
                continue;
            }
            const Profile migrated = ProfileFromLegacy(forDevice, device);
            if (migrated != DefaultProfile(BindingMode::N64, device)) {
                store.SetProfile({ port, BindingMode::N64, device }, migrated);
            }
        }
    }
    Env().cvars->SetInteger(kMigratedCVar, 1);
    SaveBindings();
}

void SaveBindings() {
    if (!Env().cvars) {
        return;
    }
    for (int port = 0; port < kPortCount; port++) {
        Env().cvars->SetString(kPortBindingsCVars[port], Bindings().Serialize(port).c_str());
    }
    Env().cvars->Save();
}

void ApplyBindings(Ship::ControlDeck* deck, const BindingStore& store, const PortDevices& devices) {
    if (deck == nullptr || !Env().deck) {
        return;
    }
    for (int port = 0; port < kPortCount; port++) {
        const PortDevice& device = devices[static_cast<size_t>(port)];
        if (device.kind != DeviceKind::KeyboardMouse && device.kind != DeviceKind::Gamepad) {
            continue;
        }
        // TODO(https://github.com/liz-baker/frigate/issues/500): a port in Modern mode keeps libultraship's mappings
        // until the Modern actions are synthesized into the pad state.
        if (store.Mode(port) != BindingMode::N64) {
            continue;
        }
        ApplyPort(deck, store, port, device);
    }
}

FaceLayout FaceLayoutOf(Ship::ControlDeck* deck, const PortDevice& device) {
    if (deck == nullptr || device.kind != DeviceKind::Gamepad) {
        return FaceLayout::Xbox;
    }
    const auto pads = deck->GetConnectedPhysicalDeviceManager()->GetConnectedGamepads();
    AttachedDevices attached;
    for (const Ship::ConnectedGamepad& pad : pads) {
        attached.gamepads.push_back({ pad.id, pad.name });
    }
    const auto index = FindGamepad(device, attached);
    if (!index) {
        return FaceLayout::Xbox;
    }
    switch (pads[*index].faceLayout) {
        case Ship::GamepadFaceLayout::Nintendo:
            return FaceLayout::Nintendo;
        case Ship::GamepadFaceLayout::PlayStation:
            return FaceLayout::PlayStation;
        case Ship::GamepadFaceLayout::Xbox:
            break;
    }
    return FaceLayout::Xbox;
}

std::vector<RawInput> PollRawInputs(Ship::ControlDeck* deck, bool mouseOverUi) {
    std::vector<RawInput> inputs;
    PollKeyboardAndMouse(mouseOverUi, inputs);
    PollGamepads(deck, inputs);
    return inputs;
}

} // namespace Frigate::Platform::Input
