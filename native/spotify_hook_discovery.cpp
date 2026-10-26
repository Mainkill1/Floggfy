#include "spotify_hook_discovery.h"

#include <algorithm>
#include <cstring>

namespace spotify {
namespace {

constexpr std::uint32_t kExecutable = 0x20000000;
constexpr std::size_t kSectionSize = 40;
constexpr std::size_t kRuntimeFunctionSize = 12;

constexpr std::uint8_t kOggPageSeek[] = {
    0x0f,0xb6,0x47,0x1a,0x8d,0x48,0x1b,0x3b,0xe9,0x0f,0x8c,0x1e,0x01,
    0x00,0x00,0x33,0xd2,0x85,0xc0,0x74,0x20,0x44,0x8b,0x43,0x1c,
};
constexpr std::uint8_t kFormatSniff[] = {
    0x48,0x89,0x55,0x88,0x4c,0x8b,0xf9,0xc7,0x44,0x24,0x3c,0x4f,0x67,
    0x67,0x53,0xc7,0x44,0x24,0x40,0x53,0x70,0x41,0x43,0x66,0xc7,0x44,
    0x24,0x38,0x49,0x44,0xc6,0x44,0x24,0x3a,0x33,0xc7,0x44,0x24,0x44,
    0x66,0x74,0x79,0x70,0xc7,0x44,0x24,0x48,0x66,0x4c,0x61,0x43,0xc7,
    0x44,0x24,0x4c,0x52,0x49,0x46,0x46,0xc7,0x44,0x24,0x50,0x57,0x41,
    0x56,0x45,
};
constexpr std::uint8_t kFlacInit[] = {
    0x48,0x8b,0x57,0x20,0x48,0x89,0x47,0x20,0x48,0x85,0xd2,0x74,0x0a,
    0x48,0x8b,0x47,0x18,0x48,0x8b,0xcf,0xff,
};
constexpr std::uint8_t kFlacRead[] = {
    0x4c,0x8d,0x41,0x10,0x40,0x38,0x79,0x38,0x74,0x09,0x49,0x8b,0x48,
    0x08,0x49,0x2b,0x08,0xeb,0x1e,0x49,0x8b,0x48,0x20,0x49,0x3b,0x48,
    0x18,0x72,0x06,0x49,0x2b,0x48,0x18,
};
constexpr std::uint8_t kFlacFrame[] = {
    0x83,0x7a,0x08,0x02,0x75,0x64,0x48,0x39,0x41,0x08,0x74,0x5e,0x49,
    0x39,0x00,0x74,0x59,0x49,0x39,0x40,0x08,0x74,0x53,0x83,0x7a,0x10,
    0x10,0x74,0x06,0x83,0x7a,0x10,0x18,0x75,0x47,
};
constexpr std::uint8_t kFlacError[] = {
    0x48,0x8b,0xd9,0x48,0x63,0xfa,0x75,0x2b,0x85,0xd2,0x75,0x27,
};

struct Pattern {
    TargetKind kind;
    const std::uint8_t* bytes;
    std::size_t size;
    std::uint32_t minimum_function_size;
    std::uint32_t maximum_function_size;
};

constexpr Pattern kPatterns[] = {
    {TargetKind::OggPageSeek, kOggPageSeek, sizeof(kOggPageSeek), 0x140, 0x300},
    {TargetKind::FormatSniff, kFormatSniff, sizeof(kFormatSniff), 0x800, 0x1000},
    {TargetKind::FlacInit, kFlacInit, sizeof(kFlacInit), 0x180, 0x400},
    {TargetKind::FlacRead, kFlacRead, sizeof(kFlacRead), 0xa0, 0x200},
    {TargetKind::FlacFrame, kFlacFrame, sizeof(kFlacFrame), 0x80, 0x200},
    {TargetKind::FlacError, kFlacError, sizeof(kFlacError), 0x60, 0x200},
};

template<class T>
bool Read(const std::uint8_t* image, std::size_t size, std::size_t offset,
          T& value) noexcept {
    if (offset > size || sizeof(T) > size - offset) return false;
    std::memcpy(&value, image + offset, sizeof(T));
    return true;
}

bool Range(std::size_t size, std::size_t offset, std::size_t length) noexcept {
    return offset <= size && length <= size - offset;
}

struct Image {
    const std::uint8_t* base = nullptr;
    std::size_t available = 0;
    std::uint32_t image_size = 0;
    std::size_t sections = 0;
    std::uint16_t section_count = 0;
    std::uint32_t exceptions = 0;
    std::uint32_t exception_size = 0;
};

bool ParseImage(const std::uint8_t* base, std::size_t available,
                Image& image) noexcept {
    if (!base || available < 0x40) return false;
    std::uint16_t dos = 0, machine = 0, optional_size = 0, magic = 0;
    std::uint32_t nt = 0, signature = 0, directory_count = 0;
    if (!Read(base, available, 0, dos) || dos != 0x5a4d ||
        !Read(base, available, 0x3c, nt) || nt > 0x100000 ||
        !Read(base, available, nt, signature) || signature != 0x00004550 ||
        !Read(base, available, nt + 4, machine) || machine != 0x8664 ||
        !Read(base, available, nt + 6, image.section_count) || !image.section_count ||
        !Read(base, available, nt + 20, optional_size) || optional_size < 240 ||
        !Read(base, available, nt + 24, magic) || magic != 0x20b ||
        !Read(base, available, nt + 24 + 56, image.image_size) ||
        !image.image_size || image.image_size > available ||
        !Read(base, available, nt + 24 + 108, directory_count) ||
        directory_count <= 3 ||
        !Read(base, available, nt + 24 + 112 + 3 * 8, image.exceptions) ||
        !Read(base, available, nt + 24 + 112 + 3 * 8 + 4, image.exception_size))
        return false;
    image.sections = std::size_t{nt} + 24 + optional_size;
    image.base = base;
    image.available = available;
    return Range(available, image.sections,
                 std::size_t{image.section_count} * kSectionSize) &&
           image.exception_size >= kRuntimeFunctionSize &&
           image.exception_size % kRuntimeFunctionSize == 0 &&
           Range(image.image_size, image.exceptions, image.exception_size);
}

bool ExecutableRange(const Image& image, std::uint32_t rva,
                     std::size_t length) noexcept {
    const std::uint64_t finish = std::uint64_t{rva} + length;
    for (std::uint16_t index = 0; index < image.section_count; ++index) {
        const std::size_t section = image.sections + std::size_t{index} * kSectionSize;
        std::uint32_t virtual_size = 0, virtual_address = 0, raw_size = 0, flags = 0;
        if (!Read(image.base, image.available, section + 8, virtual_size) ||
            !Read(image.base, image.available, section + 12, virtual_address) ||
            !Read(image.base, image.available, section + 16, raw_size) ||
            !Read(image.base, image.available, section + 36, flags)) return false;
        const std::uint64_t end = std::uint64_t{virtual_address} +
                                  std::max(virtual_size, raw_size);
        if ((flags & kExecutable) && rva >= virtual_address && finish <= end &&
            finish <= image.image_size) return true;
    }
    return false;
}

bool RuntimeFunction(const Image& image, std::uint32_t instruction,
                     std::size_t length, std::uint32_t& begin,
                     std::uint32_t& end) noexcept {
    const std::size_t count = image.exception_size / kRuntimeFunctionSize;
    std::size_t low = 0, high = count;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        std::uint32_t candidate = 0;
        if (!Read(image.base, image.available,
                  image.exceptions + middle * kRuntimeFunctionSize, candidate)) return false;
        if (candidate <= instruction) low = middle + 1; else high = middle;
    }
    if (!low) return false;
    const std::size_t entry = image.exceptions + (low - 1) * kRuntimeFunctionSize;
    if (!Read(image.base, image.available, entry, begin) ||
        !Read(image.base, image.available, entry + 4, end) ||
        begin >= end || instruction < begin ||
        std::uint64_t{instruction} + length > end ||
        !ExecutableRange(image, begin, end - begin)) return false;
    return true;
}

DiscoveryResult FindTarget(const Image& image, const Pattern& pattern,
                           HookTarget& target) noexcept {
    std::uint32_t function = 0;
    bool found = false;
    for (std::uint16_t index = 0; index < image.section_count; ++index) {
        const std::size_t section = image.sections + std::size_t{index} * kSectionSize;
        std::uint32_t virtual_size = 0, virtual_address = 0, raw_size = 0, flags = 0;
        if (!Read(image.base, image.available, section + 8, virtual_size) ||
            !Read(image.base, image.available, section + 12, virtual_address) ||
            !Read(image.base, image.available, section + 16, raw_size) ||
            !Read(image.base, image.available, section + 36, flags))
            return DiscoveryResult::InvalidImage;
        if (!(flags & kExecutable) || virtual_address >= image.image_size) continue;
        const std::size_t extent = std::min<std::size_t>(
            std::max(virtual_size, raw_size), image.image_size - virtual_address);
        if (extent < pattern.size) continue;
        const std::uint8_t* const section_start = image.base + virtual_address;
        const std::uint8_t* cursor = section_start;
        const std::uint8_t* const finish = cursor + extent - pattern.size + 1;
        while (cursor < finish) {
            const void* next = std::memchr(cursor, pattern.bytes[0],
                                           static_cast<std::size_t>(finish - cursor));
            if (!next) break;
            cursor = static_cast<const std::uint8_t*>(next);
            if (std::memcmp(cursor, pattern.bytes, pattern.size) == 0) {
                const std::uint32_t hit = virtual_address +
                    static_cast<std::uint32_t>(cursor - section_start);
                std::uint32_t begin = 0, end = 0;
                if (RuntimeFunction(image, hit, pattern.size, begin, end) &&
                    end - begin >= pattern.minimum_function_size &&
                    end - begin <= pattern.maximum_function_size) {
                    if (found && function != begin)
                        return DiscoveryResult::AmbiguousTarget;
                    found = true;
                    function = begin;
                }
            }
            ++cursor;
        }
    }
    if (!found) return DiscoveryResult::MissingTarget;
    target = HookTarget{pattern.kind, function};
    return DiscoveryResult::Found;
}

bool Gap(std::uint32_t lower, std::uint32_t upper,
         std::uint32_t minimum, std::uint32_t maximum) noexcept {
    return upper > lower && upper - lower >= minimum && upper - lower <= maximum;
}

bool Consistent(const HookTargets& targets) noexcept {
    const auto error = TargetRva(targets, TargetKind::FlacError);
    const auto init = TargetRva(targets, TargetKind::FlacInit);
    const auto read = TargetRva(targets, TargetKind::FlacRead);
    const auto frame = TargetRva(targets, TargetKind::FlacFrame);
    const auto sniff = TargetRva(targets, TargetKind::FormatSniff);
    const auto ogg = TargetRva(targets, TargetKind::OggPageSeek);
    return Gap(error, init, 0x400, 0x800) &&
           Gap(init, read, 0x500, 0xa00) &&
           Gap(read, frame, 0x200, 0x600) &&
           Gap(frame, sniff, 0x3000, 0x10000) &&
           Gap(sniff, ogg, 0x10000, 0x40000);
}

} // namespace

