#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "level/LevelInfo.h"

namespace Frigate::Level {

// TEXTBANK_LEVEL_INDEX: the text banks (`obseg/text/L<name>E`) the game reads
// its strings from. A stage's own bank is StageBank(); these are the ones every
// stage loads.
namespace TextBank {
constexpr int32_t Gun = 38;
constexpr int32_t Title = 39;
constexpr int32_t MpMenu = 40;
constexpr int32_t PropObj = 41;
constexpr int32_t MpWeapons = 42;
constexpr int32_t Options = 43;
constexpr int32_t Misc = 44;
constexpr int32_t Count = 45;
} // namespace TextBank

// getStringID: the id of slot `slot` of `bank`.
constexpr int32_t StringId(int32_t bank, int32_t slot) {
    return (bank * 0x400) + slot;
}

// The text the game shows in the language it was built for (US English): the
// loaded banks, and langGet over them.
class Lang {
  public:
    // langGetLangBankIndexFromStagenum: the bank a stage's own text is in, or
    // -1 for a stage with none.
    static int32_t StageBank(LevelId stage);
    // The resource name of `bank` (LnameX_lookuptable's English column):
    // `obseg/text/LgunE` for TextBank::Gun. Empty for an index out of range.
    static std::string BankResource(int32_t bank);

    // Loads `bank`'s strings: one per slot, an empty one for a slot with no
    // text.
    void SetBank(int32_t bank, std::vector<std::string> strings);
    // langGet: the string `id` names, empty for a bank that is not loaded or a
    // slot with no text.
    std::string_view Get(int32_t id) const;

  private:
    std::array<std::vector<std::string>, TextBank::Count> banks_;
};

} // namespace Frigate::Level
