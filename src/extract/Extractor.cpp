#include "extract/Extractor.h"

#include "extract/AudioPack.h"
#include "extract/Inflate.h"
#include "extract/ObjectTables.h"
#include "extract/ModelPackers.h"
#include "extract/TexturePackers.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace Frigate::Extract {

namespace {

class FunctionStage : public Stage {
  public:
    FunctionStage(std::string name, ArchiveId archive, StageFunction run)
        : mName(std::move(name)), mArchive(archive), mRun(std::move(run)) {
    }

    std::string_view Name() const override {
        return mName;
    }
    ArchiveId Archive() const override {
        return mArchive;
    }
    Error Run(StageContext& context, O2rWriter& writer) override {
        return mRun(context, writer);
    }

  private:
    std::string mName;
    ArchiveId mArchive;
    StageFunction mRun;
};

Error LoadRom(const std::filesystem::path& path, std::vector<uint8_t>& rom) {
    std::error_code ec;
    uint64_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return Error::Make(ErrorCode::FileOpen, "Could not read the ROM at " + path.string() + ": " + ec.message());
    }
    if (size != kRomSize) {
        return Error::Make(ErrorCode::RomSize, "This file is not the size of a GoldenEye 007 ROM (12 MiB).");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Error::Make(ErrorCode::FileOpen, "Could not open the ROM at " + path.string());
    }
    rom.resize(size);
    file.read(reinterpret_cast<char*>(rom.data()), std::streamsize(size));
    if (uint64_t(file.gcount()) != size) {
        return Error::Make(ErrorCode::FileOpen, "Could not read all of the ROM at " + path.string());
    }
    return {};
}

} // namespace

const char* ArchiveFileName(ArchiveId id) {
    switch (id) {
        case ArchiveId::Textures:
            return "goldeneye_textures.o2r";
        case ArchiveId::Objects:
            return "goldeneye_objects.o2r";
        case ArchiveId::Geometry:
            return "goldeneye_geometry.o2r";
        case ArchiveId::Audio:
            return "goldeneye_audio.o2r";
    }
    return "";
}

std::unique_ptr<Stage> MakeStage(std::string name, ArchiveId archive, StageFunction run) {
    return std::make_unique<FunctionStage>(std::move(name), archive, std::move(run));
}

StageContext::StageContext(Region region, std::span<const uint8_t> rom, const std::atomic<bool>* cancel,
                           std::function<void(uint64_t, uint64_t, std::string_view)> report)
    : mRegion(region), mRom(rom), mCancel(cancel), mReport(std::move(report)) {
}

Error StageContext::RomRange(uint64_t offset, uint64_t size, std::span<const uint8_t>& out) const {
    if (offset > mRom.size() || size > mRom.size() - offset) {
        return Error::Make(ErrorCode::Corrupt, "Data the extractor needs lies outside the ROM.");
    }
    out = mRom.subspan(size_t(offset), size_t(size));
    return {};
}

Error StageContext::ReadFile(const FileEntry& entry, std::vector<uint8_t>& out) const {
    std::span<const uint8_t> bytes;
    if (Error err = RomRange(entry.offset, entry.size, bytes); !err.ok()) {
        return err;
    }
    if (!entry.compressed) {
        out.assign(bytes.begin(), bytes.end());
        return {};
    }
    return Inflate1172(bytes, out);
}

void StageContext::Report(uint64_t done, uint64_t total, std::string_view detail) const {
    if (mReport) {
        mReport(done, total, detail);
    }
}

void Extractor::RegisterStage(std::unique_ptr<Stage> stage) {
    mStages.push_back(std::move(stage));
}

