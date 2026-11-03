#include "../native/spotify_hook_discovery.h"

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

constexpr std::uint8_t ogg[] = {
    0x0f,0xb6,0x47,0x1a,0x8d,0x48,0x1b,0x3b,0xe9,0x0f,0x8c,0x1e,0x01,
    0x00,0x00,0x33,0xd2,0x85,0xc0,0x74,0x20,0x44,0x8b,0x43,0x1c,
};
constexpr std::uint8_t sniff[] = {
    0x48,0x89,0x55,0x88,0x4c,0x8b,0xf9,0xc7,0x44,0x24,0x3c,0x4f,0x67,
    0x67,0x53,0xc7,0x44,0x24,0x40,0x53,0x70,0x41,0x43,0x66,0xc7,0x44,
    0x24,0x38,0x49,0x44,0xc6,0x44,0x24,0x3a,0x33,0xc7,0x44,0x24,0x44,
    0x66,0x74,0x79,0x70,0xc7,0x44,0x24,0x48,0x66,0x4c,0x61,0x43,0xc7,
    0x44,0x24,0x4c,0x52,0x49,0x46,0x46,0xc7,0x44,0x24,0x50,0x57,0x41,
    0x56,0x45,
};
constexpr std::uint8_t flac_init[] = {
    0x48,0x8b,0x57,0x20,0x48,0x89,0x47,0x20,0x48,0x85,0xd2,0x74,0x0a,
    0x48,0x8b,0x47,0x18,0x48,0x8b,0xcf,0xff,
};
constexpr std::uint8_t flac_read[] = {
    0x4c,0x8d,0x41,0x10,0x40,0x38,0x79,0x38,0x74,0x09,0x49,0x8b,0x48,
    0x08,0x49,0x2b,0x08,0xeb,0x1e,0x49,0x8b,0x48,0x20,0x49,0x3b,0x48,
    0x18,0x72,0x06,0x49,0x2b,0x48,0x18,
};
constexpr std::uint8_t flac_frame[] = {
    0x83,0x7a,0x08,0x02,0x75,0x64,0x48,0x39,0x41,0x08,0x74,0x5e,0x49,
    0x39,0x00,0x74,0x59,0x49,0x39,0x40,0x08,0x74,0x53,0x83,0x7a,0x10,
    0x10,0x74,0x06,0x83,0x7a,0x10,0x18,0x75,0x47,
};
constexpr std::uint8_t flac_error[] = {
    0x48,0x8b,0xd9,0x48,0x63,0xfa,0x75,0x2b,0x85,0xd2,0x75,0x27,
};
constexpr std::uint8_t decoded_output[] = {
    0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x18,0x49,0x89,0x73,0x20,0x57,
    0x48,0x83,0xec,0x40,0x49,0x8b,0x40,0x08,0x49,0x8b,0xf1,0x49,
    0x89,0x43,0x08,0x49,0x8b,0xd8,
};
constexpr std::uint8_t decoded_output_decode[] = {
    0x41,0xff,0xd2,0x48,0x8b,0x4c,0x24,0x50,0x48,0x29,0x4b,0x08,
    0x48,0x8d,0x04,0x8d,0x00,0x00,0x00,0x00,
};
constexpr std::uint8_t decoded_output_finish[] = {
    0x48,0x01,0x03,0x48,0x8b,0x44,0x24,0x58,0x48,0x01,0x06,
    0x48,0x29,0x46,0x08,0x48,0x8b,0xc7,
};
constexpr std::uint8_t aac_initialize[] = {
    0x66,0x89,0xb7,0x38,0x01,0x00,0x00,0x88,0x9f,0x3a,0x01,0x00,0x00,
    0x40,0x88,0xb7,0x3b,0x01,0x00,0x00,
};
constexpr std::uint8_t aac_push[] = {
    0x4c,0x8b,0xea,0x48,0x8b,0xf9,0x4d,0x8b,0x38,0x4d,0x8b,0x60,0x08,
    0x4d,0x03,0xe7,0x45,0x33,0xf6,0x83,0xce,0xff,0x44,0x39,0x77,0x58,
};
constexpr std::uint8_t simple_return[] = {0xc3};
constexpr std::uint8_t ogg_decoder_prefix[] = {
    0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,
    0x78,0x20,0x4c,0x89,0x40,0x18,0x55,0x41,0x54,0x41,0x55,0x41,0x56,
    0x41,0x57,0x48,0x8b,0xec,0x48,0x81,0xec,0x80,0x00,0x00,0x00,
    0x4d,0x8b,0xe1,0x48,0x8b,0xfa,0x48,0x8b,0xf1,
};

