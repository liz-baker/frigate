#include "level/ResourceBgSource.h"

#include "level/GaitHead.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/Vertex.h"
#include "libultraship/libultra/gbi.h"
#include "resource/type/Stan.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Level {

namespace {

std::string RoomPath(const LevelEntry& level, int room) {
    return fmt::format("bg/{}/room{:02d}", level.bgName, room);
}

} // namespace

ResourceBgSource::ResourceBgSource(std::shared_ptr<Ship::ResourceManager> resourceManager)
    : resourceManager_(std::move(resourceManager)) {
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

std::optional<std::string> ResourceBgSource::LoadWeaponStats() {
    auto blob = resourceManager_->LoadResource<Ship::Blob>("weapon/stats");
    if (blob == nullptr) {
        spdlog::warn("no 'weapon/stats' in the loaded archives (run `just pack_archives`); the stage has no weapons");
        return std::nullopt;
    }
    // The blob's storage carries trailing NULs past the text.
    std::string text(reinterpret_cast<const char*>(blob->Data.data()), blob->Data.size());
    text.resize(text.find_last_not_of('\0') + 1);
    return text;
}

std::optional<RoomGeometry> ResourceBgSource::LoadRoom(const LevelEntry& level, int room, const BgRoomEntry& entry) {
    const std::string base = RoomPath(level, room);
    RoomGeometry geometry;
    auto resident = std::make_shared<std::vector<std::shared_ptr<Ship::IResource>>>();

    if (entry.vertexBlob != 0) {
        auto vertices = resourceManager_->LoadResource<Fast::Vertex>(base + "/vtx/000000");
        if (vertices == nullptr) {
            spdlog::error("level: no vertices at '{}/vtx/000000'", base);
            return std::nullopt;
        }
        for (const Vtx& vertex : vertices->VertexList) {
            geometry.vertexPositions.push_back({ vertex.v.ob[0], vertex.v.ob[1], vertex.v.ob[2] });
        }
        resident->push_back(vertices);
    }

    auto loadList = [&](uint32_t address, const char* kind, std::string& path) {
        if (address == 0) {
            return true;
        }
        path = fmt::format("{}/dl/{}/000000", base, kind);
        // Loaded by hash because that is the key the interpreter resolves a
        // list call with; a by-path load is a separate cache entry.
        auto list = resourceManager_->LoadResource(CRC64(path.c_str()));
        if (list == nullptr) {
            spdlog::error("level: no display list at '{}'", path);
            return false;
        }
        resident->push_back(list);
        return true;
    };
    if (!loadList(entry.primaryList, "primary", geometry.primaryList) ||
        !loadList(entry.secondaryList, "secondary", geometry.secondaryList)) {
        return std::nullopt;
    }

    geometry.resident = resident;
    return geometry;
}

} // namespace Frigate::Level
