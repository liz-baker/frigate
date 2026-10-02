#include "level/Watch.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "level/ModelHandle.h"
#include "level/WatchMenu.h"
#include "model/ModelInstance.h"
#include "model/Random.h"

using namespace Frigate;
using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float epsilon = 0.01f) {
    return std::fabs(a - b) < epsilon;
}

constexpr int32_t kSuitHand = 84;

// A table of 85 items: every one the defaults, but the last is the suit's
// hand, which has a model.
std::string Table() {
    std::string text;
    for (int i = 0; i < 85; i++) {
        text += "item\t" + std::to_string(i) + "\tITEM\t" + (i == kSuitHand ? "1" : "0") + "\t0\t0\t0x110\t" +
                (i == kSuitHand ? "models/chr/Csuit_lf_handZ" : "-") +
                "\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0"
                "\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\t0.0\t0\t1.0\t0\t0\n";
    }
    text += "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\n";
    return text;
}

// The suit's hand as a model with nothing to draw, and a clip with the
// twenty-one frames of the arm's raise.
class SuitHand final : public ModelFactory {
  public:
    std::shared_ptr<ModelHandle> CreateBody(const std::string&, const std::string&, bool) override {
        return nullptr;
    }
    std::shared_ptr<ModelHandle> CreateProp(const std::string&) override {
        return nullptr;
    }
    std::shared_ptr<Model::ModelInstance> CreateGun(const std::string& path) override {
        if (path != "models/chr/Csuit_lf_handZ") {
            return nullptr;
        }
        auto tree = std::make_shared<ModelTree>();
        ModelNode root;
        root.type = 0x02;
        tree->Nodes = { root };
        tree->MatrixCount = 1;
        tree->Switches = { kNoNode, kNoNode, kNoNode, kNoNode, kNoNode };
        return Model::ModelInstance::FromParts(path, tree, nullptr);
    }
    const AnimationClip* LoadAnimation(Model::ModelInstance&, const std::string& name) override {
        return name == "bond_watch" ? &clip : nullptr;
    }

    AnimationClip clip = [] {
        AnimationClip value;
        value.frameCount = 21;
        return value;
    }();
};

uint32_t Zero() {
    return 0;
}

// What the menu reads of the stage: two objectives and a briefing.
class FakeHost final : public WatchMenuHost {
  public:
    std::string_view Text(int32_t) const override {
        return "word\n";
    }
    float Health() const override {
        return 1.0f;
    }
    float Armour() const override {
        return 0.5f;
    }
    int32_t Difficulty() const override {
        return 0;
    }
    int32_t ObjectiveCount() const override {
        return 2;
    }
    int32_t ObjectiveDifficulty(int32_t) const override {
        return 0;
    }
    std::string_view ObjectiveText(int32_t) const override {
        return "an objective\n";
    }
    ObjectiveStatus ObjectiveStatusOf(int32_t) const override {
        return ObjectiveStatus::Incomplete;
    }
    bool AllObjectivesComplete() const override {
        return false;
    }
    std::string_view BriefingPage(int32_t) const override {
        return "a briefing\n";
    }
    bool PropListed(const InvItem&) const override {
        return false;
    }
    void AbortMission() override {
        aborted = true;
    }
    void EnterWatchMusic() override {
        entered++;
    }
    void LeaveWatchMusic() override {
        left++;
    }

    bool aborted = false;
    int entered = 0;
    int left = 0;
};

// A font whose glyphs are all 8 wide and 12 tall.
std::shared_ptr<Font> MakeFont() {
    auto font = std::make_shared<Font>();
    font->Kerning.fill(0);
    for (size_t i = 0; i < Font::kNumChars; i++) {
        FontChar glyph;
        glyph.index = static_cast<int32_t>(i);
        glyph.baseline = 2;
        glyph.height = 12;
        glyph.width = 8;
        glyph.Pixels.assign(8 * 12, 0xff);
        font->Chars.push_back(glyph);
    }
    return font;
}

struct Scene {
    SuitHand models;
    FakeHost host;
    PlayerOptions options;
    Inventory inventory;
    PlayerWeapons weapons;
    PlayerMove player;
    Hud hud;
    Watch watch;
    std::vector<int32_t> states = { 0 };
    bool sawLocked = false;
    bool lockedOutsideOpen = false;

    Scene() {
        weapons.SetModels(&models);
        weapons.Init(WeaponTable::Parse(Table()), &inventory, Zero);
        hud.SetFonts(MakeFont(), MakeFont());
        watch.Attach(&player, &weapons, &hud, &models);
        watch.AttachMenu(&host, &options, &inventory);
        watch.Reset(0);
    }

