#include "platform/Bindings.h"

#include <algorithm>
#include <map>

namespace Frigate::Platform::Input {

namespace {

// Scancodes the defaults and the key names use (libultraship's PS/2 set 1 values).
enum Key : int {
    kEsc = 1,
    kBackspace = 14,
    kTab = 15,
    kQ = 16,
    kW = 17,
    kE = 18,
    kR = 19,
    kT = 20,
    kEnter = 28,
    kCtrl = 29,
    kA = 30,
    kS = 31,
    kD = 32,
    kF = 33,
    kG = 34,
    kH = 35,
    kShift = 42,
    kZ = 44,
    kX = 45,
    kC = 46,
    kV = 47,
    kAlt = 56,
    kSpace = 57,
    kArrowUp = 328,
    kArrowLeft = 331,
    kArrowRight = 333,
    kArrowDown = 336,
};

constexpr ContextMask kGameplay = kContextOnFoot | kContextAiming;

#define ACTION(id, mode, section, key, label, contexts, required, motion) \
    { Action::id, BindingMode::mode, Section::section, key, label, contexts, required, motion }

const ActionInfo kActions[] = {
    ACTION(N64A, N64, N64Face, "n64.a", "A", kContextAll, true, false),
    ACTION(N64B, N64, N64Face, "n64.b", "B", kContextAll, true, false),
    ACTION(N64Start, N64, N64Face, "n64.start", "Start", kContextAll, true, false),
    ACTION(N64L, N64, N64Face, "n64.l", "L", kContextAll, false, false),
    ACTION(N64R, N64, N64Face, "n64.r", "R", kContextAll, false, false),
    ACTION(N64Z, N64, N64Face, "n64.z", "Z", kContextAll, false, false),
    ACTION(N64DUp, N64, N64DPad, "n64.dup", "D-pad Up", kContextAll, false, false),
    ACTION(N64DDown, N64, N64DPad, "n64.ddown", "D-pad Down", kContextAll, false, false),
    ACTION(N64DLeft, N64, N64DPad, "n64.dleft", "D-pad Left", kContextAll, false, false),
    ACTION(N64DRight, N64, N64DPad, "n64.dright", "D-pad Right", kContextAll, false, false),
    ACTION(N64CUp, N64, N64CButtons, "n64.cup", "C-Up", kContextAll, false, false),
    ACTION(N64CDown, N64, N64CButtons, "n64.cdown", "C-Down", kContextAll, false, false),
    ACTION(N64CLeft, N64, N64CButtons, "n64.cleft", "C-Left", kContextAll, false, false),
    ACTION(N64CRight, N64, N64CButtons, "n64.cright", "C-Right", kContextAll, false, false),
    ACTION(N64StickUp, N64, N64Stick, "n64.stickup", "Stick Up", kContextAll, true, false),
    ACTION(N64StickDown, N64, N64Stick, "n64.stickdown", "Stick Down", kContextAll, true, false),
    ACTION(N64StickLeft, N64, N64Stick, "n64.stickleft", "Stick Left", kContextAll, true, false),
    ACTION(N64StickRight, N64, N64Stick, "n64.stickright", "Stick Right", kContextAll, true, false),

    ACTION(MoveForward, Modern, ModernMove, "modern.forward", "Move Forward", kContextOnFoot, false, false),
    ACTION(MoveBack, Modern, ModernMove, "modern.back", "Move Back", kContextOnFoot, false, false),
    ACTION(StrafeLeft, Modern, ModernMove, "modern.strafeleft", "Strafe Left", kContextOnFoot, false, false),
    ACTION(StrafeRight, Modern, ModernMove, "modern.straferight", "Strafe Right", kContextOnFoot, false, false),
    ACTION(LookUp, Modern, ModernLook, "modern.lookup", "Look Up", kGameplay, false, true),
    ACTION(LookDown, Modern, ModernLook, "modern.lookdown", "Look Down", kGameplay, false, true),
    ACTION(TurnLeft, Modern, ModernLook, "modern.turnleft", "Turn Left", kGameplay, false, true),
    ACTION(TurnRight, Modern, ModernLook, "modern.turnright", "Turn Right", kGameplay, false, true),
    ACTION(Aim, Modern, ModernCombat, "modern.aim", "Aim", kGameplay, false, false),
    ACTION(Fire, Modern, ModernCombat, "modern.fire", "Fire", kGameplay, false, false),
    ACTION(Crouch, Modern, ModernCombat, "modern.crouch", "Crouch", kGameplay, false, false),
    ACTION(Interact, Modern, ModernCombat, "modern.interact", "Reload / Interact", kGameplay, false, false),
    ACTION(WeaponNext, Modern, ModernCombat, "modern.weaponnext", "Next Weapon", kContextOnFoot, false, false),
    ACTION(WeaponPrev, Modern, ModernCombat, "modern.weaponprev", "Previous Weapon", kContextOnFoot, false, false),
    ACTION(Pause, Modern, ModernSystem, "modern.pause", "Pause", kGameplay, true, false),
    ACTION(ZoomIn, Modern, ModernCombat, "modern.zoomin", "Zoom In", kContextAiming, false, false),
    ACTION(ZoomOut, Modern, ModernCombat, "modern.zoomout", "Zoom Out", kContextAiming, false, false),
    ACTION(LeanLeft, Modern, ModernCombat, "modern.leanleft", "Lean Left", kContextAiming, false, false),
    ACTION(LeanRight, Modern, ModernCombat, "modern.leanright", "Lean Right", kContextAiming, false, false),
    ACTION(Detonate, Modern, ModernCombat, "modern.detonate", "Detonate", kGameplay, false, false),
    ACTION(MenuUp, Modern, ModernMenu, "modern.menuup", "Menu Up", kContextMenu, true, false),
    ACTION(MenuDown, Modern, ModernMenu, "modern.menudown", "Menu Down", kContextMenu, true, false),
    ACTION(MenuLeft, Modern, ModernMenu, "modern.menuleft", "Menu Left", kContextMenu, true, false),
    ACTION(MenuRight, Modern, ModernMenu, "modern.menuright", "Menu Right", kContextMenu, true, false),
    ACTION(MenuSelect, Modern, ModernMenu, "modern.menuselect", "Menu Select", kContextMenu, true, false),
    ACTION(MenuBack, Modern, ModernMenu, "modern.menuback", "Menu Back", kContextMenu, true, false),
};

#undef ACTION

static_assert(sizeof(kActions) / sizeof(kActions[0]) == kActionCount, "every action needs an entry");

const char* kSectionNames[] = { "Buttons", "D-pad",  "C-buttons", "Stick", "Movement",
                                "Look",    "Combat", "System",    "Menus" };
static_assert(sizeof(kSectionNames) / sizeof(kSectionNames[0]) == static_cast<size_t>(Section::Count),
              "every section needs a name");

bool ContextsOverlap(Action a, Action b) {
    return (Info(a).contexts & Info(b).contexts) != 0;
}

std::string KeyName(int scancode) {
    static const std::map<int, const char*> names = {
        { 1, "Esc" },
        { 2, "1" },
        { 3, "2" },
        { 4, "3" },
        { 5, "4" },
        { 6, "5" },
        { 7, "6" },
        { 8, "7" },
        { 9, "8" },
        { 10, "9" },
        { 11, "0" },
        { 12, "-" },
        { 13, "=" },
        { kBackspace, "Backspace" },
        { kTab, "Tab" },
        { kQ, "Q" },
        { kW, "W" },
        { kE, "E" },
        { kR, "R" },
        { kT, "T" },
        { 21, "Y" },
        { 22, "U" },
        { 23, "I" },
        { 24, "O" },
        { 25, "P" },
        { 26, "[" },
        { 27, "]" },
        { kEnter, "Enter" },
        { kCtrl, "Ctrl" },
        { kA, "A" },
        { kS, "S" },
        { kD, "D" },
        { kF, "F" },
        { kG, "G" },
        { kH, "H" },
        { 36, "J" },
        { 37, "K" },
        { 38, "L" },
        { 39, ";" },
        { 40, "'" },
        { 41, "`" },
        { kShift, "Shift" },
        { 43, "\\" },
        { kZ, "Z" },
        { kX, "X" },
        { kC, "C" },
        { kV, "V" },
        { 48, "B" },
        { 49, "N" },
        { 50, "M" },
        { 51, "," },
        { 52, "." },
        { 53, "/" },
        { 54, "Right Shift" },
        { kAlt, "Alt" },
        { kSpace, "Space" },
        { 58, "Caps Lock" },
        { 59, "F1" },
        { 60, "F2" },
        { 61, "F3" },
        { 62, "F4" },
        { 63, "F5" },
        { 64, "F6" },
        { 65, "F7" },
        { 66, "F8" },
        { 67, "F9" },
        { 68, "F10" },
        { 87, "F11" },
        { 88, "F12" },
        { 71, "Numpad 7" },
        { 72, "Numpad 8" },
        { 73, "Numpad 9" },
        { 75, "Numpad 4" },
        { 76, "Numpad 5" },
        { 77, "Numpad 6" },
        { 79, "Numpad 1" },
        { 80, "Numpad 2" },
        { 81, "Numpad 3" },
        { 82, "Numpad 0" },
        { kArrowUp, "Up Arrow" },
        { kArrowLeft, "Left Arrow" },
        { kArrowRight, "Right Arrow" },
        { kArrowDown, "Down Arrow" },
    };
    const auto found = names.find(scancode);
    return found != names.end() ? found->second : "Key " + std::to_string(scancode);
}

std::string StickName(const char* side, int axis, int sign) {
    std::string name = side;
    name += axis == 0 ? (sign > 0 ? " Stick Right" : " Stick Left") : (sign > 0 ? " Stick Up" : " Stick Down");
    return name;
}

const char* MouseButtonName(int button) {
    static const char* names[] = { "Left Mouse", "Middle Mouse", "Right Mouse", "Mouse Back", "Mouse Forward" };
    return button >= 0 && button < 5 ? names[button] : "Mouse";
}

// A gamepad button other than the face buttons, labelled per layout.
std::string OtherPadButtonLabel(PadButton button, FaceLayout layout) {
    const bool xbox = layout == FaceLayout::Xbox;
    const bool nintendo = layout == FaceLayout::Nintendo;
    switch (button) {
        case PadButton::Back:
            return xbox ? "View" : nintendo ? "-" : "Create";
        case PadButton::Guide:
            return xbox ? "Xbox" : nintendo ? "Home" : "PS";
        case PadButton::Start:
            return xbox ? "Menu" : nintendo ? "+" : "Options";
        case PadButton::LeftStick:
            return xbox || nintendo ? "Left Stick Click" : "L3";
        case PadButton::RightStick:
            return xbox || nintendo ? "Right Stick Click" : "R3";
        case PadButton::LeftShoulder:
            return xbox ? "LB" : nintendo ? "L" : "L1";
        case PadButton::RightShoulder:
            return xbox ? "RB" : nintendo ? "R" : "R1";
        case PadButton::DpadUp:
            return "D-pad Up";
        case PadButton::DpadDown:
            return "D-pad Down";
        case PadButton::DpadLeft:
            return "D-pad Left";
        case PadButton::DpadRight:
            return "D-pad Right";
        default:
            return "Button";
    }
}

std::string TriggerLabel(PadAxis axis, FaceLayout layout) {
    const bool left = axis == PadAxis::LeftTrigger;
    switch (layout) {
        case FaceLayout::Xbox:
            return left ? "LT" : "RT";
        case FaceLayout::Nintendo:
            return left ? "ZL" : "ZR";
        case FaceLayout::PlayStation:
            return left ? "L2" : "R2";
    }
    return {};
}

// Builds a profile one binding at a time.
struct ProfileBuilder {
    Profile profile;
    ProfileBuilder& Bind(Action action, std::initializer_list<PhysicalInput> inputs) {
        for (const PhysicalInput& input : inputs) {
            profile.Of(action).push_back(input);
        }
        return *this;
    }
};

Profile BuildN64Keyboard() {
    ProfileBuilder b;
    b.Bind(Action::N64A, { KeyInput(kX) })
        .Bind(Action::N64B, { KeyInput(kC) })
        .Bind(Action::N64L, { KeyInput(kE) })
        .Bind(Action::N64R, { KeyInput(kR) })
        .Bind(Action::N64Z, { KeyInput(kZ) })
        .Bind(Action::N64Start, { KeyInput(kSpace) })
        .Bind(Action::N64DUp, { KeyInput(kT) })
        .Bind(Action::N64DDown, { KeyInput(kG) })
        .Bind(Action::N64DLeft, { KeyInput(kF) })
        .Bind(Action::N64DRight, { KeyInput(kH) })
        .Bind(Action::N64CUp, { KeyInput(kArrowUp) })
        .Bind(Action::N64CDown, { KeyInput(kArrowDown) })
        .Bind(Action::N64CLeft, { KeyInput(kArrowLeft) })
        .Bind(Action::N64CRight, { KeyInput(kArrowRight) })
        .Bind(Action::N64StickUp, { KeyInput(kW) })
        .Bind(Action::N64StickDown, { KeyInput(kS) })
        .Bind(Action::N64StickLeft, { KeyInput(kA) })
        .Bind(Action::N64StickRight, { KeyInput(kD) });
    return b.profile;
}

Profile BuildN64Gamepad() {
    ProfileBuilder b;
    b.Bind(Action::N64A, { PadInput(PadButton::South) })
        .Bind(Action::N64B, { PadInput(PadButton::East) })
        .Bind(Action::N64L, { PadInput(PadButton::LeftShoulder) })
        .Bind(Action::N64R, { AxisInput(PadAxis::RightTrigger, 1) })
        .Bind(Action::N64Z, { AxisInput(PadAxis::LeftTrigger, 1) })
        .Bind(Action::N64Start, { PadInput(PadButton::Start) })
        .Bind(Action::N64DUp, { PadInput(PadButton::DpadUp) })
        .Bind(Action::N64DDown, { PadInput(PadButton::DpadDown) })
        .Bind(Action::N64DLeft, { PadInput(PadButton::DpadLeft) })
        .Bind(Action::N64DRight, { PadInput(PadButton::DpadRight) })
        .Bind(Action::N64CUp, { AxisInput(PadAxis::RightY, 1) })
        .Bind(Action::N64CDown, { AxisInput(PadAxis::RightY, -1) })
        .Bind(Action::N64CLeft, { AxisInput(PadAxis::RightX, -1) })
        .Bind(Action::N64CRight, { AxisInput(PadAxis::RightX, 1) })
        .Bind(Action::N64StickUp, { AxisInput(PadAxis::LeftY, 1) })
        .Bind(Action::N64StickDown, { AxisInput(PadAxis::LeftY, -1) })
        .Bind(Action::N64StickLeft, { AxisInput(PadAxis::LeftX, -1) })
        .Bind(Action::N64StickRight, { AxisInput(PadAxis::LeftX, 1) });
    return b.profile;
}

// frigate#500's action table. Lean's second key differs from the table's E,
// which Interact already holds in the aiming context.
Profile BuildModernKeyboard() {
    ProfileBuilder b;
    b.Bind(Action::MoveForward, { KeyInput(kW) })
        .Bind(Action::MoveBack, { KeyInput(kS) })
        .Bind(Action::StrafeLeft, { KeyInput(kA) })
        .Bind(Action::StrafeRight, { KeyInput(kD) })
        .Bind(Action::LookUp, { MotionInput(1, -1) })
        .Bind(Action::LookDown, { MotionInput(1, 1) })
        .Bind(Action::TurnLeft, { MotionInput(0, -1) })
        .Bind(Action::TurnRight, { MotionInput(0, 1) })
        .Bind(Action::Aim, { MouseInput(2) })
        .Bind(Action::Fire, { MouseInput(0) })
        .Bind(Action::Crouch, { KeyInput(kCtrl) })
        .Bind(Action::Interact, { KeyInput(kR), KeyInput(kE) })
        .Bind(Action::WeaponNext, { WheelInput(WheelDir::Down) })
        .Bind(Action::WeaponPrev, { WheelInput(WheelDir::Up) })
        .Bind(Action::Pause, { KeyInput(kTab) })
        .Bind(Action::ZoomIn, { WheelInput(WheelDir::Up) })
        .Bind(Action::ZoomOut, { WheelInput(WheelDir::Down) })
        .Bind(Action::LeanLeft, { KeyInput(kQ) })
        .Bind(Action::LeanRight, { KeyInput(kX) })
        .Bind(Action::Detonate, { KeyInput(kT) })
        .Bind(Action::MenuUp, { KeyInput(kW), KeyInput(kArrowUp) })
        .Bind(Action::MenuDown, { KeyInput(kS), KeyInput(kArrowDown) })
        .Bind(Action::MenuLeft, { KeyInput(kA), KeyInput(kArrowLeft) })
        .Bind(Action::MenuRight, { KeyInput(kD), KeyInput(kArrowRight) })
        .Bind(Action::MenuSelect, { KeyInput(kSpace), KeyInput(kEnter) })
        .Bind(Action::MenuBack, { KeyInput(kBackspace) });
    return b.profile;
}

// Previous Weapon has no default: the table's "hold North and fire" is a chord
// the Modern input synthesis adds.
Profile BuildModernGamepad() {
    ProfileBuilder b;
    b.Bind(Action::MoveForward, { AxisInput(PadAxis::LeftY, 1) })
        .Bind(Action::MoveBack, { AxisInput(PadAxis::LeftY, -1) })
        .Bind(Action::StrafeLeft, { AxisInput(PadAxis::LeftX, -1) })
        .Bind(Action::StrafeRight, { AxisInput(PadAxis::LeftX, 1) })
        .Bind(Action::LookUp, { AxisInput(PadAxis::RightY, 1) })
        .Bind(Action::LookDown, { AxisInput(PadAxis::RightY, -1) })
        .Bind(Action::TurnLeft, { AxisInput(PadAxis::RightX, -1) })
        .Bind(Action::TurnRight, { AxisInput(PadAxis::RightX, 1) })
        .Bind(Action::Aim, { PadInput(PadButton::LeftShoulder), AxisInput(PadAxis::LeftTrigger, 1) })
        .Bind(Action::Fire, { PadInput(PadButton::RightShoulder), AxisInput(PadAxis::RightTrigger, 1) })
        .Bind(Action::Crouch, { PadInput(PadButton::LeftStick) })
        .Bind(Action::Interact, { PadInput(PadButton::West), PadInput(PadButton::South) })
        .Bind(Action::WeaponNext, { PadInput(PadButton::North) })
        .Bind(Action::Pause, { PadInput(PadButton::Start) })
        .Bind(Action::ZoomIn, { AxisInput(PadAxis::LeftY, 1) })
        .Bind(Action::ZoomOut, { AxisInput(PadAxis::LeftY, -1) })
        .Bind(Action::LeanLeft, { PadInput(PadButton::DpadLeft) })
        .Bind(Action::LeanRight, { PadInput(PadButton::DpadRight) })
        .Bind(Action::Detonate, { PadInput(PadButton::RightStick) })
        .Bind(Action::MenuUp, { AxisInput(PadAxis::LeftY, 1), PadInput(PadButton::DpadUp) })
        .Bind(Action::MenuDown, { AxisInput(PadAxis::LeftY, -1), PadInput(PadButton::DpadDown) })
        .Bind(Action::MenuLeft, { AxisInput(PadAxis::LeftX, -1), PadInput(PadButton::DpadLeft) })
        .Bind(Action::MenuRight, { AxisInput(PadAxis::LeftX, 1), PadInput(PadButton::DpadRight) })
        .Bind(Action::MenuSelect, { PadInput(PadButton::South) })
        .Bind(Action::MenuBack, { PadInput(PadButton::East) });
    return b.profile;
}

size_t ModeIndex(BindingMode mode) {
    return mode == BindingMode::Modern ? 0 : 1;
}

size_t DeviceIndex(ProfileDevice device) {
    return device == ProfileDevice::KeyboardMouse ? 0 : 1;
}

bool Contains(const std::vector<PhysicalInput>& inputs, const PhysicalInput& input) {
    return std::find(inputs.begin(), inputs.end(), input) != inputs.end();
}

// The actions of the target's mode other than `action` that hold `input` in a
// context `action` shares.
std::vector<Conflict> FindConflicts(const Profile& profile, BindingMode mode, Action action,
                                    const PhysicalInput& input) {
    std::vector<Conflict> conflicts;
    for (const Action other : ActionsOf(mode)) {
        if (other == action || !ContextsOverlap(action, other)) {
            continue;
        }
        const auto& held = profile.Of(other);
        const auto found = std::find(held.begin(), held.end(), input);
        if (found != held.end()) {
            conflicts.push_back({ other, static_cast<size_t>(found - held.begin()) });
        }
    }
    return conflicts;
}

// Config tokens: k<scancode> b<button> w<direction> m<axis><sign> p<button> a<axis><sign>.
std::string InputToken(const PhysicalInput& input) {
    const auto sign = [&input] { return std::string(input.sign < 0 ? "-" : "+"); };
    switch (input.kind) {
        case InputKind::Key:
            return "k" + std::to_string(input.code);
        case InputKind::MouseButton:
            return "b" + std::to_string(input.code);
        case InputKind::MouseWheel:
            return "w" + std::to_string(input.code);
        case InputKind::MouseMotion:
            return "m" + std::to_string(input.code) + sign();
        case InputKind::PadButton:
            return "p" + std::to_string(input.code);
        case InputKind::PadAxis:
            return "a" + std::to_string(input.code) + sign();
    }
    return {};
}

bool ParseInt(const std::string& text, int& out) {
    if (text.empty() || text.size() > 5) {
        return false;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

bool ParseToken(const std::string& token, PhysicalInput& out) {
    if (token.size() < 2) {
        return false;
    }
    std::string number = token.substr(1);
    int sign = 0;
    const char kind = token[0];
    if (kind == 'm' || kind == 'a') {
        if (number.back() != '+' && number.back() != '-') {
            return false;
        }
        sign = number.back() == '-' ? -1 : 1;
        number.pop_back();
    }
    int code = 0;
    if (!ParseInt(number, code)) {
        return false;
    }
    switch (kind) {
        case 'k':
            out = KeyInput(code);
            return code > 0;
        case 'b':
            out = MouseInput(code);
            return code < 5;
        case 'w':
            out = WheelInput(static_cast<WheelDir>(code));
            return code < 4;
        case 'm':
            out = MotionInput(code, sign);
            return code < 2;
        case 'p':
            out = PadInput(static_cast<PadButton>(code));
            return code < static_cast<int>(PadButton::Count);
        case 'a':
            out = AxisInput(static_cast<PadAxis>(code), sign);
            return code < static_cast<int>(PadAxis::Count);
        default:
            return false;
    }
}

std::vector<std::string> Split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            return parts;
        }
        start = end + 1;
    }
}

const Action* FindActionByKey(const std::string& key) {
    for (const ActionInfo& info : kActions) {
        if (key == info.key) {
            return &info.id;
        }
    }
    return nullptr;
}

const char* ModeKey(BindingMode mode) {
    return mode == BindingMode::Modern ? "modern" : "n64";
}

const char* DeviceKey(ProfileDevice device) {
    return device == ProfileDevice::KeyboardMouse ? "kbm" : "pad";
}

// Drops the inputs a profile can't hold, then fills in any required action left bare.
void Repair(Profile& profile, BindingMode mode, ProfileDevice device) {
    const Profile& defaults = DefaultProfile(mode, device);
    for (const Action action : ActionsOf(mode)) {
        auto& inputs = profile.Of(action);
        std::vector<PhysicalInput> kept;
        for (const PhysicalInput& input : inputs) {
            if (Accepts(action, input, device) && !IsReserved(input) && !Contains(kept, input)) {
                kept.push_back(input);
            }
        }
        inputs = std::move(kept);
        if (inputs.empty() && Info(action).required) {
            inputs = defaults.Of(action);
        }
    }
    // Actions of the other mode hold nothing.
    for (size_t i = 0; i < kActionCount; i++) {
        const auto action = static_cast<Action>(i);
        if (Info(action).mode != mode) {
            profile.Of(action).clear();
        }
    }
}

} // namespace

