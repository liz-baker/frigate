#include "level/AiLists.h"

#include <charconv>
#include <string>
#include <utility>

namespace Frigate::Level {

size_t AiItemSize(const uint8_t* list, size_t size, size_t offset) {
    if (offset >= size) {
        return 1;
    }
    if (list[offset] == AI_PRINT) {
        // The string ends at the next zero byte.
        size_t position = offset + 1;
        while (position < size && list[position] != 0) {
            ++position;
        }
        return (position - offset) + 1;
    }
    return kAiCommandSizes[list[offset]];
}

size_t AiGoToLabel(const uint8_t* list, size_t size, size_t offset, uint8_t label) {
    for (;;) {
        if (offset >= size || list[offset] == AI_EndList) {
            return 0;
        }
        if (list[offset] == AI_Label && offset + 1 < size && list[offset + 1] == label) {
            return offset;
        }
        offset += AiItemSize(list, size, offset);
    }
}

bool AiLists::ParseGlobals(std::string_view text) {
    std::vector<AiList> lists;
    while (!text.empty()) {
        const size_t lineEnd = text.find('\n');
        const std::string_view line = text.substr(0, lineEnd);
        text = lineEnd == std::string_view::npos ? std::string_view{} : text.substr(lineEnd + 1);
        if (line.empty()) {
            continue;
        }
        const size_t tab = line.find('\t');
        if (tab == std::string_view::npos) {
            return false;
        }
        AiList list;
        const auto [end, error] = std::from_chars(line.data(), line.data() + tab, list.id);
        if (error != std::errc{} || end != line.data() + tab) {
            return false;
        }
        const std::string_view hex = line.substr(tab + 1);
        if (hex.size() % 2 != 0) {
            return false;
        }
        for (size_t i = 0; i < hex.size(); i += 2) {
            unsigned value = 0;
            const auto [byteEnd, byteError] = std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
            if (byteError != std::errc{} || byteEnd != hex.data() + i + 2) {
                return false;
            }
            list.bytes.push_back(static_cast<uint8_t>(value));
        }
        lists.push_back(std::move(list));
    }
    global_ = std::move(lists);
    return true;
}

void AiLists::SetLocal(std::vector<AiList> lists) {
    local_ = std::move(lists);
}

void AiLists::Clear() {
    global_.clear();
    local_.clear();
}

const AiList* AiLists::FindById(int32_t id) const {
    const std::vector<AiList>& lists = IsGlobalAiListId(id) ? global_ : local_;
    for (const AiList& list : lists) {
        if (list.id == id) {
            return &list;
        }
    }
    return nullptr;
}

} // namespace Frigate::Level
