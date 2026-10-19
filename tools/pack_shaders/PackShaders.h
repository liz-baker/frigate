#pragma once

#include "extract/Error.h"

#include <cstddef>
#include <filesystem>

namespace Frigate::Extract {

// Writes every file under `shadersDir` to `output` as a Shader resource at `shaders/<relative path>`, with the
// resource id set to the file's position in component-wise path order. `count`, when given, receives the number of
// shaders. An empty directory is an error.
Error PackShaders(const std::filesystem::path& shadersDir, const std::filesystem::path& output,
                  size_t* count = nullptr);

} // namespace Frigate::Extract