DiscoveryResult DiscoverHookTargets(const std::uint8_t* mapped_image,
                                    std::size_t available,
                                    HookTargets& targets) noexcept {
    targets = {};
    Image image{};
    if (!ParseImage(mapped_image, available, image))
        return DiscoveryResult::InvalidImage;
    for (std::size_t index = 0; index < std::size(kPatterns); ++index) {
        const auto result = FindTarget(image, kPatterns[index], targets.entries[index]);
        if (result != DiscoveryResult::Found) {
            targets = {};
            return result;
        }
    }
    if (!Consistent(targets)) {
        targets = {};
        return DiscoveryResult::InconsistentLayout;
    }
    return DiscoveryResult::Found;
}

std::uint32_t TargetRva(const HookTargets& targets, TargetKind kind) noexcept {
    for (const auto& target : targets.entries)
        if (target.kind == kind) return target.rva;
    return 0;
}

DiscoveryResult DiscoverAacHookTargets(const std::uint8_t* mapped_image,
    std::size_t available,AacHookTargets& targets) noexcept {
    targets={};Image image{};
    if(!ParseImage(mapped_image,available,image))return DiscoveryResult::InvalidImage;
    // SpacReader constructor and incremental compressed-input method. Resolve
    // function starts through x64 unwind metadata, then cross-check the vtable.
    static constexpr std::uint8_t initialize[]={
        0x66,0x89,0xb7,0x38,0x01,0x00,0x00,0x88,0x9f,0x3a,0x01,0x00,0x00,
        0x40,0x88,0xb7,0x3b,0x01,0x00,0x00};
    static constexpr std::uint8_t push[]={
        0x4c,0x8b,0xea,0x48,0x8b,0xf9,0x4d,0x8b,0x38,0x4d,0x8b,0x60,0x08,
        0x4d,0x03,0xe7,0x45,0x33,0xf6,0x83,0xce,0xff,0x44,0x39,0x77,0x58};
    HookTarget ctor{},feed{};
    auto result=FindTarget(image,{TargetKind::AacInitialize,initialize,sizeof(initialize),0xc0,0x180},ctor);
    if(result!=DiscoveryResult::Found)return result;
    result=FindTarget(image,{TargetKind::AacPush,push,sizeof(push),0x400,0x650},feed);
    if(result!=DiscoveryResult::Found)return result;
    // Audited constructor prefix: lea rax,[rip+vtable]; mov [rcx],rax.
    const std::uint8_t lea[]={0x48,0x8d,0x05};
    const std::uint8_t store[]={0x48,0x89,0x01};
    if(!Range(available,ctor.rva+0x14,10) ||
       std::memcmp(mapped_image+ctor.rva+0x14,lea,sizeof(lea)) ||
       std::memcmp(mapped_image+ctor.rva+0x1b,store,sizeof(store)))return DiscoveryResult::InconsistentLayout;
    std::int32_t relative=0;
    if(!Read(mapped_image,available,ctor.rva+0x17,relative))return DiscoveryResult::InvalidImage;
    const auto table=std::int64_t(ctor.rva)+0x1b+relative;
    if(table<0 || !Range(image.image_size,std::size_t(table),24))return DiscoveryResult::InconsistentLayout;
    std::uint64_t destroy_pointer=0,push_pointer=0;
    if(!Read(mapped_image,available,std::size_t(table),destroy_pointer) ||
       !Read(mapped_image,available,std::size_t(table)+16,push_pointer))return DiscoveryResult::InvalidImage;
    const auto base=reinterpret_cast<std::uintptr_t>(mapped_image);
    if(push_pointer!=base+feed.rva || destroy_pointer<base || destroy_pointer-base>=image.image_size)
        return DiscoveryResult::InconsistentLayout;
    const auto destroy=std::uint32_t(destroy_pointer-base);
    std::uint32_t begin=0,end=0;
    if(!RuntimeFunction(image,destroy,1,begin,end) || begin!=destroy || end-begin<0x20 || end-begin>0x100)
        return DiscoveryResult::InconsistentLayout;
    targets={ctor.rva,feed.rva,destroy};return DiscoveryResult::Found;
}