    // One frame of the player's tick: the input, the watch, and the guns.
    void Frame(bool start) {
        watch.SetTiming({ 1, watch.ControlsLocked() ? 0 : 1, watch.ControlsLocked() ? 0.0f : 1.0f });
        MoveInput input;
        input.pressed = start ? Buttons::Start : 0;
        watch.SetInput(input);
        watch.OnInput(start, false);
        if (watch.State() != 0) {
            watch.Tick();
        }
        GunTickInput gun;
        gun.clockTimer = watch.ControlsLocked() ? 0 : 1;
        weapons.TickGameplay(gun, nullptr);
        if (watch.State() != states.back()) {
            states.push_back(watch.State());
        }
        const bool locked = watch.ControlsLocked();
        sawLocked = sawLocked || locked;
        lockedOutsideOpen =
            lockedOutsideOpen || (locked && watch.State() != WatchState::Open && watch.State() != WatchState::Closing);
    }
};

void TestTheWatchOpensAndCloses() {
    Scene scene;
    scene.watch.UpdateZoom(60.0f);
    Check(scene.watch.ZoomFovY() == 60.0f, "the view starts at the standard field of view");
    scene.Frame(true);
    Check(scene.watch.State() == WatchState::LowerWeapon, "a Start press begins the pause");

    bool closing = false;
    float lowestPitch = 0.0f;
    float smallestFov = 60.0f;
    for (int i = 0; i < 3000 && (scene.watch.State() != WatchState::Closed || i < 5); i++) {
        const bool open = scene.watch.State() == WatchState::Open;
        // Start again once the watch is up: the menu asks for the watch to close
        // a frame later.
        scene.Frame(open && !closing);
        closing = closing || open;
        lowestPitch = std::fmin(lowestPitch, scene.player.Verta());
        smallestFov = std::fmin(smallestFov, scene.watch.ZoomFovY());
    }

    const std::vector<int32_t> expected = { 0, 1, 2, 3, 4, 5, 12, 6, 7, 8, 0 };
    Check(scene.states == expected, "the watch runs its states in order and back to closed");
    Check(scene.host.entered == 1 && scene.host.left == 1, "the mission's music gives way once and returns once");
    Check(scene.sawLocked && !scene.lockedOutsideOpen, "the controls lock only while it is open or closing");
    Check(Near(lowestPitch, -40.0f), "the view tilts down to the watch's pitch");
    Check(Near(scene.player.Verta(), -4.0f, 0.05f), "and back to where it was");
    Check(Near(smallestFov, 5.9f), "the view zooms into the watch");
    Check(Near(scene.watch.ZoomFovY(), 60.0f), "and out again");
    Check(scene.watch.PauseAnimationPercent() == 0.0f && !scene.watch.PausingFlag(), "the arm is down");
    Check(scene.hud.SightMode() == 0 && scene.hud.AmmoOff() == 0, "the crosshair and ammo counter come back");
    Check(scene.weapons.ItemInHandOrWatchMenu(HandLeft) == scene.weapons.WeaponId(HandLeft),
          "the left hand holds its own weapon again");
}

void TestTheHudHidesWhileTheWatchIsUp() {
    Scene scene;
    scene.Frame(true);
    while (scene.watch.State() != WatchState::Open && scene.watch.State() != WatchState::Closed) {
        scene.Frame(false);
    }
    Check(scene.watch.State() == WatchState::Open, "the watch opens");
    Check(scene.hud.SightMode() != 0 && scene.hud.AmmoOff() != 0, "the crosshair and ammo counter hide");
    Check(scene.watch.PauseAnimationPercent() == 1.0f && scene.watch.PausingFlag(), "the arm is up");
    Check(scene.watch.ControlsLocked(), "and the controls are locked");
    scene.Frame(false);
    Check(scene.watch.State() == WatchState::Open, "it stays open until asked to close");
}

void TestStartWhileTheWatchComesUpTurnsItRound() {
    Scene scene;
    scene.Frame(true);
    while (scene.watch.State() != WatchState::RaiseArm) {
        scene.Frame(false);
    }
    scene.Frame(true);
    Check(scene.watch.State() == WatchState::LowerArm, "a second Start lowers an arm that is coming up");
    for (int i = 0; i < 2000 && scene.watch.State() != WatchState::Closed; i++) {
        scene.Frame(false);
    }
    Check(scene.watch.State() == WatchState::Closed, "and the watch closes without opening");
}

void TestTheLetterboxOpensWithTheArm() {
    Scene scene;
    Check(scene.watch.PauseAnimationPercent() == 0.0f, "the arm starts down");
    scene.Frame(true);
    float last = 0.0f;
    bool rising = true;
    while (scene.watch.State() != WatchState::ZoomIn) {
        scene.Frame(false);
        const float now = scene.watch.PauseAnimationPercent();
        rising = rising && now >= last;
        last = now;
    }
    Check(rising && last == 1.0f, "the arm's height only grows on the way up");
}

// The watch's zoom, as the menu starts it.
class FakeOwner final : public WatchMenuOwner {
  public:
    void TriggerZoom(float fovY, float time) override {
        zoomFovY = fovY;
        zoomTime = time;
        zooms++;
    }
    bool ZoomRunning() const override {
        return false;
    }
    float PageZoomScale() const override {
        return 1.0f;
    }
    float UnfoldScale() const override {
        return 1.0f;
    }
    int32_t State() const override {
        return WatchState::Open;
    }

