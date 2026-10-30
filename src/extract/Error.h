#pragma once

#include <string>
#include <utility>

namespace Frigate::Extract {

enum class ErrorCode {
    None,
    Cancelled,
    InvalidArgument,
    FileOpen,        // a file could not be opened or read
    FileWrite,       // an output file could not be written
    RomSize,         // not the size of a GoldenEye ROM
    RomByteOrder,    // a byte-swapped (.v64/.n64) dump
    RomUnrecognized, // right size, but not one of the supported releases
    RegionMismatch,  // the caller asked for a region the ROM isn't
    RegionUnsupported,
    Corrupt, // data in the ROM didn't have the expected shape
    Inflate,
    Archive,
    Stage, // a stage reported a failure of its own
};

// `message` is written for a person reading it on screen; `code` is for callers that branch.
struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;

    bool ok() const {
        return code == ErrorCode::None;
    }

    static Error Make(ErrorCode code, std::string message) {
        return Error{ code, std::move(message) };
    }
};

} // namespace Frigate::Extract