PhysicalInput KeyInput(int scancode) {
    return { InputKind::Key, static_cast<int16_t>(scancode), 0 };
}

PhysicalInput MouseInput(int button) {
    return { InputKind::MouseButton, static_cast<int16_t>(button), 0 };
}

PhysicalInput WheelInput(WheelDir direction) {
    return { InputKind::MouseWheel, static_cast<int16_t>(direction), 0 };
}

PhysicalInput MotionInput(int axis, int sign) {
    return { InputKind::MouseMotion, static_cast<int16_t>(axis), static_cast<int8_t>(sign < 0 ? -1 : 1) };
}

PhysicalInput PadInput(PadButton button) {
    return { InputKind::PadButton, static_cast<int16_t>(button), 0 };
}

PhysicalInput AxisInput(PadAxis axis, int sign) {
    return { InputKind::PadAxis, static_cast<int16_t>(axis), static_cast<int8_t>(sign < 0 ? -1 : 1) };
}

std::string FaceButtonLabel(PadButton button, FaceLayout layout) {
    static const char* labels[3][4] = {
        // South, East, West, North
        { "A", "B", "X", "Y" },
        { "B", "A", "Y", "X" },
        { "Cross", "Circle", "Square", "Triangle" },
    };
    const auto index = static_cast<size_t>(button);
    if (index > static_cast<size_t>(PadButton::North)) {
        return OtherPadButtonLabel(button, layout);
    }
    return labels[static_cast<size_t>(layout)][index];
}