    float zoomFovY = 0.0f;
    float zoomTime = 0.0f;
    int zooms = 0;
};

// An open menu over a small inventory, fed a frame of controller at a time.
struct MenuScene {
    FakeHost host;
    FakeOwner owner;
    PlayerOptions options;
    Inventory inventory;
    PlayerWeapons weapons;
    Hud hud;
    WatchMenu menu;
    uint32_t next = 0x80000000u;
    int draws = 0;
    int8_t lastX = 0;
    int8_t lastY = 0;

    MenuScene() {
        inventory.AddItem(kItemFist);
        inventory.AddItem(kItemKnife);
        inventory.AddItem(kItemPp7);
        weapons.Init(WeaponTable::Parse(Table()), &inventory, Zero);
        hud.SetFonts(MakeFont(), MakeFont());
        WatchMenu::Links links;
        links.host = &host;
        links.owner = &owner;
        links.hud = &hud;
        links.weapons = &weapons;
        links.options = &options;
        links.inventory = &inventory;
        links.random = [this] {
            draws++;
            return next;
        };
        menu.Attach(links);
        menu.Reset();
        menu.Begin();
    }

    // One tick of the menu and, unless `render` is false, the draw that follows.
    void Frame(MoveInput input, bool render = true) {
        input.prevStickX = lastX;
        input.prevStickY = lastY;
        menu.SetInput(input);
        menu.Tick(1);
        if (render) {
            static Gfx buffer[16384];
            WatchMenuFrame frame;
            frame.hud.vi = { 320, 240, 0, 30, 320, 180 };
            frame.hud.weapons = &weapons;
            menu.BeginFrame();
            menu.Render(buffer, nullptr, nullptr, true, frame);
        }
        lastX = input.stickX;
        lastY = input.stickY;
    }
};

void TestAQuietTickDrawsOneNumber() {
    MenuScene scene;
    scene.Frame({}, false);
    Check(scene.draws == 1, "a tick draws one number to see whether the screen goes to static");
    Check(scene.menu.Green() == 0xE0, "and the screen stays clear");
}

void TestStaticDrawsAnotherNumber() {
    MenuScene scene;
    scene.next = 0xFFFFFFFFu;
    scene.Frame({}, false);
    Check(scene.draws == 2 && scene.menu.Green() == 0x80 + 3,
          "a high draw starts the static, and a second one clears it a little");
}

void TestStickRightTurnsToTheInventory() {
    MenuScene scene;
    scene.Frame({}, false);
    MoveInput right;
    right.stickX = 76;
    const int before = scene.draws;
    scene.Frame(right, false);
    Check(scene.menu.Page() == WatchPage::Inventory, "the stick to the right turns to the next page");
    Check(scene.draws - before == 2, "a tick and the page change each draw a number");
    Check(scene.owner.zooms == 1 && Near(scene.owner.zoomFovY, 4.6f) && Near(scene.owner.zoomTime, 15.0f),
          "and the watch zooms to it");
    scene.Frame(right, false);
    Check(scene.menu.Page() == WatchPage::Inventory, "holding it does not turn another page");
}

void TestASelectedItemHoldsThePage() {
    MenuScene scene;
    scene.Frame({}, false);
    MoveInput press;
    press.pressed = Buttons::A;
    scene.Frame(press);
    Check(scene.menu.ItemSelected(), "A selects the page's item");
    MoveInput right;
    right.stickX = 76;
    scene.Frame(right);
    Check(scene.menu.Page() == WatchPage::MissionStatus, "and the page stays while it is");
    scene.Frame(press);
    Check(!scene.menu.ItemSelected(), "A lets it go");
}

void TestTheInventoryEquipsWhatItListed() {
    MenuScene scene;
    scene.Frame({}, false);
    MoveInput right;
    right.stickX = 76;
    scene.Frame(right);
    Check(scene.menu.Page() == WatchPage::Inventory, "the inventory page opens");

    MoveInput down;
    down.stickY = -60;
    scene.Frame(down);
    for (int i = 0; i < 20; i++) {
        scene.Frame({});
    }
    Check(scene.menu.CurrentItem() == 1, "the stick down moves to the next item");

    MoveInput equip;
    equip.pressed = Buttons::Z;
    scene.Frame(equip);
    Check(scene.weapons.WeaponId(HandRight) == kItemKnife, "Z equips the item it rests on");
}

} // namespace

int main() {
    TestTheWatchOpensAndCloses();
    TestTheHudHidesWhileTheWatchIsUp();
    TestStartWhileTheWatchComesUpTurnsItRound();
    TestTheLetterboxOpensWithTheArm();
    TestAQuietTickDrawsOneNumber();
    TestStaticDrawsAnotherNumber();
    TestStickRightTurnsToTheInventory();
    TestASelectedItemHoldsThePage();
    TestTheInventoryEquipsWhatItListed();
    return gFailures == 0 ? 0 : 1;
}
