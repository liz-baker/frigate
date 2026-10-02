#include "level/ResourceBgSource.h"

#include "level/GaitHead.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/DisplayList.h"
#include "fast/resource/type/Vertex.h"
#include "gfx/GbiF3dex.h"
#include "libultraship/libultra/gbi.h"
#include "resource/type/Stan.h"
#include "resource/type/Text.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Level {

namespace {

std::string RoomPath(const LevelEntry& level, int room) {
    return fmt::format("bg/{}/room{:02d}", level.bgName, room);
}

// The memory a loaded room owns.
struct RoomBuffers {
    std::vector<Vtx> vertices;
    std::vector<Gfx> primary;
    std::vector<Gfx> secondary;
};

} // namespace

ResourceBgSource::ResourceBgSource(std::shared_ptr<Ship::ResourceManager> resourceManager)
    : resourceManager_(std::move(resourceManager)), models_(resourceManager_) {
}

std::optional<std::vector<uint8_t>> ResourceBgSource::LoadHeader(const LevelEntry& level) {
    auto blob = resourceManager_->LoadResource<Ship::Blob>(fmt::format("bg/{}/header", level.bgName));
    if (blob == nullptr) {
        return std::nullopt;
    }
    return blob->Data;
}

std::optional<std::vector<uint8_t>> ResourceBgSource::LoadSetup(const std::string& name) {
    auto blob = resourceManager_->LoadResource<Ship::Blob>("obseg/setup/" + name);
    if (blob == nullptr) {
        return std::nullopt;
    }
    return blob->Data;
}

std::shared_ptr<const StanData> ResourceBgSource::LoadStan(const LevelEntry& level) {
    auto stan = resourceManager_->LoadResource<StanResource>(level.stanName);
    if (stan == nullptr) {
        return nullptr;
    }
    // Shares the resource's lifetime, so the tiles are not copied.
    return std::shared_ptr<const StanData>(stan, &stan->Data);
}

std::unique_ptr<HeadMotion> ResourceBgSource::CreateHead() {
    return GaitHead::Create(resourceManager_);
}

namespace {

// A text blob resource, without the NULs its storage carries past the text.
std::optional<std::string> LoadTextBlob(Ship::ResourceManager& resourceManager, const char* path, const char* missing) {
    auto blob = resourceManager.LoadResource<Ship::Blob>(path);
    if (blob == nullptr) {
        spdlog::warn("no '{}' in the loaded archives (run `just pack_archives`); {}", path, missing);
        return std::nullopt;
    }
    std::string text(reinterpret_cast<const char*>(blob->Data.data()), blob->Data.size());
    text.resize(text.find_last_not_of('\0') + 1);
    return text;
}

} // namespace

std::optional<std::string> ResourceBgSource::LoadTextureHits() {
    return LoadTextBlob(*resourceManager_, "textures/hits", "a shot leaves the default mark whatever it hits");
}

std::optional<std::string> ResourceBgSource::LoadWeaponStats() {
    return LoadTextBlob(*resourceManager_, "weapon/stats", "the stage has no weapons");
}

std::optional<std::string> ResourceBgSource::LoadPropModels() {
    return LoadTextBlob(*resourceManager_, "prop/models", "the stage has no objects");
}

std::optional<std::string> ResourceBgSource::LoadChrRecords() {
    return LoadTextBlob(*resourceManager_, "chr/records", "the stage has no characters");
}

std::optional<std::string> ResourceBgSource::LoadChrHats() {
    return LoadTextBlob(*resourceManager_, "chr/hats", "hats sit on heads unadjusted");
}

std::optional<std::string> ResourceBgSource::LoadAiGlobals() {
    return LoadTextBlob(*resourceManager_, "ai/globals", "characters given a global AI list have none to run");
}

std::optional<std::string> ResourceBgSource::LoadAnimationTable() {
    return LoadTextBlob(*resourceManager_, "animations/table", "characters' animations do not start");
}

std::optional<std::string> ResourceBgSource::LoadChrCombat() {
    return LoadTextBlob(*resourceManager_, "chr/combat", "characters neither fight nor react to a hit");
}