std::string InputLabel(const PhysicalInput& input, FaceLayout layout) {
    switch (input.kind) {
        case InputKind::Key:
            return KeyName(input.code);
        case InputKind::MouseButton:
            return MouseButtonName(input.code);
        case InputKind::MouseWheel: {
            static const char* names[] = { "Wheel Up", "Wheel Down", "Wheel Left", "Wheel Right" };
            return input.code >= 0 && input.code < 4 ? names[input.code] : "Wheel";
        }
        case InputKind::MouseMotion:
            return input.code == 0 ? (input.sign > 0 ? "Mouse Right" : "Mouse Left")
                                   : (input.sign > 0 ? "Mouse Down" : "Mouse Up");
        case InputKind::PadButton:
            return FaceButtonLabel(static_cast<PadButton>(input.code), layout);
        case InputKind::PadAxis: {
            const auto axis = static_cast<PadAxis>(input.code);
            switch (axis) {
                case PadAxis::LeftX:
                case PadAxis::LeftY:
                    return StickName("Left", input.code - static_cast<int>(PadAxis::LeftX), input.sign);
                case PadAxis::RightX:
                case PadAxis::RightY:
                    return StickName("Right", input.code - static_cast<int>(PadAxis::RightX), input.sign);
                default:
                    return TriggerLabel(axis, layout);
            }
        }
    }
    return {};
}

