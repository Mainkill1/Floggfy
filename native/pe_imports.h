#pragma once

#include <cstddef>
#include <cstdint>

namespace hooks {

struct ImportSlots {
    void** normal = nullptr;
    void** delay = nullptr;
    bool malformed = false;
};

ImportSlots FindImportSlots(std::uint8_t* image, std::size_t available,
                            const char* dll_name, const char* function_name) noexcept;

} // namespace hooks
