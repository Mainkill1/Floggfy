#include "playback_identity_bridge.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace playback_identity {
namespace {

int HexValue(char value) noexcept {
    if(value>='0'&&value<='9')return value-'0';
    if(value>='a'&&value<='f')return value-'a'+10;
    if(value>='A'&&value<='F')return value-'A'+10;
    return -1;
}

char LowerHex(unsigned value) noexcept {
    return value<10?char('0'+value):char('a'+value-10);
}

bool EqualHex(const unsigned char* bytes,const Instance& instance) noexcept {
    for(std::size_t index=0;index<instance.hex.size();++index) {
        const auto value=char(bytes[index]);
        if(value!=instance.hex[index]&&
           !(value>='A'&&value<='F'&&char(value-'A'+'a')==instance.hex[index]))return false;
    }
    return true;
}

} // namespace

bool ParseInstance(std::string_view value,Instance& out) noexcept {
    out={};
    if(value.size()!=kInstanceHexCharacters)return false;
    bool any=false;
    for(std::size_t index=0;index<kInstanceBytes;++index) {
        const int high=HexValue(value[index*2]);
        const int low=HexValue(value[index*2+1]);
        if(high<0||low<0)return false;
        out.bytes[index]=static_cast<unsigned char>((high<<4)|low);
        any=any||out.bytes[index]!=0;
        out.hex[index*2]=LowerHex(unsigned(high));
        out.hex[index*2+1]=LowerHex(unsigned(low));
    }
    if(!any)out={};
    return any;
}

bool ReadPlaybackInstance(const void* owner,const PlayerPath& path,
                          ReadMemory read,Instance& out,
                          std::array<unsigned char,kPlayerFieldBytes>* field,
                          std::size_t payload_offset) noexcept {
    out={};
    if(field)*field={};
    if(!owner||!read||payload_offset>kPlayerFieldBytes-kInstanceBytes)return false;
    std::array<unsigned char,kPlayerFieldBytes> bytes{};
    const auto add=[](std::uintptr_t base,std::size_t offset,std::uintptr_t& result) noexcept {
        if(!base||offset>std::numeric_limits<std::uintptr_t>::max()-base)return false;
        result=base+offset;
        return true;
    };
    std::uintptr_t address=0,source=0,state=0;
    if(!add(reinterpret_cast<std::uintptr_t>(owner),path.source_offset,address)||
       !read(reinterpret_cast<const void*>(address),&source,sizeof(source))||
       !add(source,path.state_offset,address)||
       !read(reinterpret_cast<const void*>(address),&state,sizeof(state))||
       !add(state,path.playback_id_offset,address)||
       !read(reinterpret_cast<const void*>(address),bytes.data(),field?bytes.size():payload_offset+kInstanceBytes)) {
        out={};return false;
    }
    if(field)*field=bytes;
    std::copy_n(bytes.begin()+payload_offset,kInstanceBytes,out.bytes.begin());
    bool any=false;
    for(std::size_t index=0;index<out.bytes.size();++index) {
        any=any||out.bytes[index]!=0;
        out.hex[index*2]=LowerHex(out.bytes[index]>>4);
        out.hex[index*2+1]=LowerHex(out.bytes[index]&0xf);
    }
    if(!any)out={};
    return any;
}

bool DiagnosticBridge::MatchObservation(const Observation& observation,const Instance& expected,
                                        std::uint64_t metadata_generation,Match& match) const noexcept {
    std::size_t matches=0;
    Representation representation=Representation::None;
    std::size_t offset=0;
    if(observation.size>=expected.bytes.size()) {
        for(std::size_t index=0;index<=observation.size-expected.bytes.size();++index) {
            if(std::memcmp(observation.bytes.data()+index,expected.bytes.data(),expected.bytes.size()))continue;
            ++matches;representation=Representation::RawBytes;offset=index;
        }
    }
    if(observation.size>=expected.hex.size()) {
        for(std::size_t index=0;index<=observation.size-expected.hex.size();++index) {
            if(!EqualHex(observation.bytes.data()+index,expected))continue;
            ++matches;representation=Representation::AsciiHex;offset=index;
        }
    }
    if(matches!=1)return false;
    match={observation.owner,observation.decoder,observation.epoch,observation.sequence,
           metadata_generation,observation.owner_generation,representation,
           observation.role,offset,observation.observed_at};
    return true;
}

bool DiagnosticBridge::ResolveExpected() noexcept {
    if(!has_expected_)return false;
    bool matched=false;
    for(std::size_t index=0;index<observation_count_;++index) {
        auto& observation=observations_[(observation_head_+index)%observations_.size()];
        if(observation.reported_metadata_generation==metadata_generation_)continue;
        Match candidate{};
        if(!MatchObservation(observation,expected_,metadata_generation_,candidate))continue;
        observation.reported_metadata_generation=metadata_generation_;
        if(pending_match_count_<pending_matches_.size())
            pending_matches_[pending_match_count_++]=candidate;
        matched=true;
    }
    return matched;
}

bool DiagnosticBridge::SetExpected(std::string_view playback_instance,
                                   std::uint64_t metadata_generation) noexcept {
    pending_match_count_=0;pending_match_next_=0;metadata_generation_=0;has_expected_=false;
    if(!metadata_generation||!ParseInstance(playback_instance,expected_))return false;
    metadata_generation_=metadata_generation;has_expected_=true;
    ResolveExpected();return true;
}

