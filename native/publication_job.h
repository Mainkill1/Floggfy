#pragma once
#include "history_settings.h"
#include "library_layout.h"
#include "ogg_tags.h"
#include <cstdint>
#include <string>
#include <vector>
namespace history {
enum class Publication { Saved,AlreadyPresent,Replan,RetryIO,Blocked,Cancelled };
struct PublicationPreferences {
    bool metadata=false,lyrics=false;
    uint64_t revision=0;
    explicit PublicationPreferences(const Settings& settings):metadata(settings.metadata),lyrics(settings.lyrics),revision(settings.artifact_revision) {}
    bool Matches(const Settings& settings) const {return revision==settings.artifact_revision&&metadata==settings.metadata&&lyrics==settings.lyrics;}
};
struct ArtifactKey {
    std::string identity;
    std::uint64_t revision=0;
    bool operator==(const ArtifactKey& other) const {return revision==other.revision&&identity==other.identity;}
};
struct PreparedArtifact {
    ArtifactKey key;
    Catalog catalog;
    Tags tags;
    Quality quality;
    std::vector<uint8_t> bytes;
    size_t Capacity() const {
        size_t size=key.identity.capacity()+bytes.capacity()+tags.cover.capacity()+tags.mime.capacity();
        for(const auto& field:tags.fields)size+=sizeof(field)+field.first.capacity()+field.second.capacity();
        for(const auto* value:{&catalog.artist,&catalog.album,&catalog.title,&catalog.author,&catalog.series,&catalog.book,
                              &catalog.show,&catalog.folder_artist,&catalog.legacy_artist})size+=value->capacity()*sizeof(wchar_t);
        return size+catalog.spotify_uri.capacity()+catalog.spotify_album_uri.capacity();
    }
};
struct UnmatchedAttempt {
    std::wstring root,base;
    uint64_t destination_revision=0;
    bool sidecar_saved=false,audio_saved=false,committed=false;
};
}
