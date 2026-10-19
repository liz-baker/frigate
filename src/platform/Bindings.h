#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The binding store behind the Controls tab: what each action is bound to, per
// port, per device type and per mode. Pure logic with no libultraship or SDL
// types, so it is unit-tested on its own; BindingsApply.h connects it to
// libultraship. Model and persistence format: docs/bindings.md.
namespace Frigate::Platform::Input {

enum class BindingMode : uint8_t { Modern, N64 };

// Which kind of device a profile is for. Each port keeps one profile per mode
// per device type, so switching either never touches the others.
enum class ProfileDevice : uint8_t { KeyboardMouse, Gamepad };

// How a gamepad's face buttons are printed.
enum class FaceLayout : uint8_t { Xbox, Nintendo, PlayStation };

// Where an action is live. Two actions conflict only when they share a
// context, which is how the wheel can mean weapon change and zoom.
using ContextMask = uint8_t;
inline constexpr ContextMask kContextMenu = 1;
inline constexpr ContextMask kContextOnFoot = 2;
inline constexpr ContextMask kContextAiming = 4;
inline constexpr ContextMask kContextAll = kContextMenu | kContextOnFoot | kContextAiming;

enum class InputKind : uint8_t { Key, MouseButton, MouseWheel, MouseMotion, PadButton, PadAxis };

// Face buttons are by position (South is Xbox A, Nintendo B, PlayStation Cross),
// so a binding keeps its place when the layout labels change.
enum class PadButton : uint8_t {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count
};

// Stick axes are positive to the right and up; a trigger is positive only.
enum class PadAxis : uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

enum class WheelDir : uint8_t { Up, Down, Left, Right };

// code by kind:
//   Key          the scancode libultraship uses (PS/2 set 1; Esc 1, F2 60).
//   MouseButton  0 left, 1 middle, 2 right, 3 back, 4 forward.
//   MouseWheel   a WheelDir.
//   MouseMotion  0 for X, 1 for Y; sign + is right / down on screen.
//   PadButton    a PadButton.
//   PadAxis      a PadAxis; sign + or -.
struct PhysicalInput {
    InputKind kind = InputKind::Key;
    int16_t code = 0;
    int8_t sign = 0;

    bool operator==(const PhysicalInput& other) const {
        return kind == other.kind && code == other.code && sign == other.sign;
    }
    bool operator!=(const PhysicalInput& other) const {
        return !(*this == other);
    }
    bool operator<(const PhysicalInput& other) const {
        if (kind != other.kind) {
            return kind < other.kind;
        }
        return code != other.code ? code < other.code : sign < other.sign;
    }
};

PhysicalInput KeyInput(int scancode);
PhysicalInput MouseInput(int button);
PhysicalInput WheelInput(WheelDir direction);
PhysicalInput MotionInput(int axis, int sign);
PhysicalInput PadInput(PadButton button);
PhysicalInput AxisInput(PadAxis axis, int sign);

inline constexpr int kScancodeEscape = 1;
inline constexpr int kScancodeF2 = 60;

// What the input is called on screen: "Mouse Left", "Key W", and for a gamepad
// the label printed on it under `layout`, e.g. South is "A" / "B" / "Cross".
std::string InputLabel(const PhysicalInput& input, FaceLayout layout);

// A face button's printed label: North is Y / X / Triangle, South A / B / Cross,
// East B / A / Circle, West X / Y / Square for Xbox / Nintendo / PlayStation.
std::string FaceButtonLabel(PadButton button, FaceLayout layout);

enum class Section : uint8_t {
    N64Face,
    N64DPad,
    N64CButtons,
    N64Stick,
    ModernMove,
    ModernLook,
    ModernCombat,
    ModernSystem,
    ModernMenu,
    Count
};

enum class Action : uint8_t {
    // N64 mode: one action per button, and the four directions of the stick.
    N64A,
    N64B,
    N64Start,
    N64L,
    N64R,
    N64Z,
    N64DUp,
    N64DDown,
    N64DLeft,
    N64DRight,
    N64CUp,
    N64CDown,
    N64CLeft,
    N64CRight,
    N64StickUp,
    N64StickDown,
    N64StickLeft,
    N64StickRight,
    // Modern mode. Synthesizing these into gameplay is frigate#500.
    MoveForward,
    MoveBack,
    StrafeLeft,
    StrafeRight,
    LookUp,
    LookDown,
    TurnLeft,
    TurnRight,
    Aim,
    Fire,
    Crouch,
    // Reload and interact are the same B tap in the game, so one action.
    Interact,
    WeaponNext,
    WeaponPrev,
    Pause,
    ZoomIn,
    ZoomOut,
    LeanLeft,
    LeanRight,
    Detonate,
    MenuUp,
    MenuDown,
    MenuLeft,
    MenuRight,
    MenuSelect,
    MenuBack,
    Count
};

inline constexpr size_t kActionCount = static_cast<size_t>(Action::Count);

struct ActionInfo {
    Action id;
    BindingMode mode;
    Section section;
    // Config key, stable across releases.
    const char* key;
    const char* label;
    ContextMask contexts;
    // Pause and menu navigation: the last binding can't be removed.
    bool required;
    // Takes mouse motion, which only the look and turn actions understand.
    bool acceptsMotion;
};

const ActionInfo& Info(Action action);

// The actions of a mode in display order, and of one section.
const std::vector<Action>& ActionsOf(BindingMode mode);
std::vector<Action> ActionsOf(Section section);
BindingMode ModeOf(Section section);
const char* SectionName(Section section);
const std::vector<Section>& SectionsOf(BindingMode mode);

// Whether `input` can be bound to `action` at all on a `device` profile: a
// keyboard or mouse input only on the keyboard profile, a gamepad one only on
// the gamepad profile, mouse motion only to look and turn.
bool Accepts(Action action, const PhysicalInput& input, ProfileDevice device);

// Esc opens the menu and F2 toggles mouse capture, so neither can be bound.
bool IsReserved(const PhysicalInput& input);

// What an action is bound to; an action can have several bindings.
class Profile {
  public:
    const std::vector<PhysicalInput>& Of(Action action) const {
        return mBindings[static_cast<size_t>(action)];
    }
    std::vector<PhysicalInput>& Of(Action action) {
        return mBindings[static_cast<size_t>(action)];
    }
    bool operator==(const Profile& other) const {
        return mBindings == other.mBindings;
    }
    bool operator!=(const Profile& other) const {
        return !(*this == other);
    }