const ActionInfo& Info(Action action) {
    return kActions[static_cast<size_t>(action)];
}

const std::vector<Action>& ActionsOf(BindingMode mode) {
    static const std::vector<Action> lists[2] = {
        [] {
            std::vector<Action> list;
            for (const ActionInfo& info : kActions) {
                if (info.mode == BindingMode::Modern) {
                    list.push_back(info.id);
                }
            }
            return list;
        }(),
        [] {
            std::vector<Action> list;
            for (const ActionInfo& info : kActions) {
                if (info.mode == BindingMode::N64) {
                    list.push_back(info.id);
                }
            }
            return list;
        }(),
    };
    return lists[ModeIndex(mode)];
}

std::vector<Action> ActionsOf(Section section) {
    std::vector<Action> list;
    for (const ActionInfo& info : kActions) {
        if (info.section == section) {
            list.push_back(info.id);
        }
    }
    return list;
}

BindingMode ModeOf(Section section) {
    return section >= Section::ModernMove ? BindingMode::Modern : BindingMode::N64;
}

const char* SectionName(Section section) {
    return kSectionNames[static_cast<size_t>(section)];
}

const std::vector<Section>& SectionsOf(BindingMode mode) {
    static const std::vector<Section> modern = { Section::ModernMove, Section::ModernLook, Section::ModernCombat,
                                                 Section::ModernSystem, Section::ModernMenu };
    static const std::vector<Section> n64 = { Section::N64Face, Section::N64DPad, Section::N64CButtons,
                                              Section::N64Stick };
    return mode == BindingMode::Modern ? modern : n64;
}