Result Extractor::Run(const Options& options) {
    Result result;
    std::vector<std::filesystem::path> temporaries;
    std::vector<std::filesystem::path> finished;

    auto cleanup = [&]() {
        std::error_code ec;
        for (const auto& path : temporaries) {
            std::filesystem::remove(path, ec);
        }
        for (const auto& path : finished) {
            std::filesystem::remove(path, ec);
        }
    };
    auto fail = [&](Error error) {
        cleanup();
        result.error = std::move(error);
        result.archives.clear();
        return result;
    };
    auto cancelled = [&]() { return options.cancel != nullptr && options.cancel->load(); };
    const Error kCancelled = Error::Make(ErrorCode::Cancelled, "Extraction was cancelled.");

    if (options.romPath.empty() || options.outputDir.empty()) {
        return fail(Error::Make(ErrorCode::InvalidArgument, "A ROM path and an output directory are required."));
    }

    std::vector<uint8_t> rom;
    if (Error err = LoadRom(options.romPath, rom); !err.ok()) {
        return fail(err);
    }
    Error detectError;
    std::optional<Region> detected = DetectRegion(rom, &detectError);
    if (!detected) {
        return fail(detectError);
    }
    if (options.region && *options.region != *detected) {
        return fail(Error::Make(ErrorCode::RegionMismatch, std::string("This is the ") + GetRegionInfo(*detected).name +
                                                               " ROM, but the " + GetRegionInfo(*options.region).name +
                                                               " one was expected."));
    }
    result.region = *detected;

    std::error_code ec;
    std::filesystem::create_directories(options.outputDir, ec);
    if (ec) {
        return fail(
            Error::Make(ErrorCode::FileWrite, "Could not create " + options.outputDir.string() + ": " + ec.message()));
    }

    // Stages grouped by archive, in archive order.
    std::vector<Stage*> plan;
    std::vector<ArchiveId> archiveOrder;
    for (size_t a = 0; a < kArchiveCount; a++) {
        bool any = false;
        for (const auto& stage : mStages) {
            if (size_t(stage->Archive()) == a) {
                plan.push_back(stage.get());
                any = true;
            }
        }
        if (any) {
            archiveOrder.push_back(ArchiveId(a));
        }
    }

    size_t stageIndex = 0;
    std::string stageName;
    auto report = [&](uint64_t done, uint64_t total, std::string_view detail) {
        if (!options.progress) {
            return;
        }
        Progress progress;
        progress.stageIndex = stageIndex;
        progress.stageCount = plan.size();
        progress.stageName = stageName;
        progress.done = done;
        progress.total = total;
        progress.detail = std::string(detail);
        double within = (total != 0) ? std::min(1.0, double(done) / double(total)) : 0.0;
        progress.fraction = plan.empty() ? 1.0 : (double(stageIndex) + within) / double(plan.size());
        options.progress(progress);
    };
    StageContext context(*detected, rom, options.cancel, report);

    for (ArchiveId archive : archiveOrder) {
        std::filesystem::path finalPath = options.outputDir / ArchiveFileName(archive);
        std::filesystem::path tempPath = finalPath;
        tempPath += ".tmp";
        temporaries.push_back(tempPath);

        O2rWriter writer;
        if (Error err = writer.Open(tempPath); !err.ok()) {
            return fail(err);
        }
        for (Stage* stage : plan) {
            if (stage->Archive() != archive) {
                continue;
            }
            if (cancelled()) {
                return fail(kCancelled);
            }
            stageName = std::string(stage->Name());
            report(0, 0, {});
            if (Error err = stage->Run(context, writer); !err.ok()) {
                return fail(err);
            }
            if (cancelled()) {
                return fail(kCancelled);
            }
            stageIndex++;
        }
        if (Error err = writer.Close(); !err.ok()) {
            return fail(err);
        }
        std::filesystem::rename(tempPath, finalPath, ec);
        if (ec) {
            return fail(
                Error::Make(ErrorCode::FileWrite, "Could not create " + finalPath.string() + ": " + ec.message()));
        }
        temporaries.pop_back();
        finished.push_back(finalPath);
    }

    stageName.clear();
    result.archives = finished;
    if (options.progress) {
        Progress done;
        done.stageIndex = plan.size();
        done.stageCount = plan.size();
        done.fraction = 1.0;
        options.progress(done);
    }
    return result;
}

void RegisterDefaultStages(Extractor& extractor) {
    RegisterTexturePackers(extractor);
    RegisterObjectTablePackers(extractor);
    RegisterModelPackers(extractor);

    // Placeholders until the packers are ported: they write nothing.
    auto nothing = [](StageContext&, O2rWriter&) { return Error{}; };
    extractor.RegisterStage(MakeStage("Objects", ArchiveId::Objects, nothing));
    extractor.RegisterStage(MakeStage("Audio", ArchiveId::Audio, PackAudio));
}

} // namespace Frigate::Extract
