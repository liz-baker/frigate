#include "level/GunDraw.h"

#include <cmath>
#include <cstdio>
#include <memory>

#include "gfx/Gu.h"
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

bool Near(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) < epsilon;
}

// Item 1 is a gun with a model: shown in first person, at (11, -20, -33) on
// screen, following the crosshair by 3, 3 and 8.5. Item 2 has one too but is
// flagged to hide the hand.
const char* kTable =
    "item\t0\tITEM_UNARMED\t0\t0\t0\t0x110\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t-"
    "1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
    "item\t1\tITEM_GUN\t1\t1\t7\t0x800\tmodels/gun/"
    "GgunZ\tstandard_gun\t1.0\t11.0\t-20.0\t-33.0\t3.0\t3.0\t8.5\t0.8\t0.9\t"
    "1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
    "item\t2\tITEM_HIDDEN\t1\t1\t7\t0x2800\tmodels/gun/GhiddenZ\tstandard_gun\t1.0\t11.0\t-20.0\t-33.0\t3.0\t3.0\t8.5\t"
    "0.8\t0.9\t1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
    "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\n"
    "ammo\t1\tAMMO_9MM\t800\t0x0\t0.0\n";

ModelNode Node(uint32_t offset, uint8_t type, int16_t matrix, int32_t parent, int32_t child, int32_t next) {
    ModelNode node;
    node.offset = offset;
    node.type = type;
    node.matrix = matrix;
    node.parent = parent;
    node.child = child;
    node.next = next;
    return node;
}

// A group owning matrix 0 above a display list and the muzzle flash's group,
// which owns matrix 1 and is in switch slot 3.
std::shared_ptr<Model::ModelInstance> GunModel() {
    auto tree = std::make_shared<ModelTree>();
    ModelNode list = Node(0x10, 0x04, kNoMatrix, 0, kNoNode, 2);
    list.primaryList = 0x100;
    ModelNode flash = Node(0x20, 0x02, 1, 0, kNoNode, kNoNode);
    flash.origin[0] = 0.0f;
    flash.origin[1] = 2.0f;
    flash.origin[2] = 7.0f;
    tree->Nodes = { Node(0x00, 0x02, 0, kNoNode, 1, kNoNode), list, flash };
    tree->MatrixCount = 2;
    tree->Switches = { kNoNode, kNoNode, kNoNode, 2 };
    return Model::ModelInstance::FromParts("models/gun/GgunZ", tree, nullptr);
}

class OneGun final : public ModelFactory {
  public:
    std::shared_ptr<ModelHandle> CreateBody(const std::string&, const std::string&, bool) override {
        return nullptr;
    }
    std::shared_ptr<ModelHandle> CreateProp(const std::string&) override {
        return nullptr;
    }
    std::shared_ptr<Model::ModelInstance> CreateGun(const std::string& path) override {
        return path == "models/gun/GgunZ" ? gun : nullptr;
    }

    std::shared_ptr<Model::ModelInstance> gun = GunModel();
};

uint32_t Zero() {
    return 0;
}

struct Scene {
    OneGun models;
    Inventory inventory;
    PlayerWeapons weapons;
    GunDraw draw;
    GunFrame frame;

    Scene() {
        weapons.SetModels(&models);
        weapons.Init(WeaponTable::Parse(kTable), &inventory, Zero);
        frame.random = Zero;
        frame.view.scaleX = 0.003f;
        frame.view.scaleY = 0.003f;
    }

    void Equip(int32_t item) {
        weapons.EquipWeapon(HandRight, item);
        for (int i = 0; i < 60; i++) {
            weapons.TickGameplay(GunTickInput{}, nullptr);
        }
    }
};

void TestAHeldGunIsPosedInFrontOfTheCamera() {
    Scene scene;
    scene.Equip(1);
    // The crosshair, and so the gun's aim, starts in the middle of the screen.
    scene.weapons.AimSwivel(0.0f, 0.0f, 1, scene.frame.view);
    scene.draw.Update(scene.weapons, scene.frame);

    Check(scene.weapons.Hands(HandRight).modelShown, "a ready gun with a first-person model is shown");
    Check(!scene.weapons.Hands(HandLeft).modelShown, "the empty hand shows nothing");

    const auto& walk = scene.models.gun->Walk();
    Check(walk.matrices.size() == 2 && walk.lists.size() == 1, "the model is walked with its matrices");
    const Mat4& gun = walk.matrices[0];
    // The model is drawn a tenth of its size, at the item's place on screen
    // (within the little the sway has moved it).
    float length = std::sqrt(gun.m[0][0] * gun.m[0][0] + gun.m[0][1] * gun.m[0][1] + gun.m[0][2] * gun.m[0][2]);
    Check(Near(length, 0.10000001f), "the gun is drawn at a tenth of its size");
    Check(std::fabs(gun.m[3][0] - 11.0f) < 1.0f && std::fabs(gun.m[3][1] + 20.0f) < 1.0f &&
              std::fabs(gun.m[3][2] + 33.0f) < 1.0f,
          "and sits at the item's place on screen");
    // The aim point is the middle of the screen, far ahead, so the gun's forward
    // axis (the third row, scaled) leans from its own position toward that point.
    Check(gun.m[2][2] * 10.0f < -0.99f, "the gun points forward");
}

void TestAHiddenHandIsNotShown() {
    Scene scene;
    scene.Equip(2);
    scene.draw.Update(scene.weapons, scene.frame);
    Check(!scene.weapons.Hands(HandRight).modelShown, "a gun flagged to hide its hand is not shown");
}

void TestAlignTurnsMinusZOntoTheAxis() {
    Gu::MatrixF mf;
    Gu::AlignF(mf, 30.0f, 3.0f, -4.0f, 12.0f);
    // The third row is the negated unit axis.
    Check(Near(mf[2][0], -3.0f / 13.0f) && Near(mf[2][1], 4.0f / 13.0f) && Near(mf[2][2], -12.0f / 13.0f),
          "the third row is minus the axis");
    // The rows are orthonormal.
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            const float dot = mf[a][0] * mf[b][0] + mf[a][1] * mf[b][1] + mf[a][2] * mf[b][2];
            Check(Near(dot, a == b ? 1.0f : 0.0f), "the rotation is orthonormal");
        }
    }
    // A zero angle about an axis along -Z is the identity.
    Gu::AlignF(mf, 0.0f, 0.0f, 0.0f, -1.0f);
    Check(Near(mf[0][0], 1.0f) && Near(mf[1][1], 1.0f) && Near(mf[2][2], 1.0f) && Near(mf[0][1], 0.0f),
          "aligning -Z to itself leaves the identity");
}

} // namespace

int main() {
    TestAlignTurnsMinusZOntoTheAxis();
    TestAHeldGunIsPosedInFrontOfTheCamera();
    TestAHiddenHandIsNotShown();
    if (gFailures == 0) {
        std::printf("GunDrawTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
