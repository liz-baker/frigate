#pragma once

#include "extract/Error.h"
#include "extract/O2rWriter.h"
#include "extract/Region.h"
#include "extract/RomLayout.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frigate::Extract {

// The archives an extraction produces, in the order they are written.
enum class ArchiveId { Textures, Objects, Geometry, Audio };

constexpr size_t kArchiveCount = 4;

// "goldeneye_textures.o2r" and so on. The names carry no region: one extraction's output directory holds one
// region's archive set.
const char* ArchiveFileName(ArchiveId id);

struct Progress {
    size_t stageIndex = 0; // zero-based, among the stages that will run
    size_t stageCount = 0;
    std::string stageName;
    uint64_t done = 0; // within the stage; `total` is 0 when the stage cannot say
    uint64_t total = 0;
    std::string detail;    // what the stage is working on, for display
    double fraction = 0.0; // overall, 0..1
};

// Called on the thread running Extractor::Run().
using ProgressCallback = std::function<void(const Progress&)>;

struct Options {
    std::filesystem::path romPath;
    std::filesystem::path outputDir;
    // The region the caller expects. Unset means whatever the ROM turns out to be; set and different from the
    // ROM's is a RegionMismatch error.
    std::optional<Region> region;
    ProgressCallback progress;
    // Checked between stages and by stages as they work. Set from any thread to stop; the run then returns
    // ErrorCode::Cancelled and removes what it wrote.
    const std::atomic<bool>* cancel = nullptr;
};

// What a stage sees of the run.
class StageContext {
  public:
    StageContext(Region region, std::span<const uint8_t> rom, const std::atomic<bool>* cancel,
                 std::function<void(uint64_t, uint64_t, std::string_view)> report);

    Region region() const {
        return mRegion;
    }
    const RomLayout& layout() const {
        return GetRomLayout(mRegion);
    }
    // The whole big-endian ROM.
    std::span<const uint8_t> rom() const {
        return mRom;
    }

    // Bytes [offset, offset + size) of the ROM, or an error when that runs off the end.
    Error RomRange(uint64_t offset, uint64_t size, std::span<const uint8_t>& out) const;
    // A file-list entry's bytes, inflated when the entry is compressed.
    Error ReadFile(const FileEntry& entry, std::vector<uint8_t>& out) const;

    void Report(uint64_t done, uint64_t total, std::string_view detail = {}) const;
    bool Cancelled() const {
        return mCancel != nullptr && mCancel->load();
    }

  private:
    Region mRegion;
    std::span<const uint8_t> mRom;
    const std::atomic<bool>* mCancel;
    std::function<void(uint64_t, uint64_t, std::string_view)> mReport;
};

// One unit of extraction work writing into one archive. A packer is a Stage: it reads the ROM through the
// context and adds resources to the writer.
class Stage {
  public:
    virtual ~Stage() = default;
    virtual std::string_view Name() const = 0;
    virtual ArchiveId Archive() const = 0;
    virtual Error Run(StageContext& context, O2rWriter& writer) = 0;
};

using StageFunction = std::function<Error(StageContext&, O2rWriter&)>;
std::unique_ptr<Stage> MakeStage(std::string name, ArchiveId archive, StageFunction run);

struct Result {
    Error error;
    Region region = Region::Us;
    std::vector<std::filesystem::path> archives; // finished archives, when error.ok()
};

class Extractor {
  public:
    // Stages run in registration order within their archive; archives are written in ArchiveId order. An
    // archive with no stage is not written.
    void RegisterStage(std::unique_ptr<Stage> stage);
    size_t StageCount() const {
        return mStages.size();
    }

    // Reads and identifies the ROM, runs every stage, and leaves the finished archives in outputDir. Each archive
    // is written under a temporary name and renamed when complete, so a finished name is a whole archive. On any
    // error or cancellation the run removes the archives it produced.
    Result Run(const Options& options);

  private:
    std::vector<std::unique_ptr<Stage>> mStages;
};

// The stage set the shipped game and frigate-extract use. Packer stages are added here as they are ported;
// until then each archive has a placeholder stage that writes nothing, so the archives exist but are empty.
void RegisterDefaultStages(Extractor& extractor);

} // namespace Frigate::Extract
