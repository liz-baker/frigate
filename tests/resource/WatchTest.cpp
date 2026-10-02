#include "level/Watch.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "level/ModelHandle.h"
#include "model/ModelInstance.h"

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
                "\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\t0.0\t0\t1.0\n";
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

struct Scene {
    SuitHand models;
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
        watch.Attach(&player, &weapons, &hud, &models);
        watch.Reset(0);
    }

    // One frame of the player's tick: the input, the watch, and the guns.
    void Frame(bool start) {
        watch.SetTiming({ 1, watch.ControlsLocked() ? 0 : 1, watch.ControlsLocked() ? 0.0f : 1.0f });
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

} // namespace

int main() {
    TestTheWatchOpensAndCloses();
    TestTheHudHidesWhileTheWatchIsUp();
    TestStartWhileTheWatchComesUpTurnsItRound();
    TestTheLetterboxOpensWithTheArm();
    return gFailures == 0 ? 0 : 1;
}
