#include "extract/ImageDefs.h"

namespace Frigate::Extract {

std::span<const ImageDef> ImageDefs() {
    return { Detail::kImageDefs, Detail::kImageDefCount };
}

} // namespace Frigate::Extract
