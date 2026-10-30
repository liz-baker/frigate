#pragma once

#include "extract/Extractor.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Frigate::Extract {

// A zip file that is non-empty and ends in an intact end-of-central-directory record whose central directory lies
// inside the file. Catches a missing, empty or truncated archive without reading it.
bool IsArchiveComplete(const std::filesystem::path& path);

// The standard archive file names that are missing from `directory` or fail IsArchiveComplete().
std::vector<std::string> MissingArchives(const std::filesystem::path& directory);

struct FirstRunOptions {
    std::filesystem::path archiveDir;
    // Named explicitly (--rom): searching is skipped, but the file is still checked against the pinned hash.
    std::optional<std::filesystem::path> rom;
    // Searched in order for the US ROM when `rom` is unset.
    std::vector<std::filesystem::path> searchDirs;
    // Called on the thread running EnsureArchives(), once per notable step; the text is also fit for a log.
    std::function<void(const std::string&)> log;
    // Called on the thread running EnsureArchives().
    ProgressCallback progress;
    const std::atomic<bool>* cancel = nullptr;
};

enum class FirstRunStatus {
    NotNeeded, // the archive set was already complete; nothing was read or written
    Extracted,
    Cancelled,
    Failed,
};

struct FirstRunResult {
    FirstRunStatus status = FirstRunStatus::NotNeeded;
    // On Failed and Cancelled: what was looked for and where, or the extractor's own message, fit to show a person.
    std::string message;
    std::optional<std::filesystem::path> rom; // the ROM extracted from
};

// If archiveDir lacks any standard archive (or holds an incomplete one), finds the US ROM, verifies it and
// extracts into archiveDir. Only the US release is extracted. A failed or cancelled run leaves no archives from
// this run behind, and the set only counts as complete once all four are whole.
FirstRunResult EnsureArchives(const FirstRunOptions& options);

} // namespace Frigate::Extract
