#pragma once
#include "existing_quality.h"
#include <cstddef>
namespace history {
enum class FilePublication { Saved, Upgraded, Skipped, Failed, Replan, ResourceWait };
// Caller holds a flushed, verified staging object denying writes/deletion.
// Rename that exact object without replacing an occupied destination.
bool CommitArchiveStage(void* staged_file,const std::wstring& destination);
enum class FilenameConversion { Unchanged, Renamed, Failed };
using FilenameRenamePermit=bool(*)(void* context);
using PublicationPermit=bool(*)(void* context);
using PublicationMemoryPermit=RecordingMemoryPermit;
#ifdef FLOGGFY_PUBLICATION_TEST
void PublicationTestPhase(const wchar_t* phase,const std::wstring& path);
#endif
// Publication-worker-only: remove an exact old metadata-derived prefix from a
// complete owned recording. Atomic same-folder rename; never replaces a target.
// On Unchanged/Failed, renamed_path is untouched and the source is retained.
FilenameConversion ConvertRecordingFilename(const std::wstring& source,const std::wstring& title_path,
    const Catalog& catalog,std::wstring& renamed_path,FilenameRenamePermit is_current=nullptr,void* context=nullptr);
// Input is complete tagged audio. The catalog identifies this recording, not
// merely its sanitized filename. Every publication stages before rename.
// A supplied permit runs after staging, immediately before promotion. Replan
// leaves the exact old recording intact; a granted permit completes that attempt.
FilePublication PublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size,const Catalog& catalog,
                             size_t* published_bytes=nullptr,PublicationPermit permit=nullptr,void* context=nullptr,
                             PublicationMemoryPermit memory_permit=nullptr,void* memory_context=nullptr);
struct FormatUpgradeReceipt {
    std::wstring source,destination;
    Catalog catalog;
    Quality quality,source_quality;
    uint64_t source_file=0;
    unsigned source_volume=0;
    uint64_t source_hash=0;
    size_t source_bytes=0;
    bool committed=false,metadata_preserved=false,acknowledged=false;
    size_t Capacity() const {
        size_t size=(source.capacity()+destination.capacity())*sizeof(wchar_t);
        for(const auto* value:{&catalog.artist,&catalog.album,&catalog.title,&catalog.author,&catalog.series,&catalog.book,&catalog.show,&catalog.folder_artist,&catalog.legacy_artist})size+=value->capacity()*sizeof(wchar_t);
        return size+catalog.spotify_uri.capacity()+catalog.spotify_album_uri.capacity();
    }
};
FilePublication CompleteFormatUpgrade(FormatUpgradeReceipt& receipt,size_t* published_bytes=nullptr,
    PublicationMemoryPermit memory_permit=nullptr,void* memory_context=nullptr);
// Replace a verified complete owned Ogg with a better FLAC at its format path.
// The Ogg stays locked and present until the FLAC is complete, identity matched,
// quality checked and supplied with the Ogg's retained metadata. A pre-existing
// matching FLAC is repaired in place before the exact Ogg object is retired.
FilePublication PublishFormatUpgrade(const std::wstring& source,const std::wstring& destination,
    const Quality& quality,const void* bytes,size_t size,const Catalog& catalog,size_t* published_bytes=nullptr,PublicationPermit permit=nullptr,void* context=nullptr,FormatUpgradeReceipt* receipt=nullptr,
    PublicationMemoryPermit memory_permit=nullptr,void* memory_context=nullptr);
// Caller retains a GENERIC_READ|DELETE handle denying writes/deletes from the
// original metadata read through this exact-object replacement.
FilePublication PublishMetadataBytes(const std::wstring& path,const void* bytes,size_t size,const Catalog& catalog,void* locked_file);
}