  private:
    std::array<std::vector<PhysicalInput>, kActionCount> mBindings;
};

// The default bindings of a mode on a device type.
const Profile& DefaultProfile(BindingMode mode, ProfileDevice device);

// The mode a port starts in.
// TODO(https://github.com/liz-baker/frigate/issues/500): Modern becomes the default once its actions drive gameplay;
// until then only N64 mode has an effect.
inline constexpr BindingMode kDefaultMode = BindingMode::N64;

// Which profile an edit applies to.
struct BindTarget {
    int port = 0;
    BindingMode mode = BindingMode::N64;
    ProfileDevice device = ProfileDevice::KeyboardMouse;

    bool operator==(const BindTarget& other) const {
        return port == other.port && mode == other.mode && device == other.device;
    }
};

inline constexpr size_t kNewSlot = static_cast<size_t>(-1);

struct Conflict {
    Action action;
    size_t slot;
};

struct BindCheck {
    enum class Status : uint8_t {
        // Free to bind.
        Ok,
        // Esc or F2.
        Reserved,
        // Not valid for this action or device type.
        Invalid,
        // The action already has it in another slot.
        AlreadyBound,
        // Bound to other actions in a shared context: ask swap / replace / cancel.
        Conflict
    };
    Status status = Status::Ok;
    std::vector<Conflict> conflicts;
    // Replace takes the input from every conflicting action; false when that
    // would leave a required one unbound.
    bool canReplace = false;
    // Swap gives the action it conflicts with the input being replaced; only
    // for a single conflict and an existing slot the other action accepts.
    bool canSwap = false;
};

enum class ConflictChoice : uint8_t { Swap, Replace, Cancel };

enum class BindStatus : uint8_t {
    Bound,
    Reserved,
    Invalid,
    AlreadyBound,
    // A conflict and no choice given; ask, then call again with the choice.
    NeedsDecision,
    // The choice can't be carried out (see BindCheck::canReplace, canSwap).
    Refused,
    Cancelled
};

enum class UnbindStatus : uint8_t { Removed, NotBound, RequiredAction };

// Human-readable lines for the editor.
std::string Describe(BindCheck::Status status);

class BindingStore {
  public:
    BindingStore();

    BindingMode Mode(int port) const;
    void SetMode(int port, BindingMode mode);

    const Profile& Get(const BindTarget& target) const;

    // What binding `input` to `action` (in `slot`, kNewSlot to add one) would do.
    BindCheck Check(const BindTarget& target, Action action, const PhysicalInput& input, size_t slot) const;

    // Binds it. With a conflict, no `choice` returns NeedsDecision and changes
    // nothing; Cancel returns Cancelled.
    BindStatus Bind(const BindTarget& target, Action action, const PhysicalInput& input, size_t slot,
                    const ConflictChoice* choice = nullptr);

    // Removes a binding; the last binding of a required action stays.
    UnbindStatus Unbind(const BindTarget& target, Action action, size_t slot);

    // Back to the defaults for one section, one profile, or everything.
    void ResetSection(const BindTarget& target, Section section);
    void ResetProfile(const BindTarget& target);
    void ResetAll();

    // Replaces a profile wholesale, e.g. with bindings migrated from libultraship;
    // inputs the profile can't hold and required actions left empty are repaired.
    void SetProfile(const BindTarget& target, const Profile& profile);

    // Bumped by every change, so the libultraship side knows when to re-apply.
    uint32_t Generation() const {
        return mGeneration;
    }

    // One port as a config string and back. Profiles, and actions inside a
    // profile, the string leaves out read as defaults. False (and the port
    // unchanged) when the string isn't a binding store; see docs/bindings.md.
    std::string Serialize(int port) const;
    bool Deserialize(int port, const std::string& text);

  private:
    struct PortData {
        BindingMode mode = kDefaultMode;
        // [mode][device]
        Profile profiles[2][2];
        // Whether the profile was set or edited; the others read as defaults.
        bool touched[2][2] = {};
    };

    PortData& Port(int port);
    const PortData& Port(int port) const;
    Profile& Edit(const BindTarget& target);

    std::array<PortData, 4> mPorts;
    uint32_t mGeneration = 0;
};

// The store the menu edits and the game reads.
BindingStore& Bindings();

// One binding read from libultraship's own mappings, for migration.
struct LegacyBinding {
    Action action;
    PhysicalInput input;
};

// The profile for `device` built from `legacy`: each legacy binding that fits
// the profile, defaults for any required action left without one.
Profile ProfileFromLegacy(const std::vector<LegacyBinding>& legacy, ProfileDevice device);

} // namespace Frigate::Platform::Input