bool Accepts(Action action, const PhysicalInput& input, ProfileDevice device) {
    const ActionInfo& info = Info(action);
    switch (input.kind) {
        case InputKind::Key:
        case InputKind::MouseButton:
        case InputKind::MouseWheel:
            return device == ProfileDevice::KeyboardMouse;
        case InputKind::MouseMotion:
            return device == ProfileDevice::KeyboardMouse && info.acceptsMotion && (input.code == 0 || input.code == 1);
        case InputKind::PadButton:
            return device == ProfileDevice::Gamepad && input.code < static_cast<int>(PadButton::Count);
        case InputKind::PadAxis:
            return device == ProfileDevice::Gamepad && input.code < static_cast<int>(PadAxis::Count) &&
                   (input.code < static_cast<int>(PadAxis::LeftTrigger) || input.sign > 0);
    }
    return false;
}

bool IsReserved(const PhysicalInput& input) {
    return input.kind == InputKind::Key && (input.code == kScancodeEscape || input.code == kScancodeF2);
}

const Profile& DefaultProfile(BindingMode mode, ProfileDevice device) {
    static const Profile profiles[2][2] = {
        { BuildModernKeyboard(), BuildModernGamepad() },
        { BuildN64Keyboard(), BuildN64Gamepad() },
    };
    return profiles[ModeIndex(mode)][DeviceIndex(device)];
}

