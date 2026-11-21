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

std::size_t ReadPlayerState(const void* owner,const PlayerPath& path,ReadMemory read,
                     std::array<unsigned char,kPlayerStateSearchBytes>& bytes) noexcept {
    const auto pointer=[&](std::uintptr_t base,std::size_t offset,std::uintptr_t& out) {
        return base&&offset<=std::numeric_limits<std::uintptr_t>::max()-base&&
            read(reinterpret_cast<const void*>(base+offset),&out,sizeof(out))&&out;
    };
    std::uintptr_t source=0,state=0;
    if(!owner||!read||!pointer(reinterpret_cast<std::uintptr_t>(owner),path.source_offset,source)||
       !pointer(source,path.state_offset,state)||
       state>std::numeric_limits<std::uintptr_t>::max()-bytes.size())return 0;
    bytes.fill(0);
    std::size_t copied=0;
    while(copied<bytes.size()) {
        auto chunk=std::min(std::size_t{256},bytes.size()-copied);
        while(chunk>=kInstanceBytes&&!read(reinterpret_cast<const void*>(state+copied),bytes.data()+copied,chunk))chunk/=2;
        if(chunk<kInstanceBytes)break;
        copied+=chunk;
    }
    return copied;
}

static PlayerLocation LocateInState(const std::array<unsigned char,kPlayerStateSearchBytes>& bytes,
                                    const Instance& expected,std::size_t readable_bytes) noexcept {
    if(std::all_of(expected.bytes.begin(),expected.bytes.end(),[](unsigned char value){return value==0;}))return {};
    readable_bytes=std::min(readable_bytes,bytes.size());
    PlayerLocation result{LocationStatus::NotFound,0,0,readable_bytes};
    if(readable_bytes<kInstanceBytes){result.status=LocationStatus::Unreadable;return result;}
    for(std::size_t offset=0;offset<=readable_bytes-kInstanceBytes;++offset) {
        if(std::memcmp(bytes.data()+offset,expected.bytes.data(),expected.bytes.size()))continue;
        result.offset=offset;++result.matches;
    }
    result.status=!result.matches?LocationStatus::NotFound:
        result.matches!=1?LocationStatus::Ambiguous:
        readable_bytes<kPlayerFieldBytes||result.offset>readable_bytes-kPlayerFieldBytes?LocationStatus::Unreadable:LocationStatus::Verified;
    return result;
}