bool DiagnosticBridge::NeedsObservation(std::uintptr_t owner,std::uintptr_t decoder,
                                        unsigned epoch,SnapshotRole role,double minimum_observed_at) const noexcept {
    if(!owner||!decoder)return false;
    for(std::size_t index=0;index<observation_count_;++index) {
        const auto& prior=observations_[(observation_head_+index)%observations_.size()];
        if(prior.owner==owner&&prior.decoder==decoder&&prior.epoch==epoch&&
           prior.role==role&&prior.observed_at>=minimum_observed_at)return false;
    }
    return true;
}

bool DiagnosticBridge::NeedsObservation(std::uintptr_t owner,std::uintptr_t decoder,
                                        unsigned epoch,SnapshotRole role,
                                        const void* snapshot,std::size_t snapshot_size,
                                        double minimum_observed_at) const noexcept {
    if(!owner||!decoder||!snapshot||!snapshot_size)return false;
    const auto retained_size=std::min(snapshot_size,kMaximumSnapshotBytes);
    for(std::size_t index=0;index<observation_count_;++index) {
        const auto& prior=observations_[(observation_head_+index)%observations_.size()];
        if(prior.owner==owner&&prior.decoder==decoder&&prior.epoch==epoch&&
           prior.role==role&&prior.observed_at>=minimum_observed_at&&prior.size==retained_size&&
           !std::memcmp(prior.bytes.data(),snapshot,retained_size))return false;
    }
    return true;
}

bool DiagnosticBridge::Observe(std::uintptr_t owner,std::uintptr_t decoder,unsigned epoch,
                               std::uint64_t sequence,const void* snapshot,
                               std::size_t snapshot_size,SnapshotRole role,
                               double observed_at,double minimum_observed_at) noexcept {
    if(!owner||!decoder||!sequence||!snapshot||!snapshot_size)return false;
    const auto retained_size=std::min(snapshot_size,kMaximumSnapshotBytes);
    for(std::size_t index=0;index<observation_count_;++index) {
        const auto& prior=observations_[(observation_head_+index)%observations_.size()];
        if(prior.owner==owner&&prior.decoder==decoder&&prior.epoch==epoch&&
           prior.role==role&&prior.observed_at>=minimum_observed_at&&prior.size==retained_size&&
           !std::memcmp(prior.bytes.data(),snapshot,retained_size))return false;
    }
    std::uint64_t owner_generation=0;
    for(std::size_t index=observation_count_;index>0;--index) {
        const auto& prior=observations_[(observation_head_+index-1)%observations_.size()];
        if(prior.owner==owner) {
            owner_generation=prior.owner_generation;break;
        }
    }
    const auto& latest=observations_[observation_count_?
        (observation_head_+observation_count_-1)%observations_.size():observation_head_];
    if(!owner_generation||
       (observation_count_&&
        (latest.owner!=owner||latest.decoder!=decoder||latest.epoch!=epoch)))
        owner_generation=next_owner_generation_++;
    std::size_t slot=0;
    if(observation_count_==observations_.size()) {
        observation_lost_=true;slot=observation_head_;
        observation_head_=(observation_head_+1)%observations_.size();
    } else {
        slot=(observation_head_+observation_count_)%observations_.size();
        ++observation_count_;
    }
    auto& observation=observations_[slot];
    observation.owner=owner;observation.decoder=decoder;observation.epoch=epoch;
    observation.sequence=sequence;observation.owner_generation=owner_generation;
    observation.role=role;observation.observed_at=observed_at;
    observation.size=retained_size;
    std::memcpy(observation.bytes.data(),snapshot,observation.size);
    pending_observation_=ObservationInfo{owner,decoder,epoch,sequence,owner_generation,role,observed_at};
    if(!has_expected_)return false;
    return ResolveExpected();
}

std::size_t DiagnosticBridge::QueryMatches(std::string_view playback_instance,
                                           std::uint64_t metadata_generation,unsigned epoch,
                                           Match* matches,std::size_t capacity) const noexcept {
    if(!metadata_generation||!matches||!capacity)return 0;
    Instance expected{};
    if(!ParseInstance(playback_instance,expected))return 0;
    std::size_t count=0;
    for(std::size_t index=0;index<observation_count_&&count<capacity;++index) {
        const auto& observation=observations_[(observation_head_+index)%observations_.size()];
        if(observation.epoch!=epoch)continue;
        Match match{};
        if(MatchObservation(observation,expected,metadata_generation,match))
            matches[count++]=match;
    }
    return count;
}

void DiagnosticBridge::Reset() noexcept {
    *this={};
}

std::optional<Match> DiagnosticBridge::TakeMatch() noexcept {
    if(pending_match_next_>=pending_match_count_) {
        pending_match_count_=0;pending_match_next_=0;return std::nullopt;
    }
    const auto match=pending_matches_[pending_match_next_++];
    if(pending_match_next_==pending_match_count_) {
        pending_match_count_=0;pending_match_next_=0;
    }
    return match;
}

std::optional<ObservationInfo> DiagnosticBridge::TakeObservation() noexcept {
    auto observation=pending_observation_;pending_observation_.reset();return observation;
}

} // namespace playback_identity
