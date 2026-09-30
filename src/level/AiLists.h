#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "level/AiCommands.h"

namespace Frigate::Level {

// An AI list: the commands a character (or the stage) runs, as the bytes the
// game keeps. Command layouts: AiCommands.h.
struct AiList {
    // The list's id (AIListRecord::ID).
    int32_t id = 0;
    std::vector<uint8_t> bytes;
};

// A list number the AI commands name: isGlobalAIListID, isBGAIListID.
constexpr bool IsGlobalAiListId(int32_t id) {
    return id <= 1024;
}
constexpr bool IsBackgroundAiListId(int32_t id) {
    return id >= 4096;
}

// chraiitemsize: the size of the command at `offset` of a list of `size`
// bytes. A command that runs off the end of the list is one byte long.
size_t AiItemSize(const uint8_t* list, size_t size, size_t offset);

// chraiGoToLabel: the offset of the label numbered `label` from `offset` on;
// 0 if the list ends first.
size_t AiGoToLabel(const uint8_t* list, size_t size, size_t offset, uint8_t label);

// The global AI lists (g_GlobalAILists) and the current setup's own.
class AiLists {
  public:
    // The global lists from the `ai/globals` resource's text
    // (docs/formats/goldeneye-ai-lists.md). False if it is malformed.
    bool ParseGlobals(std::string_view text);
    // The lists of the setup (g_CurrentSetup.ailists).
    void SetLocal(std::vector<AiList> lists);
    // Empties both sets.
    void Clear();

    // ailistFindById: a global id (1024 and below) searches the global lists,
    // any other the setup's. Null when there is no such list.
    const AiList* FindById(int32_t id) const;
    const std::vector<AiList>& Local() const {
        return local_;
    }
    const std::vector<AiList>& Global() const {
        return global_;
    }

  private:
    std::vector<AiList> global_;
    std::vector<AiList> local_;
};

// A reader of one command's fields: big-endian, as the game's ntohs and ntohl
// read them.
class AiCommand {
  public:
    AiCommand(const uint8_t* list, size_t size, size_t offset) : bytes_(list + offset), remaining_(size - offset) {
    }

    uint8_t Cmd() const {
        return bytes_[0];
    }
    uint8_t U8(size_t field) const {
        return field < remaining_ ? bytes_[field] : 0;
    }
    int8_t S8(size_t field) const {
        return static_cast<int8_t>(U8(field));
    }
    uint16_t U16(size_t field) const {
        return static_cast<uint16_t>((U8(field) << 8) | U8(field + 1));
    }
    int16_t S16(size_t field) const {
        return static_cast<int16_t>(U16(field));
    }
    uint32_t U32(size_t field) const {
        return (static_cast<uint32_t>(U16(field)) << 16) | U16(field + 2);
    }
    int32_t S32(size_t field) const {
        return static_cast<int32_t>(U32(field));
    }
    // CharArrayTo24.
    uint32_t U24(size_t field) const {
        return (static_cast<uint32_t>(U8(field)) << 16) | (static_cast<uint32_t>(U8(field + 1)) << 8) | U8(field + 2);
    }

  private:
    const uint8_t* bytes_;
    size_t remaining_;
};

} // namespace Frigate::Level
