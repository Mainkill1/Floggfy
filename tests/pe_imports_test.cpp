#include "../native/pe_imports.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

template<class T>
void Put(std::vector<std::uint8_t>& image, std::size_t offset, T value) {
    assert(offset + sizeof(value) <= image.size());
    std::memcpy(image.data() + offset, &value, sizeof(value));
}

void Text(std::vector<std::uint8_t>& image, std::size_t offset, const char* value) {
    const std::size_t size = std::strlen(value) + 1;
    assert(offset + size <= image.size());
    std::memcpy(image.data() + offset, value, size);
}

std::vector<std::uint8_t> Image(bool normal, bool delay) {
    std::vector<std::uint8_t> image(4096);
    Put<std::uint16_t>(image, 0x00, 0x5a4d);
    Put<std::uint32_t>(image, 0x3c, 0x80);
    Put<std::uint32_t>(image, 0x80, 0x00004550);
    Put<std::uint16_t>(image, 0x84, 0x8664);
    Put<std::uint16_t>(image, 0x94, 240);
    Put<std::uint16_t>(image, 0x98, 0x20b);
    Put<std::uint32_t>(image, 0x98 + 56, 4096);
    Put<std::uint32_t>(image, 0x98 + 108, 16);

    if (normal) {
        Put<std::uint32_t>(image, 0x98 + 112 + 8, 0x200);
        Put<std::uint32_t>(image, 0x98 + 112 + 12, 40);
        Put<std::uint32_t>(image, 0x200, 0x300);
        Put<std::uint32_t>(image, 0x20c, 0x280);
        Put<std::uint32_t>(image, 0x210, 0x380);
        Text(image, 0x280, "ole32.dll");
        Put<std::uint64_t>(image, 0x300, 0x400);
        Put<std::uint64_t>(image, 0x380, 0x11111111);
        Text(image, 0x402, "CoCreateInstance");
    }
    if (delay) {
        Put<std::uint32_t>(image, 0x98 + 112 + 13 * 8, 0x500);
        Put<std::uint32_t>(image, 0x98 + 116 + 13 * 8, 64);
        Put<std::uint32_t>(image, 0x500, 1);
        Put<std::uint32_t>(image, 0x504, 0x580);
        Put<std::uint32_t>(image, 0x50c, 0x680);
        Put<std::uint32_t>(image, 0x510, 0x600);
        Text(image, 0x580, "ole32.dll");
        Put<std::uint64_t>(image, 0x600, 0x700);
        Put<std::uint64_t>(image, 0x680, 0x22222222);
        Text(image, 0x702, "CoCreateInstance");
    }
    return image;
}

} // namespace

int main() {
    auto normal = Image(true, false);
    auto normal_result = hooks::FindImportSlots(normal.data(), normal.size(),
                                                 "ole32.dll", "CoCreateInstance");
    assert(normal_result.normal == reinterpret_cast<void**>(normal.data() + 0x380));
    assert(normal_result.delay == nullptr);
    assert(!normal_result.malformed);

    auto delay = Image(false, true);
    auto delay_result = hooks::FindImportSlots(delay.data(), delay.size(),
                                               "OLE32.DLL", "CoCreateInstance");
    assert(delay_result.normal == nullptr);
    assert(delay_result.delay == reinterpret_cast<void**>(delay.data() + 0x680));
    assert(!delay_result.malformed);

    auto provider_independent = Image(true, true);
    Text(provider_independent, 0x280, "combase.dll");
    Text(provider_independent, 0x580, "api-ms-win-core-com-l1-1-0.dll");
    auto provider_independent_result = hooks::FindImportSlots(
        provider_independent.data(), provider_independent.size(), nullptr,
        "CoCreateInstance");
    assert(provider_independent_result.normal ==
           reinterpret_cast<void**>(provider_independent.data() + 0x380));
    assert(provider_independent_result.delay ==
           reinterpret_cast<void**>(provider_independent.data() + 0x680));
    assert(!provider_independent_result.malformed);

    auto both = Image(true, true);
    auto both_result = hooks::FindImportSlots(both.data(), both.size(),
                                              "ole32.dll", "CoCreateInstance");
    assert(both_result.normal == reinterpret_cast<void**>(both.data() + 0x380));
    assert(both_result.delay == reinterpret_cast<void**>(both.data() + 0x680));

    auto zero_normal_iat = Image(true, false);
    Put<std::uint32_t>(zero_normal_iat, 0x210, 0);
    auto zero_normal_result = hooks::FindImportSlots(zero_normal_iat.data(), zero_normal_iat.size(),
                                                     "ole32.dll", "CoCreateInstance");
    assert(zero_normal_result.normal == nullptr);
    assert(zero_normal_result.malformed);

    auto zero_delay_iat = Image(false, true);
    Put<std::uint32_t>(zero_delay_iat, 0x50c, 0);
    auto zero_delay_result = hooks::FindImportSlots(zero_delay_iat.data(), zero_delay_iat.size(),
                                                    "ole32.dll", "CoCreateInstance");
    assert(zero_delay_result.delay == nullptr);
    assert(zero_delay_result.malformed);

    both.resize(0x690);
    auto truncated = hooks::FindImportSlots(both.data(), both.size(),
                                            "ole32.dll", "CoCreateInstance");
    assert(truncated.normal == reinterpret_cast<void**>(both.data() + 0x380));
    assert(truncated.delay == nullptr);
    assert(truncated.malformed);
}
