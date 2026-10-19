#include "extract/AudioPack.h"

#include "extract/AudioBank.h"
#include "extract/AudioNames.h"

#include <algorithm>
#include <string>

namespace Frigate::Extract {

std::vector<uint8_t> BuildAudioNamesPayload(std::span<const AudioName> names) {
    uint32_t count = 0;
    for (const AudioName& entry : names) {
        count = std::max(count, entry.value + 1);
    }
    std::vector<std::string> lines(count);
    for (const AudioName& entry : names) {
        lines[entry.value] = entry.name;
    }
    std::vector<uint8_t> out;
    for (uint32_t i = 0; i < count; i++) {
        if (i != 0) {
            out.push_back('\n');
        }
        out.insert(out.end(), lines[i].begin(), lines[i].end());
    }
    return out;
}

namespace {

constexpr const char* kBankResourceType = "GEAB";
constexpr const char* kMusicResourceType = "GEMS";
constexpr const char* kBlobResourceType = "OBLB";

struct BankPath {
    const char* file; // under assets/music/, without the extension
    const char* path; // resource path
};
constexpr BankPath kBanks[] = {
    { "sfx", "audio/sfx" },
    { "instruments", "audio/instruments" },
};

constexpr const char* kMusicFile = "assets/music/music.sbk";

Error FindRange(StageContext& context, const std::string& name, std::span<const uint8_t>& out,
                uint32_t* offset = nullptr) {
    const FileEntry* entry = context.layout().FindFile(name);
    if (entry == nullptr) {
        return Error::Make(ErrorCode::Corrupt, name + " is not in the file list.");
    }
    if (offset != nullptr) {
        *offset = entry->offset;
    }
    return context.RomRange(entry->offset, entry->size, out);
}

} // namespace

Error PackAudio(StageContext& context, O2rWriter& writer) {
    std::vector<uint8_t> payload;
    for (const BankPath& bank : kBanks) {
        if (context.Cancelled()) {
            return Error::Make(ErrorCode::Cancelled, "Extraction was cancelled.");
        }
        context.Report(0, 0, bank.path);
        std::span<const uint8_t> ctl;
        std::span<const uint8_t> tbl;
        if (Error err = FindRange(context, std::string("assets/music/") + bank.file + ".ctl", ctl); !err.ok()) {
            return err;
        }
        if (Error err = FindRange(context, std::string("assets/music/") + bank.file + ".tbl", tbl); !err.ok()) {
            return err;
        }
        if (Error err = PackAudioBank(ctl, tbl, payload); !err.ok()) {
            return Error::Make(err.code, std::string("Audio bank ") + bank.file + ": " + err.message);
        }
        if (Error err = writer.AddResource(bank.path, kBankResourceType, 0, 0, payload); !err.ok()) {
            return err;
        }
    }

    context.Report(0, 0, "audio/music");
    std::span<const uint8_t> sbk;
    uint32_t sbkOffset = 0;
    if (Error err = FindRange(context, kMusicFile, sbk, &sbkOffset); !err.ok()) {
        return err;
    }
    if (Error err = PackMusicSequences(sbk, sbkOffset, payload); !err.ok()) {
        return Error::Make(err.code, "Music: " + err.message);
    }
    if (Error err = writer.AddResource("audio/music", kMusicResourceType, 0, 0, payload); !err.ok()) {
        return err;
    }

    std::vector<uint8_t> names = BuildAudioNamesPayload({ Detail::kSfxNames, Detail::kSfxNamesCount });
    if (Error err = writer.AddResource("audio/sfx_names", kBlobResourceType, 0, 0, names); !err.ok()) {
        return err;
    }
    names = BuildAudioNamesPayload({ Detail::kMusicNames, Detail::kMusicNamesCount });
    return writer.AddResource("audio/music_names", kBlobResourceType, 0, 0, names);
}

} // namespace Frigate::Extract