struct Function {
    std::uint32_t begin;
    std::uint32_t end;
    std::uint32_t anchor_offset;
    const std::uint8_t* anchor;
    std::size_t anchor_size;
};

std::vector<std::uint8_t> MappedImage(std::uint32_t shift = 0,
                                      bool duplicate_ogg = false,
                                      bool inconsistent_layout = false) {
    std::vector<std::uint8_t> image(0x50000);
    Put<std::uint16_t>(image, 0, 0x5a4d);
    Put<std::uint32_t>(image, 0x3c, 0x80);
    Put<std::uint32_t>(image, 0x80, 0x00004550);
    Put<std::uint16_t>(image, 0x84, 0x8664);
    Put<std::uint16_t>(image, 0x86, 2);
    Put<std::uint16_t>(image, 0x94, 240);
    Put<std::uint16_t>(image, 0x98, 0x20b);
    Put<std::uint32_t>(image, 0x98 + 56,
                       static_cast<std::uint32_t>(image.size()));
    Put<std::uint32_t>(image, 0x98 + 108, 16);
    Put<std::uint32_t>(image, 0x98 + 112 + 3 * 8, 0x1000);

    const std::size_t sections = 0x98 + 240;
    Put<std::uint32_t>(image, sections + 8, 0x1000);
    Put<std::uint32_t>(image, sections + 12, 0x1000);
    Put<std::uint32_t>(image, sections + 16, 0x1000);
    Put<std::uint32_t>(image, sections + 36, 0x40000040);
    Put<std::uint32_t>(image, sections + 40 + 8, 0x47000);
    Put<std::uint32_t>(image, sections + 40 + 12, 0x2000);
    Put<std::uint32_t>(image, sections + 40 + 16, 0x47000);
    Put<std::uint32_t>(image, sections + 40 + 36, 0x60000020);

    std::vector<Function> functions = {
        {0x3000 + shift, 0x308e + shift, 17, flac_error, sizeof(flac_error)},
        {0x3550 + shift, 0x3761 + shift, 107, flac_init, sizeof(flac_init)},
        {0x3c60 + shift, 0x3d4e + shift, 112, flac_read, sizeof(flac_read)},
        {0x4000 + shift, 0x40b5 + shift, 48, flac_frame, sizeof(flac_frame)},
        {0x4400 + shift, 0x448c + shift, 0, decoded_output, sizeof(decoded_output)},
        {0x5000 + shift, 0x5300 + shift, 0, simple_return, sizeof(simple_return)},
        {0x5400 + shift, 0x582e + shift, 0, simple_return, sizeof(simple_return)},
        {0x5900 + shift, 0x594d + shift, 0, simple_return, sizeof(simple_return)},
        {0x5a00 + shift, 0x5d80 + shift, 0, simple_return, sizeof(simple_return)},
        {0x6000 + shift, 0x6140 + shift, 0x80, aac_initialize, sizeof(aac_initialize)},
        {0x6200 + shift, 0x6700 + shift, 0x20, aac_push, sizeof(aac_push)},
        {0x6800 + shift, 0x6840 + shift, 0, simple_return, sizeof(simple_return)},
        {0x6900 + shift, 0x6d00 + shift, 0, simple_return, sizeof(simple_return)},
        {static_cast<std::uint32_t>((inconsistent_layout ? 0x1a000 : 0x9000) + shift),
         static_cast<std::uint32_t>((inconsistent_layout ? 0x1aaf6 : 0x9af6) + shift),
         55, sniff, sizeof(sniff)},
        {0x2b000 + shift, 0x2b1b4 + shift, 103, ogg, sizeof(ogg)},
    };
    // GCC 15 can diagnose the first growth from an initializer-list-sized
    // allocation as an out-of-bounds construction under -Warray-bounds.
    functions.reserve(functions.size()+2);
    if (duplicate_ogg)
        functions.push_back({0x3b000, 0x3b1b4, 103, ogg, sizeof(ogg)});

    static constexpr std::uint8_t stereo[]={0x48,0x8b,0x53,0x20,0x48,0xd1,0xea,0x48,0x8b,0x01,0xff,0x90,0xc8,0x00,0x00,0x00};
    functions.push_back({0x40000+shift,0x40700+shift,0x48c,stereo,sizeof(stereo)});
    image[0x40000+shift+0x2c9]=0xe8;
    Put<std::int32_t>(image,0x40000+shift+0x2ca,std::int32_t(0x4400)-std::int32_t(0x40000+0x2ce));
    const std::uint8_t pump_decoder[]={0x49,0x8b,0x4e,0x70};
    std::memcpy(image.data()+0x40000+shift+0x2c5,pump_decoder,sizeof(pump_decoder));
    Put<std::uint32_t>(image, 0x98 + 112 + 3 * 8 + 4,
                       static_cast<std::uint32_t>(functions.size() * 12));
    for (std::size_t index = 0; index < functions.size(); ++index) {
        const auto& function = functions[index];
        Put<std::uint32_t>(image, 0x1000 + index * 12, function.begin);
        Put<std::uint32_t>(image, 0x1000 + index * 12 + 4, function.end);
        Put<std::uint32_t>(image, 0x1000 + index * 12 + 8, 0x1800);
        std::memcpy(image.data() + function.begin + function.anchor_offset,
                    function.anchor, function.anchor_size);
    }
    std::memcpy(image.data()+0x4400+shift+0x4e,decoded_output_decode,sizeof(decoded_output_decode));
    std::memcpy(image.data()+0x4400+shift+0x62,decoded_output_finish,sizeof(decoded_output_finish));
    // Ogg outer decoder -> embedded parser (+0x18) -> embedded sync (+0x70)
    // -> wrapper -> existing page function.
    const auto call=[&](std::uint32_t at,std::uint32_t target) {
        image[at]=0xe8;Put<std::int32_t>(image,at+1,std::int32_t(target)-std::int32_t(at+5));
    };
    std::memcpy(image.data()+0x5000+shift,ogg_decoder_prefix,sizeof(ogg_decoder_prefix));
    const std::uint8_t ogg_outer_link[]={0x48,0x8d,0x55,0xb8,0x48,0x8d,0x4e,0x18};
    std::memcpy(image.data()+0x5000+shift+0x178,ogg_outer_link,sizeof(ogg_outer_link));
    call(0x5000+shift+0x180,0x5400+shift);
    const std::uint8_t ogg_parser_link[]={0x48,0x8d,0x99,0x80,0x03,0x00,0x00,
        0x48,0x8d,0x41,0x70,0x48,0x8b,0xd3,0x48,0x8b,0xc8};
    std::memcpy(image.data()+0x5400+shift+0x2c,ogg_parser_link,sizeof(ogg_parser_link));
    call(0x5400+shift+0x3d,0x5900+shift);
    const std::uint8_t ogg_wrapper_prefix[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
    std::memcpy(image.data()+0x5900+shift,ogg_wrapper_prefix,sizeof(ogg_wrapper_prefix));
    const std::uint8_t wrapper_save_context[]={0x48,0x8b,0xd9};
    const std::uint8_t wrapper_forward_context[]={0x48,0x8b,0xd7,0x48,0x8b,0xcb};
    std::memcpy(image.data()+0x5900+shift+0x11,wrapper_save_context,sizeof(wrapper_save_context));
    std::memcpy(image.data()+0x5900+shift+0x2b,wrapper_forward_context,sizeof(wrapper_forward_context));
    call(0x5900+shift+0x31,0x2b000+shift);

    // FLAC keeps RCX in RSI and supplies the same pointer as callback client data.
    const std::uint8_t flac_this[]={0x48,0x8b,0xf1};
    const std::uint8_t flac_client_data[]={0x48,0x89,0x74,0x24,0x48};
    std::memcpy(image.data()+0x3550+shift+0x2f,flac_this,sizeof(flac_this));
    std::memcpy(image.data()+0x3550+shift+0x13c,flac_client_data,sizeof(flac_client_data));
    const auto thunk=[&](std::uint32_t at,const std::uint8_t move[3],std::uint32_t target) {
        std::memcpy(image.data()+at,move,3);image[at+3]=0xe9;
        Put<std::int32_t>(image,at+4,std::int32_t(target)-std::int32_t(at+8));
    };
    const std::uint8_t r9_to_rcx[]={0x49,0x8b,0xc9};
    const std::uint8_t r8_to_rcx[]={0x49,0x8b,0xc8};
    thunk(0x5e00+shift,r9_to_rcx,0x3c60+shift);
    thunk(0x5e10+shift,r9_to_rcx,0x4000+shift);
    thunk(0x5e20+shift,r8_to_rcx,0x3000+shift);

    // AAC constructor installs one vtable containing destroy/decode/push.
    image[0x6000+shift+0x14]=0x48;image[0x6000+shift+0x15]=0x8d;image[0x6000+shift+0x16]=0x05;
    Put<std::int32_t>(image,0x6000+shift+0x17,std::int32_t(0x1a40)-std::int32_t(0x6000+shift+0x1b));
    image[0x6000+shift+0x1b]=0x48;image[0x6000+shift+0x1c]=0x89;image[0x6000+shift+0x1d]=0x01;

    const auto base=reinterpret_cast<std::uintptr_t>(image.data());
    Put<std::uint64_t>(image,0x1a00,base+0x3000+shift);
    Put<std::uint64_t>(image,0x1a08,base+0x5000+shift);
    Put<std::uint64_t>(image,0x1a20,base+0x3550+shift);
    Put<std::uint64_t>(image,0x1a28,base+0x5a00+shift);
    Put<std::uint64_t>(image,0x1a40,base+0x6800+shift);
    Put<std::uint64_t>(image,0x1a48,base+0x6900+shift);
    Put<std::uint64_t>(image,0x1a50,base+0x6200+shift);
    return image;
}

std::uint32_t Rva(const spotify::HookTargets& targets,
                  spotify::TargetKind kind) {
    return spotify::TargetRva(targets, kind);
}

} // namespace

