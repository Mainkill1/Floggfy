#pragma once
#include "media_session.h"
#include "recording_counters.h"
#include <deque>
#include <algorithm>
namespace history {
struct RecordingMetadata {
    Media media;
    double start=0,finish=0; // First/latest metadata observation; never listening coverage.
    uint64_t generation=0;
};
// Identity history is independent of listening coverage and UI end ticks.
class RecordingHistory {
public:
    struct Record {
        RecordingMetadata metadata;
        std::uint64_t start_number=0;
        bool started=false;
        bool assigned=false;
        RecordCounter metadata_counter{};
    };
private:
    std::deque<Record> records_;
    std::uint64_t generation_=0,starts_=0,current_=0;
    double last_start_time_=-1,last_observation_time_=-1;
public:
    std::uint64_t Starts() const noexcept{return starts_;}
    const std::deque<Record>& Records() const noexcept{return records_;}
    Record& Observe(const Media& media,double time) {
        auto current=std::find_if(records_.begin(),records_.end(),[&](const auto& record) {
            return record.metadata.generation==current_;
        });
        // Delayed provider replies cannot replay an old transition or spend a
        // start. Keep the current generation as the caller's association input.
        if(time<last_observation_time_&&current!=records_.end())return *current;
        last_observation_time_=std::max(last_observation_time_,time);
        auto found=records_.end();
        if(!media.playback_instance.empty()) {
            found=std::find_if(records_.begin(),records_.end(),[&](const auto& record) {
                return record.metadata.media.playback_instance==media.playback_instance;
            });
        }
        // A catalog key proves continuity only with the current occurrence.
        // A -> B -> A therefore gets a fresh generation, while a late native
        // identity enriches the same uninterrupted fallback occurrence.
        if(found==records_.end()&&current!=records_.end()) {
            const auto& prior=current->metadata.media;
            const auto key=[](const Media& item) {
                return item.spotify_uri.empty()?item.Key():item.spotify_uri;
            };
            if((media.playback_instance.empty()||prior.playback_instance.empty())&&key(prior)==key(media))
                found=current;
        }
        if(found==records_.end()) {
            records_.push_back({{media,time,time,++generation_},starts_,false});
            found=std::prev(records_.end());
            found->metadata_counter.Begin(RecordKind::Metadata);
        } else {
            auto& target=found->metadata.media;
            if(!media.spotify_uri.empty())target.spotify_uri=media.spotify_uri;
            if(!media.playback_instance.empty())target.playback_instance=media.playback_instance;
            // Empty/delayed properties never erase labels already observed.
            if(!media.title.empty())target.title=media.title;
            if(!media.artist.empty())target.artist=media.artist;
            if(!media.album.empty())target.album=media.album;
            if(!media.album_artist.empty())target.album_artist=media.album_artist;
            if(!media.genre.empty())target.genre=media.genre;
            if(media.track)target.track=media.track;
            if(media.duration>0)target.duration=media.duration;
            found->metadata.finish=std::max(found->metadata.finish,time);
        }
        if(!found->started&&media.playing&&time>=last_start_time_) {
            found->started=true;found->start_number=++starts_;last_start_time_=time;
        }
        current_=found->metadata.generation;return *found;
    }
    const Record* Find(std::uint64_t generation) const noexcept {
        for(const auto& record:records_)if(record.metadata.generation==generation)return &record;
        return nullptr;
    }
    const Record* Current() const noexcept{return Find(current_);}
    void MarkAssigned(std::uint64_t generation) noexcept {
        for(auto& record:records_)if(record.metadata.generation==generation){record.assigned=true;return;}
    }
    RecordCounter TakePending(std::uint64_t generation) noexcept {
        for(auto& record:records_)if(record.metadata.generation==generation)
            return std::move(record.metadata_counter);
        return {};
    }
    void ReturnPending(std::uint64_t generation,RecordCounter counter) noexcept {
        if(!counter.Active())return;
        for(auto& record:records_)if(record.metadata.generation==generation) {
            record.metadata_counter=std::move(counter);return;
        }
    }
    void Prune(std::uint64_t max_queue=5) {
        records_.erase(std::remove_if(records_.begin(),records_.end(),[&](const auto& record) {
            return starts_>=record.start_number&&starts_-record.start_number>=max_queue;
        }),records_.end());
    }
};
}