std::optional<std::string> ResourceBgSource::LoadMonitorLists() {
    return LoadTextBlob(*resourceManager_, "monitor/lists", "monitor screens do not animate");
}

std::optional<std::vector<std::string>> ResourceBgSource::LoadTextBank(const std::string& resource) {
    auto bank = resourceManager_->LoadResource<Frigate::Text>(resource);
    if (bank == nullptr) {
        return std::nullopt;
    }
    return bank->Strings;
}

std::shared_ptr<Font> ResourceBgSource::LoadFont(const std::string& path) {
    return resourceManager_->LoadResource<Font>(path);
}

std::optional<std::string> ResourceBgSource::LoadHudImages() {
    return LoadTextBlob(*resourceManager_, "hud/images", "the HUD has no crosshair or ammo icons");
}

std::optional<RoomGeometry> ResourceBgSource::LoadRoom(const LevelEntry& level, int room, const BgRoomEntry& entry) {
    const std::string base = RoomPath(level, room);
    const std::string vertexPath = base + "/vtx/000000";
    RoomGeometry geometry;
    auto buffers = std::make_shared<RoomBuffers>();

    // The room's data is read into memory of its own, as the game reads the
    // file, so the code that darkens vertices and swaps commands edits this
    // room's copy and not the cached resource.
    const Vtx* resourceVertices = nullptr;
    if (entry.vertexBlob != 0) {
        auto vertices = resourceManager_->LoadResource<Fast::Vertex>(vertexPath);
        if (vertices == nullptr) {
            spdlog::error("level: no vertices at '{}'", vertexPath);
            return std::nullopt;
        }
        for (const Vtx& vertex : vertices->VertexList) {
            geometry.vertexPositions.push_back({ vertex.v.ob[0], vertex.v.ob[1], vertex.v.ob[2] });
        }
        buffers->vertices = vertices->VertexList;
        resourceVertices = vertices->VertexList.data();
    }

    const uint64_t vertexHash = CRC64(vertexPath.c_str());
    auto loadList = [&](uint32_t address, const char* kind, std::vector<Gfx>& commands) {
        if (address == 0) {
            return true;
        }
        const std::string path = fmt::format("{}/dl/{}/000000", base, kind);
        // Loaded by hash because that is the key the interpreter resolves a
        // list call with; a by-path load is a separate cache entry.
        auto list = std::dynamic_pointer_cast<Fast::DisplayList>(resourceManager_->LoadResource(CRC64(path.c_str())));
        if (list == nullptr) {
            spdlog::error("level: no display list at '{}'", path);
            return false;
        }
        commands = list->Instructions;
        // A vertex load names its block by hash and an offset into it; the
        // interpreter overwrites the offset with the address the first time
        // the load runs. Name this room's vertices by address from the start.
        for (size_t i = 0; i + 1 < commands.size(); i += F3dex::IsExpandedCommand(commands[i].words.w0 >> 24) ? 2 : 1) {
            if ((commands[i].words.w0 >> 24) != static_cast<uint8_t>(G_VTX_OTR_HASH)) {
                continue;
            }
            const uint64_t hash = (static_cast<uint64_t>(commands[i + 1].words.w0) << 32) | commands[i + 1].words.w1;
            if (hash != vertexHash || resourceVertices == nullptr) {
                continue;
            }
            const uintptr_t operand = commands[i].words.w1;
            // Either an offset into the block or, once run, an address in it.
            const uintptr_t offset =
                operand > 0xFFFFF ? operand - reinterpret_cast<uintptr_t>(resourceVertices) : operand;
            commands[i].words.w1 = reinterpret_cast<uintptr_t>(buffers->vertices.data()) + offset;
        }
        return true;
    };
    if (!loadList(entry.primaryList, "primary", buffers->primary) ||
        !loadList(entry.secondaryList, "secondary", buffers->secondary)) {
        return std::nullopt;
    }

    geometry.vertices = { buffers->vertices.data(), buffers->vertices.size() };
    geometry.primaryList = { buffers->primary.data(), buffers->primary.size() };
    geometry.secondaryList = { buffers->secondary.data(), buffers->secondary.size() };
    geometry.resident = buffers;
    return geometry;
}

} // namespace Frigate::Level