namespace {
template<std::size_t N> bool ConnectBytes(const Image& image,std::uint32_t at,
                                        const std::uint8_t (&bytes)[N]) noexcept {
    return Range(image.image_size,at,N)&&!std::memcmp(image.base+at,bytes,N);
}
bool ConnectCall(const Image& image,std::uint32_t at,std::uint32_t owner_begin,
                 std::uint32_t owner_end,std::uint32_t& target,bool require_entry=true) noexcept {
    if(at<owner_begin || at>=owner_end || owner_end-at<5 || image.base[at]!=0xe8)return false;
    std::int32_t displacement=0;
    if(!Read(image.base,image.image_size,at+1,displacement))return false;
    const auto destination=std::int64_t(at)+5+displacement;
    if(destination<0 || destination>=image.image_size)return false;
    target=std::uint32_t(destination);
    if(!require_entry)return ExecutableRange(image,target,1);
    std::uint32_t begin=0,end=0;
    return RuntimeFunction(image,target,1,begin,end)&&begin==target;
}
bool ConnectInflaterConstructor(const Image& image,std::uint32_t at) noexcept {
    // Audited leaf constructor has no .pdata entry. Its complete body only
    // installs the native vtable/sink and clears state, then returns this.
    static constexpr std::uint8_t lea[]={0x48,0x8d,0x05};
    static constexpr std::uint8_t tail[]={0x48,0x89,0x51,0x08,0x48,0x89,0x01,0x48,0x8b,0xc1,
        0x48,0xc7,0x41,0x10,0x00,0x00,0x00,0x00,0xc3};
    std::int32_t displacement=0;
    if(!ExecutableRange(image,at,26)||!ConnectBytes(image,at,lea)||!ConnectBytes(image,at+7,tail)||
       !Read(image.base,image.image_size,at+3,displacement))return false;
    const auto table=std::int64_t(at)+7+displacement;
    return table>=0&&Range(image.image_size,std::size_t(table),8);
}
bool ConnectLiteral(const Image& image,std::uint32_t at,const char* text) noexcept {
    if(!Range(image.image_size,at,7))return false;
    const auto* p=image.base+at;
    // RIP-relative LEA, any destination register; never scan raw pointer values.
    if((p[0]!=0x48&&p[0]!=0x4c)||p[1]!=0x8d||(p[2]&0xc7)!=0x05)return false;
    std::int32_t displacement=0;Read(image.base,image.image_size,at+3,displacement);
    const auto literal=std::int64_t(at)+7+displacement;
    const auto length=std::strlen(text)+1;
    return literal>=0&&Range(image.image_size,std::size_t(literal),length)&&
        !std::memcmp(image.base+literal,text,length);
}
bool ConnectHasLiteral(const Image& image,std::uint32_t begin,std::uint32_t end,const char* text) noexcept {
    for(auto at=begin;end-at>=7;++at)if(ConnectLiteral(image,at,text))return true;
    return false;
}
// This is the audited const-string adapter, not a speculative C++ JSON ABI.
// It reads size/capacity, selects SSO data, builds a borrowed pointer/length
// view, calls the lower parser, and returns AL without destroying the input.
bool ConnectStringAdapter(const Image& image,std::uint32_t wrapper) noexcept {
    static constexpr std::uint8_t prefix[]={
        0x48,0x83,0xec,0x38,0x48,0x83,0x7a,0x18,0x0f,0x48,0x8b,0xc2,0x76,0x03,
        0x48,0x8b,0x02,0x48,0x89,0x44,0x24,0x20,0x48,0x8b,0x42,0x10,0x48,0x8d,0x54,0x24,0x20,
        0x48,0x89,0x44,0x24,0x28};
    static constexpr std::uint8_t suffix[]={0x48,0x83,0xc4,0x38,0xc3};
    std::uint32_t begin=0,end=0,lower=0;
    return RuntimeFunction(image,wrapper,46,begin,end)&&begin==wrapper&&end-wrapper==46&&
        ConnectBytes(image,wrapper,prefix)&&ConnectBytes(image,wrapper+41,suffix)&&
        ConnectCall(image,wrapper+36,begin,end,lower)&&lower!=wrapper;
}

constexpr std::uint32_t kConnectCallSize = 5;
constexpr std::uint32_t kConnectLiteralInstructionSize = 7;
constexpr std::uint32_t kConnectMinimumCommandSize = 0x400;
constexpr std::uint32_t kConnectMaximumCommandSize = 0x1000;

struct ConnectCommandFunction {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

struct ConnectParserCalls {
    std::uint32_t decoded = 0;
    std::uint32_t plain = 0;
    std::uint32_t wrapper = 0;
};

DiscoveryResult FindConnectCommandFunction(const Image& image,
                                           ConnectCommandFunction& command) noexcept {
    unsigned anchors = 0;
    // Runtime entries bound every scan and establish the actual function owner.
    // No allocations, imports, storage probes or production RVAs are involved.
    for (std::size_t i = 0; i < image.exception_size / kRuntimeFunctionSize; ++i) {
        std::uint32_t begin = 0, end = 0;
        Read(image.base, image.image_size, image.exceptions + i * kRuntimeFunctionSize, begin);
        Read(image.base, image.image_size, image.exceptions + i * kRuntimeFunctionSize + 4, end);
        if (begin >= end || !ExecutableRange(image, begin, end - begin)) continue;
        for (auto at = begin; end - at >= kConnectLiteralInstructionSize; ++at) {
            if (!ConnectLiteral(image, at, "Received player command: %s")) continue;
            // Count references, including multiple references in the same owner.
            if (++anchors > 1) return DiscoveryResult::AmbiguousTarget;
            command = {begin, end};
        }
    }
    return anchors ? DiscoveryResult::Found : DiscoveryResult::MissingTarget;
}

bool VerifyConnectCommandInput(const Image& image,
                               const ConnectCommandFunction& command) noexcept {
    const auto owner = command.begin;
    const auto finish = command.end;
    if (finish - owner < kConnectMinimumCommandSize ||
        finish - owner > kConnectMaximumCommandSize ||
        !ConnectHasLiteral(image, owner, finish, "sent_by_device_id") ||
        !ConnectHasLiteral(image, owner, finish, "Unable to parse connect player command:\n%s"))
        return false;

    // mov rsi,rdx; mov rdi,rcx saves the input separately from the output.
    static constexpr std::uint8_t input_save[] = {0x48,0x8b,0xf2,0x48,0x8b,0xf9};
    // mov rcx,rdx; call compression_check; mov rdx,[rsi+20h].
    // The final load obtains the native body string from the saved input.
    static constexpr std::uint8_t input_body[] = {0x48,0x8b,0xca};
    static constexpr std::uint8_t body_load[] = {0x48,0x8b,0x56,0x20};
    constexpr std::uint32_t kCompressionCheckOffset = 3;
    constexpr std::uint32_t kBodyLoadOffset = 8;
    constexpr std::uint32_t kBodySequenceSize = 12;
    bool input = false, body = false;
    for (auto at = owner; at < finish; ++at) {
        if (ConnectBytes(image, at, input_save) && finish - at >= sizeof(input_save)) input = true;
        std::uint32_t compression_check = 0;
        if (finish - at >= kBodySequenceSize && ConnectBytes(image, at, input_body) &&
            ConnectCall(image, at + kCompressionCheckOffset, owner, finish, compression_check) &&
            ConnectBytes(image, at + kBodyLoadOffset, body_load)) body = true;
    }
    return input && body;
}

bool VerifyConnectParserCalls(const Image& image, const ConnectCommandFunction& command,
                              ConnectParserCalls& calls) noexcept {
    const auto owner = command.begin;
    const auto finish = command.end;
    // lea rdx,[rbp-78h]; lea rcx,[rsp+48h] supplies decoded string/output locals.
    static constexpr std::uint8_t decoded[] = {0x48,0x8d,0x55,0x88,0x48,0x8d,0x4c,0x24,0x48};
    // Complete-command parses share [rsp+48h]; the envelope uses [rbp-30h].
    static constexpr std::uint8_t complete[] = {0x48,0x8d,0x4c,0x24,0x48};
    static constexpr std::uint8_t envelope[] = {0x48,0x8d,0x4d,0xd0};
    static constexpr std::uint8_t test[] = {0x84,0xc0}; // test al,al checks parser success.
    constexpr std::uint32_t kDecodedCallOffset = 9;
    constexpr std::uint32_t kDecodedTestOffset = 14;
    constexpr std::uint32_t kDecodedSequenceSize = 16;
    constexpr std::uint32_t kCompleteArgumentSize = 5;
    constexpr std::uint32_t kEnvelopeArgumentSize = 4;
    constexpr std::uint32_t kCallAndTestSize = 7;
    unsigned decoded_count = 0, complete_count = 0;
    for (auto at = owner; finish - at >= kDecodedSequenceSize; ++at) {
        std::uint32_t target = 0;
        if (ConnectBytes(image, at, decoded) &&
            ConnectCall(image, at + kDecodedCallOffset, owner, finish, target) &&
            ConnectBytes(image, at + kDecodedTestOffset, test)) {
            if (++decoded_count > 1) return false;
            calls.decoded = at + kDecodedCallOffset;
            calls.wrapper = target;
        }
    }
    if (decoded_count != 1 || !ConnectStringAdapter(image, calls.wrapper)) return false;
    unsigned envelope_count = 0, total_calls = 0;
    for (auto at = owner; finish - at >= kConnectCallSize; ++at) {
        std::uint32_t target = 0;
        if (!ConnectCall(image, at, owner, finish, target) || target != calls.wrapper) continue;
        ++total_calls;
        if (at >= owner + kCompleteArgumentSize && finish - at >= kCallAndTestSize &&
            ConnectBytes(image, at - kCompleteArgumentSize, complete) &&
            ConnectBytes(image, at + kConnectCallSize, test)) {
            ++complete_count;
            if (at != calls.decoded) calls.plain = at;
        } else if (at >= owner + kEnvelopeArgumentSize && finish - at >= kCallAndTestSize &&
                   ConnectBytes(image, at - kEnvelopeArgumentSize, envelope) &&
                   ConnectBytes(image, at + kConnectCallSize, test)) ++envelope_count;
    }
    return total_calls == 3 && complete_count == 2 && envelope_count == 1 &&
           calls.plain && calls.plain > calls.decoded;
}

bool VerifyConnectInflaterPipeline(const Image& image, const ConnectCommandFunction& command,
                                   const ConnectParserCalls& calls) noexcept {
    const auto owner = command.begin;
    const auto finish = command.end;
    // The decoded parse follows feed AND finalization. A feed-return observer
    // would be incomplete because the finalizer can flush additional bytes.
    // create: lea rdx,[rsp+38h]; lea rcx,[rsp+70h] passes sink/inflater locals.
    static constexpr std::uint8_t create[] = {0x48,0x8d,0x54,0x24,0x38,0x48,0x8d,0x4c,0x24,0x70};
    // feed selects the input's inline/heap data, loads its size into r8d, then
    // passes the same inflater local in rcx. finalize reuses that local.
    static constexpr std::uint8_t feed[] = {0x48,0x8d,0x55,0xa8,0x48,0x39,0x5d,0xc0,0x48,0x0f,0x47,0x55,0xa8,
        0x44,0x8b,0x45,0xb8,0x48,0x8d,0x4c,0x24,0x70};
    static constexpr std::uint8_t finalize[] = {0x48,0x8d,0x4c,0x24,0x70};
    // Offsets from create's first LEA: create[10], call[5], feed[22],
    // call[5], finalize[5], call[5], decoded arguments[9], parser call.
    constexpr std::uint32_t kConstructorCallOffset = 10;
    constexpr std::uint32_t kFeedArgumentsOffset = 15;
    constexpr std::uint32_t kFeedCallOffset = 37;
    constexpr std::uint32_t kFinalizeArgumentsOffset = 42;
    constexpr std::uint32_t kFinalizeCallOffset = 47;
    constexpr std::uint32_t kDecodedCallOffset = 61;
    if (calls.decoded < owner + kDecodedCallOffset) return false;
    const auto pipeline = calls.decoded - kDecodedCallOffset;
    std::uint32_t ctor = 0, push = 0, close = 0;
    return ConnectBytes(image, pipeline, create) &&
           ConnectBytes(image, pipeline + kFeedArgumentsOffset, feed) &&
           ConnectBytes(image, pipeline + kFinalizeArgumentsOffset, finalize) &&
           ConnectCall(image, pipeline + kConstructorCallOffset, owner, finish, ctor, false) &&
           ConnectInflaterConstructor(image, ctor) &&
           ConnectCall(image, pipeline + kFeedCallOffset, owner, finish, push) &&
           ConnectCall(image, pipeline + kFinalizeCallOffset, owner, finish, close) &&
           ctor != push && ctor != close && push != close &&
           ctor != calls.wrapper && push != calls.wrapper && close != calls.wrapper;
}
} // namespace

DiscoveryResult DiscoverConnectDecodeTargets(const std::uint8_t* mapped_image,
    std::size_t available, ConnectDecodeTargets& targets) noexcept {
    targets = {};
    Image image;
    if (!ParseImage(mapped_image, available, image)) return DiscoveryResult::InvalidImage;
    ConnectCommandFunction command;
    const auto result = FindConnectCommandFunction(image, command);
    if (result != DiscoveryResult::Found) return result;
    ConnectParserCalls calls;
    if (!VerifyConnectCommandInput(image, command) ||
        !VerifyConnectParserCalls(image, command, calls) ||
        !VerifyConnectInflaterPipeline(image, command, calls))
        return DiscoveryResult::InconsistentLayout;
    targets = {calls.wrapper, calls.plain + kConnectCallSize, calls.decoded + kConnectCallSize};
    return DiscoveryResult::Found;
}

} // namespace spotify
