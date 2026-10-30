// ROM discovery and the first-run flow (extract/RomSearch.h, extract/FirstRun.h). The checks on fake files run
// anywhere; the ones that extract need the US ROM in FRIGATE_ROMS (a path-separator list of ROM files and
// directories) and skip otherwise.

#include "extract/FirstRun.h"
#include "extract/RomSearch.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <io.h>
#define access _access
#define R_OK 4
#else
#include <unistd.h>
#endif

using namespace Frigate::Extract;
namespace fs = std::filesystem;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

fs::path ScratchDir(const char* name) {
    fs::path dir = fs::temp_directory_path() / (std::string("frigate_first_run_test_") + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// A file of `size` bytes starting with `head`.
void WriteFile(const fs::path& path, uint64_t size, const std::string& head = {}) {
    std::ofstream file(path, std::ios::binary);
    file.write(head.data(), std::streamsize(head.size()));
    file.close();
    if (size > head.size()) {
        fs::resize_file(path, size);
    }
}

std::optional<fs::path> FindRomInEnvironment(Region region = Region::Us) {
    const char* env = std::getenv("FRIGATE_ROMS");
    if (env == nullptr) {
        return std::nullopt;
    }
    std::string list = env;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(':', start);
        fs::path location = list.substr(start, end == std::string::npos ? end : end - start);
        std::error_code ec;
        std::vector<fs::path> files;
        if (fs::is_directory(location, ec)) {
            for (const auto& entry : fs::directory_iterator(location, ec)) {
                files.push_back(entry.path());
            }
        } else {
            files.push_back(location);
        }
        for (const fs::path& file : files) {
            if (fs::is_regular_file(file, ec) && IsRegionRom(file, region)) {
                return file;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return std::nullopt;
}

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void TestSearchOnFakeFiles() {
    fs::path root = ScratchDir("search");
    fs::path first = root / "first", second = root / "second", missing = root / "missing";
    fs::create_directories(first);
    fs::create_directories(second);

    WriteFile(first / "decoy.z64", kRomSize);                                       // the right size, the wrong hash
    WriteFile(first / "small.z64", 1000);                                           // never read
    WriteFile(first / "huge.bin", kRomSize * 4);                                    // never read
    WriteFile(first / "swapped.v64", kRomSize, std::string("\x37\x80\x40\x12", 4)); // byte-swapped header
    fs::create_directories(first / "nested");
    WriteFile(first / "nested" / "inner.z64", kRomSize); // subdirectories are not searched

    fs::path locked = first / "locked.z64";
    WriteFile(locked, kRomSize);
    fs::permissions(locked, fs::perms::none);
    bool lockedIsUnreadable = access(locked.c_str(), R_OK) != 0; // not so when running as root

    std::vector<fs::path> dirs = { first, first, missing, second };
    RomSearchResult result = FindUsRom(dirs);
    Check(!result.usRom, "no ROM among decoys");
    Check(result.searched.size() == 3, "a repeated directory is searched once");
    Check(result.byteSwapped.size() == 1 && result.byteSwapped[0].filename() == "swapped.v64",
          "byte-swapped dump is reported");
    Check(result.unreadable.size() == (lockedIsUnreadable ? 1u : 0u), "unreadable file is reported, not fatal");
    Check(result.hashed == 3u, "only files of ROM size are hashed");
    fs::permissions(locked, fs::perms::owner_all);

    // The message names what was looked for and where.
    FirstRunOptions options;
    options.archiveDir = root / "archives";
    options.searchDirs = dirs;
    FirstRunResult run = EnsureArchives(options);
    Check(run.status == FirstRunStatus::Failed, "no ROM is a failure");
    Check(Contains(run.message, "Not a supported ROM despite the right size"), "message lists the wrong-hash file");
    Check(Contains(run.message, first.string()) && Contains(run.message, second.string()),
          "message lists the searched directories");
    Check(Contains(run.message, "byte-swapped"), "message mentions the byte-swapped dump");
    Check(Contains(run.message, "--rom"), "message says how to name a ROM");
    Check(!fs::exists(options.archiveDir / ArchiveFileName(ArchiveId::Textures)), "nothing was written");

    // --rom with a file that is not the ROM.
    options.rom = first / "decoy.z64";
    run = EnsureArchives(options);
    Check(run.status == FirstRunStatus::Failed && Contains(run.message, "decoy.z64"), "explicit decoy is refused");
    options.rom = first / "swapped.v64";
    run = EnsureArchives(options);
    Check(run.status == FirstRunStatus::Failed && Contains(run.message, "byte-swapped"),
          "explicit byte-swapped file says so");
    options.rom = root / "absent.z64";
    run = EnsureArchives(options);
    Check(run.status == FirstRunStatus::Failed && Contains(run.message, "absent.z64"),
          "explicit missing file is named");

    fs::remove_all(root);
}

void TestArchiveCompleteness() {
    fs::path root = ScratchDir("complete");
    Check(MissingArchives(root).size() == kArchiveCount, "an empty directory is missing every archive");
    WriteFile(root / ArchiveFileName(ArchiveId::Textures), 0);
    Check(!IsArchiveComplete(root / ArchiveFileName(ArchiveId::Textures)), "an empty file is not an archive");
    WriteFile(root / ArchiveFileName(ArchiveId::Objects), 5000, "PK\x03\x04");
    Check(!IsArchiveComplete(root / ArchiveFileName(ArchiveId::Objects)),
          "a file without an end record is not an archive");
    fs::create_directories(root / ArchiveFileName(ArchiveId::Audio));
    Check(!IsArchiveComplete(root / ArchiveFileName(ArchiveId::Audio)), "a directory is not an archive");
    fs::remove_all(root);
}

void TestWithRealRom() {
    std::optional<fs::path> rom = FindRomInEnvironment();
    if (!rom) {
        std::printf("skip: no US ROM (set FRIGATE_ROMS)\n");
        return;
    }
    fs::path root = ScratchDir("real");
    fs::path workDir = root / "work", archives = root / "archives";
    fs::create_directories(workDir / "subdir");
    WriteFile(workDir / "decoy.z64", kRomSize);
    fs::create_symlink(fs::absolute(*rom), workDir / "ge.z64");
    fs::copy_file(*rom, workDir / "subdir" / "ignored.z64");

    // Discovery finds the real ROM past the decoy, through a symlink, and ignores the subdirectory.
    std::vector<fs::path> dirs = { root / "elsewhere", workDir };
    RomSearchResult search = FindUsRom(dirs);
    Check(search.usRom && search.usRom->filename() == "ge.z64", "the US ROM is found in the second directory");
    Check(search.hashed == 2, "the decoy and the ROM were hashed");

    // A fresh run extracts all four archives, leaves no temporaries, and a second run has nothing to do.
    std::vector<std::string> log;
    size_t progressCalls = 0;
    FirstRunOptions options;
    options.archiveDir = archives;
    options.searchDirs = dirs;
    options.log = [&](const std::string& text) { log.push_back(text); };
    options.progress = [&](const Progress&) { progressCalls++; };
    FirstRunResult run = EnsureArchives(options);
    Check(run.status == FirstRunStatus::Extracted, "extraction runs from an empty archive directory");
    Check(MissingArchives(archives).empty(), "all four archives are whole afterwards");
    Check(progressCalls > 0 && !log.empty(), "progress and log were reported");
    size_t entries = 0;
    for (const auto& entry : fs::directory_iterator(archives)) {
        entries++;
        Check(entry.path().extension() == ".o2r", "only finished archives are left in the directory");
    }
    Check(entries == kArchiveCount, "exactly the four archives are written");
    Check(EnsureArchives(options).status == FirstRunStatus::NotNeeded, "a complete set is left alone");

    // A truncated archive counts as missing and is rebuilt.
    fs::path textures = archives / ArchiveFileName(ArchiveId::Textures);
    fs::resize_file(textures, fs::file_size(textures) / 2);
    Check(MissingArchives(archives).size() == 1, "a truncated archive is stale");
    Check(EnsureArchives(options).status == FirstRunStatus::Extracted, "a stale set is re-extracted");
    Check(MissingArchives(archives).empty(), "the set is whole again");

    // A cancelled run leaves nothing of its own behind.
    fs::remove(textures);
    std::atomic<bool> cancel{ true };
    options.cancel = &cancel;
    run = EnsureArchives(options);
    Check(run.status == FirstRunStatus::Cancelled, "a cancelled run reports cancellation");
    Check(!fs::exists(textures), "a cancelled run leaves no archive of its own behind");
    for (const auto& entry : fs::directory_iterator(archives)) {
        Check(entry.path().extension() == ".o2r", "a cancelled run leaves no temporary file");
    }
    fs::remove_all(root);
}

void TestOtherRegion() {
    std::optional<fs::path> europe = FindRomInEnvironment(Region::Eur);
    if (!europe) {
        std::printf("skip: no Europe ROM (set FRIGATE_ROMS)\n");
        return;
    }
    fs::path root = ScratchDir("europe");
    fs::create_symlink(fs::absolute(*europe), root / "ge-eur.z64");
    FirstRunOptions options;
    options.archiveDir = root / "archives";
    options.searchDirs = { root };
    FirstRunResult run = EnsureArchives(options);
    Check(run.status == FirstRunStatus::Failed, "a Europe ROM is not extracted");
    Check(Contains(run.message, "Europe") && Contains(run.message, "only the USA release"),
          "the message says the release is not supported yet");
    options.rom = root / "ge-eur.z64";
    run = EnsureArchives(options);
    Check(run.status == FirstRunStatus::Failed && Contains(run.message, "Europe"), "an explicit Europe ROM is refused");
    Check(!fs::exists(options.archiveDir), "nothing was created");
    fs::remove_all(root);
}

} // namespace

int main() {
    TestSearchOnFakeFiles();
    TestArchiveCompleteness();
    TestWithRealRom();
    TestOtherRegion();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