PlayerLocation LocatePlaybackInstance(const void* owner,const PlayerPath& path,
                                     ReadMemory read,const Instance& expected) noexcept {
    std::array<unsigned char,kPlayerStateSearchBytes> bytes{};
    const auto readable_bytes=ReadPlayerState(owner,path,read,bytes);
    return LocateInState(bytes,expected,readable_bytes);
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

bool DiagnosticBridge::NeedsPlayerState(std::uintptr_t owner,std::uintptr_t decoder,unsigned epoch,double now) const noexcept {
    if(!owner||!decoder||location_.status==LocationStatus::Verified)return false;
    double latest=-1;
    for(std::size_t i=0;i<player_state_count_;++i) {
        const auto& prior=player_states_[i];
        if(prior.owner==owner&&prior.decoder==decoder&&prior.epoch==epoch&&
           prior.metadata_generation==metadata_generation_)latest=std::max(latest,prior.observed_at);
    }
    return latest<0||(has_expected_&&now>=latest+.25);
}

void DiagnosticBridge::ObservePlayerState(std::uintptr_t owner,std::uintptr_t decoder,unsigned epoch,
                                         std::uint64_t sequence,
                                         const std::array<unsigned char,kPlayerStateSearchBytes>& bytes,
                                         double observed_at,std::size_t readable_bytes) noexcept {
    if(!sequence||!NeedsPlayerState(owner,decoder,epoch,observed_at))return;
    PlayerStateObservation* selected=nullptr;bool retry=false;
    for(std::size_t i=0;i<player_state_count_;++i) {
        auto& prior=player_states_[i];
        if(prior.owner==owner&&prior.decoder==decoder&&prior.epoch==epoch&&prior.metadata_generation==metadata_generation_) {
            retry=true;if(prior.retry)selected=&prior;
        }
    }
    if(!selected) {
        // These are unverified calibration snapshots, not established ID
        // observations. Expiring them must not disable the independent matcher
        // forever on a layout that has never calibrated successfully.
        if(player_state_count_==player_states_.size())++calibration_retirements_;
        else ++player_state_count_;
        selected=&player_states_[player_state_next_++%player_states_.size()];
    }
    auto& snapshot=*selected;snapshot.retry=retry;
    snapshot.owner=owner;snapshot.decoder=decoder;snapshot.epoch=epoch;
    snapshot.sequence=sequence;snapshot.metadata_generation=metadata_generation_;
    readable_bytes=std::min(readable_bytes,bytes.size());
    if(snapshot.readable_bytes!=readable_bytes||
       std::memcmp(snapshot.bytes.data(),bytes.data(),readable_bytes))snapshot.location_cached=false;
    snapshot.observed_at=observed_at;snapshot.bytes=bytes;
    snapshot.readable_bytes=readable_bytes;
    LocateExpected();
}

void DiagnosticBridge::LocateExpected() noexcept {
    if(!has_expected_||location_.status==LocationStatus::Verified||!player_state_count_)return;
    PlayerLocation found{LocationStatus::NotFound,0,0};
    for(std::size_t i=0;i<player_state_count_;++i) {
        auto& snapshot=player_states_[i];
        if(!snapshot.location_cached) {
            snapshot.location=LocateInState(snapshot.bytes,expected_,snapshot.readable_bytes);
            snapshot.location_cached=true;
        }
        const auto candidate=snapshot.location;
        if(candidate.status==LocationStatus::Ambiguous) {location_=candidate;return;}
        if(candidate.status!=LocationStatus::Verified) {
            if(found.status!=LocationStatus::Verified&&candidate.status==LocationStatus::Unreadable)found=candidate;
            found.readable_bytes=std::max(found.readable_bytes,candidate.readable_bytes);
            continue;
        }
        if(found.status==LocationStatus::Verified&&found.offset!=candidate.offset) {
            location_={LocationStatus::Ambiguous,0,2,std::max(found.readable_bytes,candidate.readable_bytes)};return;
        }
        found=candidate;
    }
    location_=found;
    if(found.status!=LocationStatus::Verified)return;
    // The offset describes this process's layout, not the UI's current track.
    // Convert all retained owners, including preloads, at their original time.
    std::array<std::size_t,16> order{};
    for(std::size_t i=0;i<player_state_count_;++i)order[i]=i;
    std::sort(order.begin(),order.begin()+player_state_count_,[&](auto a,auto b){return player_states_[a].sequence<player_states_[b].sequence;});
    for(std::size_t i=0;i<player_state_count_;++i) {
        const auto& snapshot=player_states_[order[i]];
        if(found.offset>snapshot.readable_bytes||snapshot.readable_bytes-found.offset<kPlayerFieldBytes)continue;
        if(std::all_of(snapshot.bytes.begin()+found.offset,snapshot.bytes.begin()+found.offset+kInstanceBytes,
            [](unsigned char value){return value==0;}))continue;
        Observe(snapshot.owner,snapshot.decoder,snapshot.epoch,snapshot.sequence,
            snapshot.bytes.data()+found.offset,kPlayerFieldBytes,SnapshotRole::PlayerPlaybackId,snapshot.observed_at);
    }
    player_states_={};player_state_count_=player_state_next_=0;
}

bool DiagnosticBridge::HasPlayerIdentity(std::uintptr_t decoder,unsigned epoch,double began,double closed_at) const noexcept {
    if(!decoder)return false;
    for(std::size_t i=0;i<observation_count_;++i) {
        const auto& prior=observations_[(observation_head_+i)%observations_.size()];
        Match lifetime{};lifetime.observed_at=prior.observed_at;
        if(prior.decoder==decoder&&prior.epoch==epoch&&prior.role==SnapshotRole::PlayerPlaybackId&&
           prior.size>=kInstanceBytes&&lifetime.ObservedDuring(began,closed_at))return true;
    }
    return false;
}

bool DiagnosticBridge::SetExpected(std::string_view playback_instance,
                                   std::uint64_t metadata_generation) noexcept {
    metadata_generation_=0;has_expected_=false;
    Instance expected{};
    if(!metadata_generation||!ParseInstance(playback_instance,expected))return false;
    if(expected.bytes!=expected_.bytes) {
        for(auto& snapshot:player_states_)snapshot.location_cached=false;
    }
    expected_=expected;
    metadata_generation_=metadata_generation;has_expected_=true;
    if(location_.status==LocationStatus::WaitingMetadata&&!player_state_count_)
        location_.status=LocationStatus::WaitingState;
    LocateExpected();return true;
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
    return true;
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

std::optional<ObservationInfo> DiagnosticBridge::TakeObservation() noexcept {
    auto observation=pending_observation_;pending_observation_.reset();return observation;
}

} // namespace playback_identity
