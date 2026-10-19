#include "PackShaders.h"

#include "extract/O2rWriter.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Frigate::Extract {

namespace {

constexpr uint32_t kShaderVersion = 0;

std::vector<std::string> Components(const std::filesystem::path& path) {
    std::vector<std::string> parts;
    for (const auto& part : path) {
        parts.push_back(part.generic_string());
    }
    return parts;
}

} // namespace

Error PackShaders(const std::filesystem::path& shadersDir, const std::filesystem::path& output, size_t* count) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(shadersDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file()) {
            files.push_back(fs::relative(it->path(), shadersDir));
        }
    }
    if (ec) {
        return Error::Make(ErrorCode::FileOpen, "cannot read " + shadersDir.string() + ": " + ec.message());
    }
    if (files.empty()) {
        return Error::Make(ErrorCode::FileOpen, "no shader sources under " + shadersDir.string());
    }
    // Path order compares component by component, so "a/b" sorts before "a.x" although '/' follows '.' as a character.
    std::sort(files.begin(), files.end(),
              [](const fs::path& a, const fs::path& b) { return Components(a) < Components(b); });

    O2rWriter writer;
    if (Error error = writer.Open(output); !error.ok()) {
        return error;
    }
    for (size_t index = 0; index < files.size(); index++) {
        std::ifstream in(shadersDir / files[index], std::ios::binary);
        if (!in) {
            return Error::Make(ErrorCode::FileOpen, "cannot read " + (shadersDir / files[index]).string());
        }
        // No terminator: ReadCString stops at end of data, and would keep a NUL in the returned text, which the
        // shader preprocessor rejects.
        std::vector<uint8_t> payload((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string virtualPath = "shaders/" + files[index].generic_string();
        if (Error error = writer.AddResource(virtualPath, "SHAD", kShaderVersion, index, payload); !error.ok()) {
            return error;
        }
    }
    if (count != nullptr) {
        *count = files.size();
    }
    return writer.Close();
}

} // namespace Frigate::Extract