std::string Describe(BindCheck::Status status) {
    switch (status) {
        case BindCheck::Status::Ok:
            return {};
        case BindCheck::Status::Reserved:
            return "Esc opens the menu and F2 toggles mouse capture, so neither can be bound.";
        case BindCheck::Status::Invalid:
            return "That input can't be used for this action.";
        case BindCheck::Status::AlreadyBound:
            return "This action already uses that input.";
        case BindCheck::Status::Conflict:
            return "That input is already used by another action.";
    }
    return {};
}

BindingStore::BindingStore() {
    for (PortData& port : mPorts) {
        for (const BindingMode mode : { BindingMode::Modern, BindingMode::N64 }) {
            for (const ProfileDevice device : { ProfileDevice::KeyboardMouse, ProfileDevice::Gamepad }) {
                port.profiles[ModeIndex(mode)][DeviceIndex(device)] = DefaultProfile(mode, device);
            }
        }
    }
}

BindingStore::PortData& BindingStore::Port(int port) {
    return mPorts[static_cast<size_t>(std::clamp(port, 0, 3))];
}

const BindingStore::PortData& BindingStore::Port(int port) const {
    return mPorts[static_cast<size_t>(std::clamp(port, 0, 3))];
}

BindingMode BindingStore::Mode(int port) const {
    return Port(port).mode;
}