int main() {
    spotify::HookTargets targets{};
    auto image = MappedImage();
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::Found);
    assert(Rva(targets, spotify::TargetKind::FlacError) == 0x3000);
    assert(Rva(targets, spotify::TargetKind::FlacInit) == 0x3550);
    assert(Rva(targets, spotify::TargetKind::FlacRead) == 0x3c60);
    assert(Rva(targets, spotify::TargetKind::FlacFrame) == 0x4000);
    assert(Rva(targets, spotify::TargetKind::FormatSniff) == 0x9000);
    assert(Rva(targets, spotify::TargetKind::OggPageSeek) == 0x2b000);
    spotify::DecodedOutputTarget decoded{};
    assert(spotify::DiscoverDecodedOutputTarget(image.data(),image.size(),decoded)==spotify::DiscoveryResult::Found);
    assert(decoded.rva==0x4400&&decoded.caller_rva==0x40000&&decoded.decoder_offset==0x70);
    spotify::DecoderOwnershipLayout ownership{};
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==spotify::DiscoveryResult::Found);
    assert(ownership.decoded_output==0x4400&&ownership.ogg_capture_to_decoder==-0x88&&
           ownership.flac_capture_to_decoder==0&&ownership.aac_capture_to_decoder==0&&!ownership.aac_verified);

    image=MappedImage();image[0x5900+0x2e]^=0xff;
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::InconsistentLayout);

    image=MappedImage();image[0x5000+0x17f]=0;
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::InconsistentLayout);
    image=MappedImage();image[0x5400+0x36]=0;
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::InconsistentLayout);
    image=MappedImage();image[0x5000+0x180]=0x90;
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::InconsistentLayout);
    image=MappedImage();std::memcpy(image.data()+0x1b00,image.data()+0x1a00,16);
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::AmbiguousTarget);

    image=MappedImage();image[0x3550+0x13c]^=0xff;
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::InconsistentLayout);
    image=MappedImage();image[0x5e00]^=0xff;
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::InconsistentLayout);
    image=MappedImage();std::memcpy(image.data()+0x1b20,image.data()+0x1a20,16);
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::AmbiguousTarget);
    image=MappedImage();Put<std::uint64_t>(image,0x1a28,reinterpret_cast<std::uintptr_t>(image.data())+0x1a00);
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::InconsistentLayout);

    // AAC is optional: an unproved AAC table must not revoke proven Ogg/FLAC.
    image=MappedImage();Put<std::uint64_t>(image,0x1a50,reinterpret_cast<std::uintptr_t>(image.data())+0x6800);
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==spotify::DiscoveryResult::Found);
    assert(!ownership.aac_verified);
    image=MappedImage();Put<std::uint64_t>(image,0x1a48,reinterpret_cast<std::uintptr_t>(image.data())+0x1a00);
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==spotify::DiscoveryResult::Found);
    assert(!ownership.aac_verified);

    image=MappedImage();image[0x40000+0x2c5]^=0xff;
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size(),ownership)==
           spotify::DiscoveryResult::InconsistentLayout);
    image=MappedImage();
    assert(spotify::DiscoverDecoderOwnershipLayout(image.data(),image.size()-1,ownership)!=
           spotify::DiscoveryResult::Found);

    image = MappedImage(0x1000);
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::Found);
    assert(Rva(targets, spotify::TargetKind::FlacError) == 0x4000);
    assert(Rva(targets, spotify::TargetKind::OggPageSeek) == 0x2c000);

    image = MappedImage();
    image[0x9000 + 55] ^= 0xff;
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::MissingTarget);

    image = MappedImage(0, true);
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::AmbiguousTarget);

    image=MappedImage();
    std::memcpy(image.data()+0x4600,decoded_output,sizeof(decoded_output));
    std::memcpy(image.data()+0x4600+0x4e,decoded_output_decode,sizeof(decoded_output_decode));
    std::memcpy(image.data()+0x4600+0x62,decoded_output_finish,sizeof(decoded_output_finish));
    Put<std::uint32_t>(image,0x1000+5*12,0x4600);
    Put<std::uint32_t>(image,0x1000+5*12+4,0x468c);
    Put<std::uint32_t>(image,0x1000+5*12+8,0x1800);
    Put<std::uint32_t>(image,0x1000+6*12,0x9000);
    Put<std::uint32_t>(image,0x1000+6*12+4,0x9af6);
    Put<std::uint32_t>(image,0x1000+6*12+8,0x1800);
    Put<std::uint32_t>(image,0x1000+7*12,0x2b000);
    Put<std::uint32_t>(image,0x1000+7*12+4,0x2b1b4);
    Put<std::uint32_t>(image,0x1000+7*12+8,0x1800);
    Put<std::uint32_t>(image,0x98+112+3*8+4,8*12);
    assert(spotify::DiscoverDecodedOutputTarget(image.data(),image.size(),decoded)==spotify::DiscoveryResult::AmbiguousTarget);

    image=MappedImage();image[0x4400]^=0xff;
    assert(spotify::DiscoverDecodedOutputTarget(image.data(),image.size(),decoded)==spotify::DiscoveryResult::MissingTarget);

    image=MappedImage();Put<std::uint32_t>(image,0x1000+4*12,0x43f0);
    assert(spotify::DiscoverDecodedOutputTarget(image.data(),image.size(),decoded)==spotify::DiscoveryResult::InconsistentLayout);

    image=MappedImage();image[0x40000+0x48c]^=0xff;
    assert(spotify::DiscoverDecodedOutputTarget(image.data(),image.size(),decoded)!=spotify::DiscoveryResult::Found);
    image=MappedImage();image[0x40000+0x2c5]^=0xff;
    assert(spotify::DiscoverDecodedOutputTarget(image.data(),image.size(),decoded)==spotify::DiscoveryResult::InconsistentLayout);
    image=MappedImage();image[0x40000+0x2c9]=0x90;
    assert(spotify::DiscoverDecodedOutputTarget(image.data(),image.size(),decoded)==spotify::DiscoveryResult::InconsistentLayout);

    image = MappedImage(0, false, true);
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::InconsistentLayout);

    image = MappedImage();
    Put<std::uint32_t>(image, 0x98 + 240 + 40 + 36, 0x40000040);
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) !=
           spotify::DiscoveryResult::Found);
}
