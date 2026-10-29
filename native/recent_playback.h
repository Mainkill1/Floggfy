#pragma once
#include "media_session.h"
#include "ogg_history_core.h"
#include <deque>
namespace history {
struct PlaybackListen {Media media;double start=0,finish=0;uint64_t generation=0;bool ambiguous=false;};
class RecentPlaybackHistory {
public:
    struct Record {PlaybackListen listen;bool complete=false;};
private:
    std::deque<Record> records_;
    static void MergeLabels(Media& target,const Media& incoming) {
        if(!incoming.title.empty())target.title=incoming.title;
        if(!incoming.artist.empty())target.artist=incoming.artist;
        if(!incoming.album.empty())target.album=incoming.album;
        if(!incoming.album_artist.empty())target.album_artist=incoming.album_artist;
        if(incoming.track)target.track=incoming.track;
    }
    Record& Put(const PlaybackListen& listen,bool complete) {
        if(!listen.media.spotify_uri.empty())for(auto& record:records_)
            if(record.listen.media.spotify_uri==listen.media.spotify_uri)MergeLabels(record.listen.media,listen.media);
        for(auto& record:records_)if(record.listen.generation==listen.generation&&record.listen.media.ListenKey()==listen.media.ListenKey()) {
            MergeLabels(record.listen.media,listen.media);
            if(complete){record.listen.start=listen.start;record.listen.finish=listen.finish;record.complete=true;}
            return record;
        }
        records_.push_back({listen,complete});
        while(records_.size()>5)records_.pop_front();
        return records_.back();
    }
public:
    void Clear(){records_.clear();}
    size_t Size() const noexcept{return records_.size();}
    const Record* Find(uint64_t generation,const std::string& key) const {
        for(const auto& record:records_)if(record.listen.generation==generation&&record.listen.media.ListenKey()==key)return &record;
        return nullptr;
    }
    void Observe(const Media& media,const Listen& listen) {
        if(listen.generation&&listen.identity==media.ListenKey())Put({media,listen.start_time,0,listen.generation},false);
    }
    void Complete(const PlaybackListen& listen){Put(listen,true);}
    void Refresh(PlaybackListen& listen) const {
        if(const auto* record=Find(listen.generation,listen.media.ListenKey()))MergeLabels(listen.media,record->listen.media);
    }
};
}