void BindingStore::SetMode(int port, BindingMode mode) {
    if (Port(port).mode != mode) {
        Port(port).mode = mode;
        mGeneration++;
    }
}

const Profile& BindingStore::Get(const BindTarget& target) const {
    return Port(target.port).profiles[ModeIndex(target.mode)][DeviceIndex(target.device)];
}

Profile& BindingStore::Edit(const BindTarget& target) {
    PortData& port = Port(target.port);
    port.touched[ModeIndex(target.mode)][DeviceIndex(target.device)] = true;
    mGeneration++;
    return port.profiles[ModeIndex(target.mode)][DeviceIndex(target.device)];
}

BindCheck BindingStore::Check(const BindTarget& target, Action action, const PhysicalInput& input, size_t slot) const {
    BindCheck check;
    if (Info(action).mode != target.mode || !Accepts(action, input, target.device)) {
        check.status = IsReserved(input) ? BindCheck::Status::Reserved : BindCheck::Status::Invalid;
        return check;
    }
    if (IsReserved(input)) {
        check.status = BindCheck::Status::Reserved;
        return check;
    }
    const Profile& profile = Get(target);
    if (Contains(profile.Of(action), input)) {
        check.status = BindCheck::Status::AlreadyBound;
        return check;
    }
    check.conflicts = FindConflicts(profile, target.mode, action, input);
    if (check.conflicts.empty()) {
        return check;
    }
    check.status = BindCheck::Status::Conflict;
    check.canReplace = std::all_of(check.conflicts.begin(), check.conflicts.end(), [&profile](const Conflict& c) {
        return !Info(c.action).required || profile.Of(c.action).size() > 1;
    });
    const auto& own = profile.Of(action);
    if (check.conflicts.size() == 1 && slot < own.size()) {
        const Action other = check.conflicts[0].action;
        const PhysicalInput& old = own[slot];
        bool clashes = !Accepts(other, old, target.device) || Contains(profile.Of(other), old);
        if (!clashes) {
            // The old input moving to the other action must not clash there.
            for (const Conflict& c : FindConflicts(profile, target.mode, other, old)) {
                clashes = clashes || c.action != action;
            }
        }
        check.canSwap = !clashes;
    }
    return check;
}

BindStatus BindingStore::Bind(const BindTarget& target, Action action, const PhysicalInput& input, size_t slot,
                              const ConflictChoice* choice) {
    const BindCheck check = Check(target, action, input, slot);
    switch (check.status) {
        case BindCheck::Status::Reserved:
            return BindStatus::Reserved;
        case BindCheck::Status::Invalid:
            return BindStatus::Invalid;
        case BindCheck::Status::AlreadyBound:
            return BindStatus::AlreadyBound;
        case BindCheck::Status::Conflict:
            if (choice == nullptr) {
                return BindStatus::NeedsDecision;
            }
            if (*choice == ConflictChoice::Cancel) {
                return BindStatus::Cancelled;
            }
            if ((*choice == ConflictChoice::Replace && !check.canReplace) ||
                (*choice == ConflictChoice::Swap && !check.canSwap)) {
                return BindStatus::Refused;
            }
            break;
        case BindCheck::Status::Ok:
            break;
    }

    Profile& profile = Edit(target);
    auto& own = profile.Of(action);
    const bool replacing = slot < own.size();
    const PhysicalInput old = replacing ? own[slot] : PhysicalInput{};
    if (check.status == BindCheck::Status::Conflict) {
        if (*choice == ConflictChoice::Swap) {
            profile.Of(check.conflicts[0].action)[check.conflicts[0].slot] = old;
        } else {
            for (const Conflict& conflict : check.conflicts) {
                auto& held = profile.Of(conflict.action);
                held.erase(std::remove(held.begin(), held.end(), input), held.end());
            }
        }
    }
    if (replacing) {
        own[slot] = input;
    } else {
        own.push_back(input);
    }
    return BindStatus::Bound;
}

