"""The sound effect and music track names (src/extract/AudioNamesData.cpp)."""

from __future__ import annotations

from .common import Decomp, Output, TableError, banner, enum_entries, footer

ENUMS = (("SFX_ID", "kSfxNames"), ("MUSIC_TRACKS", "kMusicNames"))


def generate_audio_names(decomp: Decomp) -> Output:
    header_text = decomp.read("src/bondconstants.h")
    out = banner("the GoldenEye decomp's SFX_ID and MUSIC_TRACKS enums in src/bondconstants.h")
    out += ['#include "extract/AudioNames.h"', "", "namespace Frigate::Extract::Detail {", ""]
    for enum_name, symbol in ENUMS:
        try:
            entries = enum_entries(header_text, enum_name)
        except ValueError as error:
            raise TableError(f"{enum_name}: {error}") from error
        out.append(f"// {enum_name}: {{name, value}}")
        out.append(f"const AudioName {symbol}[] = {{")
        for name, value in entries:
            out.append(f'    {{"{name}", {value}}},')
        out.append("};")
        out.append(f"const size_t {symbol}Count = sizeof({symbol}) / sizeof({symbol}[0]);")
        out.append("")
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/AudioNamesData.cpp", "\n".join(out))