UnbindStatus BindingStore::Unbind(const BindTarget& target, Action action, size_t slot) {
    const auto& current = Get(target).Of(action);
    if (slot >= current.size()) {
        return UnbindStatus::NotBound;
    }
    if (Info(action).required && current.size() == 1) {
        return UnbindStatus::RequiredAction;
    }
    auto& inputs = Edit(target).Of(action);
    inputs.erase(inputs.begin() + static_cast<std::ptrdiff_t>(slot));
    return UnbindStatus::Removed;
}

void BindingStore::ResetSection(const BindTarget& target, Section section) {
    if (ModeOf(section) != target.mode) {
        return;
    }
    Profile& profile = Edit(target);
    const Profile& defaults = DefaultProfile(target.mode, target.device);
    for (const Action action : ActionsOf(section)) {
        profile.Of(action) = defaults.Of(action);
    }
}

void BindingStore::ResetProfile(const BindTarget& target) {
    Edit(target) = DefaultProfile(target.mode, target.device);
}

void BindingStore::ResetAll() {
    *this = BindingStore();
    mGeneration++;
}

void BindingStore::SetProfile(const BindTarget& target, const Profile& profile) {
    Profile repaired = profile;
    Repair(repaired, target.mode, target.device);
    Edit(target) = repaired;
}

std::string BindingStore::Serialize(int port) const {
    const PortData& data = Port(port);
    std::string text = std::string("v1;mode=") + ModeKey(data.mode);
    for (const BindingMode mode : { BindingMode::N64, BindingMode::Modern }) {
        for (const ProfileDevice device : { ProfileDevice::KeyboardMouse, ProfileDevice::Gamepad }) {
            if (!data.touched[ModeIndex(mode)][DeviceIndex(device)]) {
                continue;
            }
            const Profile& profile = data.profiles[ModeIndex(mode)][DeviceIndex(device)];
            text += std::string(";") + ModeKey(mode) + "." + DeviceKey(device) + "=";
            bool first = true;
            for (const Action action : ActionsOf(mode)) {
                text += (first ? "" : "|") + std::string(Info(action).key) + ":";
                first = false;
                const auto& inputs = profile.Of(action);
                for (size_t i = 0; i < inputs.size(); i++) {
                    text += (i > 0 ? "," : "") + InputToken(inputs[i]);
                }
            }
        }
    }
    return text;
}

bool BindingStore::Deserialize(int port, const std::string& text) {
    const std::vector<std::string> fields = Split(text, ';');
    if (fields.empty() || fields[0] != "v1") {
        return false;
    }
    PortData parsed;
    for (const BindingMode mode : { BindingMode::Modern, BindingMode::N64 }) {
        for (const ProfileDevice device : { ProfileDevice::KeyboardMouse, ProfileDevice::Gamepad }) {
            parsed.profiles[ModeIndex(mode)][DeviceIndex(device)] = DefaultProfile(mode, device);
        }
    }
    for (size_t i = 1; i < fields.size(); i++) {
        const size_t equals = fields[i].find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string name = fields[i].substr(0, equals);
        const std::string value = fields[i].substr(equals + 1);
        if (name == "mode") {
            parsed.mode = value == "modern" ? BindingMode::Modern : value == "n64" ? BindingMode::N64 : kDefaultMode;
            continue;
        }
        for (const BindingMode mode : { BindingMode::Modern, BindingMode::N64 }) {
            for (const ProfileDevice device : { ProfileDevice::KeyboardMouse, ProfileDevice::Gamepad }) {
                if (name != std::string(ModeKey(mode)) + "." + DeviceKey(device)) {
                    continue;
                }
                Profile& profile = parsed.profiles[ModeIndex(mode)][DeviceIndex(device)];
                for (const std::string& entry : Split(value, '|')) {
                    const size_t colon = entry.find(':');
                    const Action* action =
                        colon == std::string::npos ? nullptr : FindActionByKey(entry.substr(0, colon));
                    if (action == nullptr || Info(*action).mode != mode) {
                        continue;
                    }
                    std::vector<PhysicalInput> inputs;
                    for (const std::string& token : Split(entry.substr(colon + 1), ',')) {
                        PhysicalInput input;
                        if (ParseToken(token, input)) {
                            inputs.push_back(input);
                        }
                    }
                    profile.Of(*action) = std::move(inputs);
                }
                Repair(profile, mode, device);
                parsed.touched[ModeIndex(mode)][DeviceIndex(device)] = true;
            }
        }
    }
    Port(port) = parsed;
    mGeneration++;
    return true;
}

BindingStore& Bindings() {
    static BindingStore store;
    return store;
}

Profile ProfileFromLegacy(const std::vector<LegacyBinding>& legacy, ProfileDevice device) {
    Profile profile;
    bool any = false;
    for (const LegacyBinding& binding : legacy) {
        if (Info(binding.action).mode != BindingMode::N64 || !Accepts(binding.action, binding.input, device) ||
            IsReserved(binding.input)) {
            continue;
        }
        auto& inputs = profile.Of(binding.action);
        if (!Contains(inputs, binding.input)) {
            inputs.push_back(binding.input);
            any = true;
        }
    }
    if (!any) {
        return DefaultProfile(BindingMode::N64, device);
    }
    Repair(profile, BindingMode::N64, device);
    return profile;
}

} // namespace Frigate::Platform::Input
