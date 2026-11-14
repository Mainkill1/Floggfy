#include "recording_fixture.h"
#include "../native/recording_file.h"
#include "../native/flac_history_core.h"
#include "../native/vorbis_headers.h"
#include "../native/mp3_stream.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
using namespace history;
static void check(bool value,const char* label) { if(!value) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); } }
using recording_fixture::fixture;
static void flac_buffer_coverage() {
    auto bytes=fixture();
    const size_t audio=42,frame_size=bytes.size()-audio;
    for(unsigned number=1;number<3;++number) {
        auto frame=std::vector<uint8_t>(bytes.begin()+audio,bytes.begin()+audio+frame_size);
        frame[4]=uint8_t(number);frame[6]=uint8_t(recording_fixture::crc(
            std::vector<uint8_t>(frame.begin(),frame.begin()+6),8,7));
        frame.resize(frame.size()-2);
        const auto crc=recording_fixture::crc(frame,16,0x8005);
        frame.push_back(uint8_t(crc>>8));frame.push_back(uint8_t(crc));
        bytes.insert(bytes.end(),frame.begin(),frame.end());
    }
    bytes[25]=48;
    FlacInfo info;size_t parsed_audio=0;
    check(ParseFlacMetadata(bytes,info,parsed_audio)==FlacParse::Valid&&parsed_audio==audio,
          "three-frame source metadata parsed");
    RecordingFile recording;
    check(InspectRecording(bytes.data(),bytes.size(),recording)&&recording.complete,
          "three-frame source independently passes final inspection");
    for(size_t split=audio;split<=bytes.size();++split) {
        CompressedBuffer buffer;FlacBufferCoverage coverage;
        check(buffer.Append(bytes.data(),split,kMaximumRecordingBytes),"append source prefix");
        coverage.Scan(buffer,info,audio);
        check(coverage.complete==(split==bytes.size()),"only exact complete source is ready");
        for(size_t at=split;at<bytes.size();++at) {
            check(buffer.Append(bytes.data()+at,1,kMaximumRecordingBytes),"append single source byte");
            coverage.Scan(buffer,info,audio);
            check(coverage.complete==(at+1==bytes.size()),"arbitrary chunks preserve incomplete final frame");
        }
        check(coverage.complete&&coverage.samples==48&&coverage.frames==3&&coverage.next_offset==bytes.size(),
              "compressed bytes alone prove every sample and frame");
        const uint8_t trailing=0;
        check(buffer.Append(&trailing,1,kMaximumRecordingBytes),"append trailing byte");
        coverage.Scan(buffer,info,audio);
        check(!coverage.complete&&coverage.next_offset==bytes.size(),
              "trailing bytes revoke completion while preserving complete prefix boundary");
    }
    for(unsigned kind=0;kind<5;++kind) {
        auto broken=bytes;
        if(kind==0)broken.back()^=1;
        if(kind==1)broken[audio+frame_size+6]^=1;
        if(kind==2)broken.resize(broken.size()-1);
        if(kind==3)broken.erase(broken.begin()+audio+frame_size,broken.begin()+audio+2*frame_size);
        if(kind==4)broken.insert(broken.end(),bytes.begin(),bytes.end());
        CompressedBuffer buffer;FlacBufferCoverage coverage;
        check(buffer.Append(broken.data(),broken.size(),kMaximumRecordingBytes),"append damaged source");
        coverage.Scan(buffer,info,audio);
        check(!coverage.complete,"checksum errors, truncation, missing frames and appended sources never complete");
    }
    // Exercise a frame header and payload spanning the fixed storage blocks.
    const size_t padded_audio=65532;
    std::vector<uint8_t> padded(padded_audio,0);
    padded.insert(padded.end(),bytes.begin()+audio,bytes.end());
    CompressedBuffer buffer;FlacBufferCoverage coverage;
    check(buffer.Append(padded.data(),padded.size(),kMaximumRecordingBytes),"append block-spanning frames");
    coverage.Scan(buffer,info,padded_audio);
    check(coverage.complete&&coverage.next_offset==padded.size(),"frame validation crosses compressed storage blocks");
    auto variable=bytes;
    for(unsigned number=0;number<3;++number) {
        const auto start=audio+number*frame_size;
        variable[start+1]=0xf9;variable[start+4]=uint8_t(number*16);
        variable[start+6]=uint8_t(recording_fixture::crc(
            std::vector<uint8_t>(variable.begin()+start,variable.begin()+start+6),8,7));
        const auto crc=recording_fixture::crc(
            std::vector<uint8_t>(variable.begin()+start,variable.begin()+start+frame_size-2),16,0x8005);
        variable[start+frame_size-2]=uint8_t(crc>>8);variable[start+frame_size-1]=uint8_t(crc);
    }
    CompressedBuffer variable_buffer;
    check(variable_buffer.Append(variable.data(),variable.size(),kMaximumRecordingBytes),"append variable-block source");
    coverage={};coverage.Scan(variable_buffer,info,audio);
    check(coverage.complete&&coverage.samples==48,"variable-block sample numbers prove contiguous coverage");
    auto oversized_info=info;oversized_info.total_samples=47;
    coverage={};coverage.Scan(variable_buffer,oversized_info,audio);
    check(!coverage.complete&&coverage.samples==32,"frame exceeding declared sample count is rejected");
    auto short_fixed_info=info;short_fixed_info.max_block=32;
    coverage={};coverage.Scan(buffer,short_fixed_info,padded_audio);
    check(!coverage.complete&&coverage.samples==0,"short fixed-block frame before source end cannot hide a gap");
}
static std::vector<uint8_t> recovery_source() {
    auto bytes=fixture();const size_t frame_size=bytes.size()-42;
    for(unsigned number=1;number<4;++number) {
        std::vector<uint8_t> frame(bytes.begin()+42,bytes.begin()+42+frame_size);
        frame[4]=uint8_t(number);frame[6]=uint8_t(recording_fixture::crc(
            std::vector<uint8_t>(frame.begin(),frame.begin()+6),8,7));
        frame.resize(frame.size()-2);const auto crc=recording_fixture::crc(frame,16,0x8005);
        frame.push_back(uint8_t(crc>>8));frame.push_back(uint8_t(crc));
        bytes.insert(bytes.end(),frame.begin(),frame.end());
    }
    bytes[25]=64;return bytes;
}
static void flac_resync_work_is_bounded() {
    auto clean=recovery_source();const size_t audio=42,fs=(clean.size()-audio)/4;
    FlacInfo info;size_t parsed=0;
    check(ParseFlacMetadata(clean,info,parsed)==FlacParse::Valid,"resync fixture metadata");
    std::vector<uint8_t> source(clean.begin(),clean.begin()+audio+2*fs);
    source.insert(source.end(),clean.begin()+audio,clean.begin()+audio+fs);
    auto corrupt=std::vector<uint8_t>(clean.begin()+audio+2*fs,clean.begin()+audio+3*fs);
    corrupt.back()^=1;source.insert(source.end(),corrupt.begin(),corrupt.end());
    for(unsigned i=0;i<4096;++i)
        source.insert(source.end(),clean.begin()+audio+3*fs,clean.end());
    CompressedBuffer buffer;FlacCaptureReader reader;
    for(size_t at=0;at<source.size();) {
        const auto n=std::min(size_t(32),source.size()-at);
        check(buffer.Append(source.data()+at,n,kMaximumRecordingBytes),"append unresolved source");
        reader.Scan(buffer,info,audio);at+=n;
    }
    check(!reader.complete&&reader.samples==32,"invalid source remains unresolved");
    check(reader.resync_bytes_examined<=source.size()*2,
          "resync examines appended bytes once instead of rescanning the retained prefix");
    check(reader.resync_candidate_parses<8192,"completed rejected candidates are not reparsed per callback");
    std::vector<uint8_t> padding(16384,0);
    for(unsigned i=0;i<128;++i) {
        source.insert(source.end(),padding.begin(),padding.end());
        check(buffer.Append(padding.data(),padding.size(),kMaximumRecordingBytes),"append beyond resync window");
        reader.Scan(buffer,info,audio);
    }
    const auto examined=reader.resync_bytes_examined;
    source.insert(source.end(),padding.begin(),padding.end());
    check(buffer.Append(padding.data(),padding.size(),kMaximumRecordingBytes),"append after exhausted resync window");
    reader.Scan(buffer,info,audio);
    check(reader.resync_bytes_examined==examined,"exhausted resync window is never scanned again");
    check(!reader.complete,"exhausted resync window cannot grant completion");
    check(buffer.Flatten()==source,"bounded resync does not discard unproven source bytes");
}
static void flac_frame_larger_than_resync_window() {
    // A valid eight-channel verbatim frame exceeds the recovery search window.
    // Reaching that window while the frame is partial must not make it invalid.
    FlacInfo info{44100,8,32,65535,65535,65535};
    std::vector<uint8_t> frame{0xff,0xf8,0x70,0x7e,0,0xff,0xfe};
    frame.push_back(uint8_t(recording_fixture::crc(frame,8,7)));
    for(unsigned c=0;c<8;++c) {
        frame.push_back(2);frame.insert(frame.end(),65535*4,0);
    }
    // A sync-like prefix split at a callback boundary is ordinary sample data.
    // Remembering that speculative candidate must not require recovery memory.
    frame[16384-42-2]=0xff;frame[16384-42-1]=0xf8;
    const auto crc=recording_fixture::crc(frame,16,0x8005);
    frame.push_back(uint8_t(crc>>8));frame.push_back(uint8_t(crc));
    auto source=fixture();source.resize(42);
    for(size_t at=8;at<12;++at)source[at]=0xff;
    const uint64_t packed=(uint64_t(44100)<<44)|(uint64_t(7)<<41)|(uint64_t(31)<<36)|65535;
    for(unsigned i=0;i<8;++i)source[18+i]=uint8_t(packed>>(56-8*i));
    source.insert(source.end(),frame.begin(),frame.end());
    RecordingFile inspected;
    check(InspectRecording(source.data(),source.size(),inspected)&&inspected.complete,
          "large frame fixture passes independent final inspection");
    CompressedBuffer buffer;FlacCaptureReader reader;
    for(size_t at=0;at<source.size();) {
        const auto n=std::min(size_t(16384),source.size()-at);
        check(buffer.Append(source.data()+at,n,kMaximumRecordingBytes),"append large partial frame");
        reader.Scan(buffer,info,42,0);at+=n;
    }
    check(reader.complete&&reader.samples==65535&&reader.frames==1,
          "valid large frame still completes after partial input exceeds resync window");
    check(buffer.Flatten()==source,"large sequential frame remains byte exact");
}
static void flac_capture_recovery() {
    for(bool variable:{false,true}) {
    auto clean=recovery_source();const size_t audio=42,fs=(clean.size()-audio)/4;
    if(variable)for(unsigned number=0;number<4;++number) {
        const auto at=audio+number*fs;clean[at+1]=0xf9;clean[at+4]=uint8_t(number*16);
        clean[at+6]=uint8_t(recording_fixture::crc(std::vector<uint8_t>(clean.begin()+at,clean.begin()+at+6),8,7));
        const auto crc=recording_fixture::crc(std::vector<uint8_t>(clean.begin()+at,clean.begin()+at+fs-2),16,0x8005);
        clean[at+fs-2]=uint8_t(crc>>8);clean[at+fs-1]=uint8_t(crc);
    }
    FlacInfo info;size_t parsed=0;
    check(ParseFlacMetadata(clean,info,parsed)==FlacParse::Valid,"recovery metadata parsed");
    auto append_frame=[&](std::vector<uint8_t>& out,unsigned n,size_t count=0) {
        out.insert(out.end(),clean.begin()+audio+n*fs,clean.begin()+audio+n*fs+(count?count:fs));
    };
    auto repeated=std::vector<uint8_t>(clean.begin(),clean.begin()+audio+2*fs);
    append_frame(repeated,0);append_frame(repeated,1);append_frame(repeated,2);append_frame(repeated,3);
    auto interrupted=std::vector<uint8_t>(clean.begin(),clean.begin()+audio+2*fs);
    append_frame(interrupted,2,9);append_frame(interrupted,0);append_frame(interrupted,1);
    append_frame(interrupted,2);append_frame(interrupted,3);
    auto interrupted_known=std::vector<uint8_t>(clean.begin(),clean.begin()+audio+2*fs);
    append_frame(interrupted_known,1,9);append_frame(interrupted_known,0);append_frame(interrupted_known,1);
    append_frame(interrupted_known,2);append_frame(interrupted_known,3);
    for(const auto& source:{clean,repeated,interrupted,interrupted_known}) {
        RecordingFile strict;
        check(InspectRecording(source.data(),source.size(),strict)&&strict.complete==(source==clean),
              "final inspection stays strict before capture normalization");
        for(const size_t chunk:{size_t(1),size_t(7),source.size()}) {
            CompressedBuffer buffer;FlacCaptureReader reader;
            for(size_t at=0;at<source.size();) {
                const auto n=std::min(chunk,source.size()-at);
                check(buffer.Append(source.data()+at,n,kMaximumRecordingBytes),"append capture chunk");
                reader.Scan(buffer,info,audio);at+=n;
                if(source==clean)check(reader.MemoryUsage()==0,"sequential partial chunks never allocate recovery index");
            }
            if(!reader.complete)std::fprintf(stderr,"RECOVERY source=%zu chunk=%zu offset=%zu samples=%llu frames=%llu reason=%s\n",source.size(),chunk,reader.next_offset,(unsigned long long)reader.samples,(unsigned long long)reader.frames,reader.reason);
            check(reader.complete&&reader.samples==64&&reader.frames==4,"repeated capture completes exact samples");
            if(source==clean)check(reader.MemoryUsage()==0,"ordinary sequential capture needs no index");
            else check(reader.duplicate_frames==2&&reader.duplicate_bytes==2*fs&&
                       reader.interrupted_bytes==(source.size()>repeated.size()?9:0),"recovery counts exact removed bytes");
            FlacBufferCoverage coverage;coverage.Scan(buffer,info,audio);
            check(coverage.complete,"independent strict coverage validates canonical capture");
            check(buffer.Flatten()==clean,"normalization retains byte-exact original frames");
        }
    }
    for(unsigned kind=0;kind<5;++kind) {
        auto broken=repeated;
        if(kind==0)broken.erase(broken.begin()+audio+4*fs,broken.begin()+audio+5*fs); // missing unique frame
        if(kind==1) { // Valid checksum, different audio for a previously verified frame.
            broken[audio+2*fs+9]^=1;
            std::vector<uint8_t> frame(broken.begin()+audio+2*fs,broken.begin()+audio+3*fs-2);
            const auto crc=recording_fixture::crc(frame,16,0x8005);
            broken[audio+3*fs-2]=uint8_t(crc>>8);broken[audio+3*fs-1]=uint8_t(crc);
        }
        if(kind==2)broken[audio+4*fs+9]^=1; // corrupt unique frame
        if(kind==3) {broken=interrupted;broken[audio+2*fs+8]^=1;} // corrupt fragment prefix
        if(kind==4) {broken=clean;broken.insert(broken.end(),clean.begin(),clean.end());}
        CompressedBuffer buffer;FlacCaptureReader reader;
        check(buffer.Append(broken.data(),broken.size(),kMaximumRecordingBytes),"append nonrecoverable capture");
        reader.Scan(buffer,info,audio);
        check(!reader.complete&&buffer.Flatten()==broken,"gaps, conflicts, corrupt frames and second containers remain unsaved and unchanged");
    }
    CompressedBuffer limited;FlacCaptureReader reader;
    check(limited.Append(repeated.data(),repeated.size(),kMaximumRecordingBytes),"append budget-limited capture");
    reader.Scan(limited,info,audio,0);
    check(!reader.complete&&reader.MemoryUsage()==0,"capture index honors shared memory budget");
    auto trailing=clean;append_frame(trailing,1);
    CompressedBuffer tail;FlacCaptureReader tail_reader;
    check(tail.Append(clean.data(),clean.size(),kMaximumRecordingBytes),"append initially complete capture");
    tail_reader.Scan(tail,info,audio);check(tail_reader.complete,"initial clean capture complete");
    check(tail.Append(trailing.data()+clean.size(),fs,kMaximumRecordingBytes),"append reread after completion");
    tail_reader.Scan(tail,info,audio);
    check(tail_reader.complete&&tail_reader.duplicate_frames==1&&tail.Flatten()==clean,
          "reread after prior completion remains one exact canonical recording");
    }
}
static void vorbis_headers() {
    const auto id=recording_fixture::identification(),setup=recording_fixture::setup();
    check(ValidateVorbisHeaders(id,setup)==VorbisHeadersResult::Valid,"complete Vorbis I headers accepted without audio decoding");
    check(ValidateVorbisHeaders(id,recording_fixture::setup(2,1))==VorbisHeadersResult::Valid,"fully populated two-entry Huffman tree accepted");
    std::vector<uint8_t> owned_comment={3,'v','o','r','b','i','s',0,0,0,0,2,0,0,0};
    for(const std::string field:{"HISTORY_COMPLETE_LISTEN=1","HISTORY_CAPTURE=native compressed Ogg pages"}) {
        for(unsigned i=0;i<4;++i)owned_comment.push_back(uint8_t(field.size()>>(8*i)));
        owned_comment.insert(owned_comment.end(),field.begin(),field.end());
    }
    owned_comment.push_back(1);
    const auto owned_valid=recording_fixture::ogg_headers(id,setup,false,owned_comment);
    RecordingFile owned_recording;
    check(InspectRecording(owned_valid.data(),owned_valid.size(),owned_recording)&&owned_recording.owned&&owned_recording.complete,
          "supported headers preserve ownership for a complete captured recording");
    auto rejected=[&](const std::vector<uint8_t>& identification,const std::vector<uint8_t>& configuration,const char* label) {
        const auto stream=recording_fixture::ogg_headers(identification,configuration,false,owned_comment);RecordingFile recording;
        check(!InspectRecording(stream.data(),stream.size(),recording)&&!recording.owned&&!recording.complete,label);
    };
    rejected(id,{5,'v','o','r','b','i','s'},"signature-only setup with valid Ogg CRC/EOS cannot suppress a save");
    // Three fixed regressions: truncated codebook, mapping, and framing byte.
    for(const size_t length:{size_t(15),size_t(40),setup.size()-1})
        rejected(id,std::vector<uint8_t>(setup.begin(),setup.begin()+length),"truncated setup rejected despite valid container framing");
    auto bad=setup;bad[8]^=1;rejected(id,bad,"invalid codebook sync rejected");
    rejected(id,recording_fixture::setup(1,2),"single-entry codebook must use a one-bit codeword");
    rejected(id,recording_fixture::setup(2,2),"underpopulated Huffman tree rejected");
    rejected(id,recording_fixture::setup(3,1),"overpopulated Huffman tree rejected");
    bad=setup;bad.back()=0;rejected(id,bad,"missing setup framing bit rejected");
    bad=setup;bad.push_back(0);rejected(id,bad,"trailing setup bytes are not silently treated as validated configuration");
    // Bit positions in this deliberately minimal fixture, after the 7-byte signature.
    auto bit=[&](std::vector<uint8_t>& packet,unsigned position) {position+=56;packet[position/8]|=uint8_t(1u<<(position%8));};
    bad=setup;bit(bad,339);rejected(id,bad,"mode referencing a nonexistent mapping rejected");
    bad=setup;bit(bad,307);rejected(id,bad,"reserved nonzero mode window type rejected");
    bad=setup;bad[11]=bad[12]=0;rejected(id,bad,"zero-dimensional populated codebook rejected");
    bad=setup;bad[13]=bad[14]=bad[15]=255;
    check(ValidateVorbisHeaders(id,bad)==VorbisHeadersResult::Unsupported,"oversized codebook rejected before allocation or expansion");
    rejected(id,bad,"unsupported owned codebook cannot authorize repair or lower-quality replacement");
    auto invalid_id=id;invalid_id[28]=0x55;rejected(invalid_id,setup,"invalid Vorbis block sizes rejected");
    invalid_id=id;invalid_id[29]=0;rejected(invalid_id,setup,"missing identification framing bit rejected");
    invalid_id=id;invalid_id[11]=9;
    check(ValidateVorbisHeaders(invalid_id,setup)==VorbisHeadersResult::Unsupported,"channel resource limit is not reported as validated audio");
    rejected(invalid_id,setup,"unsupported owned channel count does not confer ownership");
    bad.assign(1024*1024+1,0);
    check(ValidateVorbisHeaders(id,bad)==VorbisHeadersResult::Unsupported,"setup input limit checked before parser allocation");
}
int main(int argc,char** argv) {
    flac_buffer_coverage();
    flac_capture_recovery();
    flac_resync_work_is_bounded();
    flac_frame_larger_than_resync_window();
    vorbis_headers();
    RecordingFile file;
    auto ogg=recording_fixture::ogg();
    check(InspectRecording(ogg.data(),ogg.size(),file)&&file.complete,"complete Ogg packet and EOS coverage validated");
    ogg=recording_fixture::ogg(true);
    check(InspectRecording(ogg.data(),ogg.size(),file)&&!file.complete,"repeated BOS cannot reset coverage to hide incomplete stream");
    ogg=recording_fixture::ogg();ogg.pop_back();
    check(!InspectRecording(ogg.data(),ogg.size(),file)||!file.complete,"truncated Ogg audio cannot suppress save");
    ogg=recording_fixture::ogg();ogg.back()^=1;
    check(!InspectRecording(ogg.data(),ogg.size(),file)||!file.complete,"corrupt audio-page CRC cannot suppress save");
    if(argc>1 && std::string(argv[1])=="--issue6") {
        std::puts("PASS: supported Vorbis header validation, conservative ownership, Ogg CRC/EOS coverage");
        return 0;
    }
    auto bytes=fixture();
    check(InspectRecording(bytes.data(),bytes.size(),file)&&file.complete,"complete FLAC frames and checksums validated");
    auto header=bytes; header.resize(42);
    check(InspectRecording(header.data(),header.size(),file)&&!file.complete,"header-only FLAC is not a recording");
    auto broken=bytes; broken.back()^=1;
    check(InspectRecording(broken.data(),broken.size(),file)&&!file.complete,"corrupt frame CRC cannot suppress save");
    broken=bytes; broken.pop_back();
    check(InspectRecording(broken.data(),broken.size(),file)&&!file.complete,"interrupted final frame detected");
    Catalog catalog; catalog.title=L"Track";catalog.artist=L"Artist";catalog.album=L"Album";catalog.track=1;catalog.duration=1;
    Tags tags; tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"},{"TRACKNUMBER","1"},{"HISTORY_MEDIA_DURATION","1"},{"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"}};
    std::vector<uint8_t> tagged; std::string error;
    check(TagFlac(bytes,tags,tagged,error),"owned fixture tagged without artwork");
    check(InspectRecording(tagged.data(),tagged.size(),file)&&file.complete&&file.owned&&MatchesRecording(file,catalog),"full legacy identity and complete recording recognized");
    auto other=catalog; other.disc=2;
    check(!MatchesRecording(file,other),"different discs never upgrade one another");
    other=catalog; other.duration=100;
    check(!MatchesRecording(file,other),"different durations never upgrade one another");
    other=catalog;other.title+=L" different";
    check(!MatchesRecording(file,other),"full title distinguishes truncated path collisions");
    tags.fields.push_back({"SPOTIFY_URI","spotify:track:one"});
    check(TagFlac(bytes,tags,tagged,error)&&InspectRecording(tagged.data(),tagged.size(),file),"URI fixture");
    catalog.spotify_uri="spotify:track:two";
    check(!MatchesRecording(file,catalog),"different URI wins over matching textual metadata");
    catalog.spotify_uri="spotify:track:one";
    check(MatchesRecording(file,catalog),"matching stable URI recognized");
    std::vector<uint8_t> mp3(417*2,0);for(size_t at:{size_t(0),size_t(417)}) {
        mp3[at]=0xff;mp3[at+1]=0xfb;mp3[at+2]=0x90;mp3[at+3]=0;
    }
    Tags episode_tags;episode_tags.fields={{"TITLE","Episode"},{"ARTIST","Publisher"},{"ALBUM","Show"},
        {"SPOTIFY_URI","spotify:episode:0000000000000000000001"},{"HISTORY_MEDIA_DURATION","0.052245"},
        {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed MP3 input"},
        {"HISTORY_SAMPLES","2304"}};
    check(MakeOwnedMp3(mp3.data(),mp3.size(),episode_tags,tagged,error),"owned MP3 fixture");
    Catalog episode;episode.kind=MediaKind::Podcast;episode.title=L"Episode";episode.artist=L"Publisher";
    episode.album=episode.show=L"Show";episode.spotify_uri="spotify:episode:0000000000000000000001";episode.duration=.052245;
    check(InspectRecording(tagged.data(),tagged.size(),file)&&file.complete&&file.owned&&
          file.quality.codec==Codec::Mp3&&MatchesRecording(file,episode),"owned podcast MP3 recognized");
    Mp3Info complete_info;Tags complete_tags;
    check(InspectOwnedMp3(tagged.data(),tagged.size(),complete_info,complete_tags,error),"owned MP3 bounds available");
    auto frame_boundary=tagged;frame_boundary.resize(complete_info.audio_offset+417);
    check(InspectRecording(frame_boundary.data(),frame_boundary.size(),file)&&!file.complete&&file.owned&&
          MatchesRecording(file,episode),"frame-aligned owned MP3 truncation remains repairable");
    auto inside_frame=tagged;inside_frame.resize(inside_frame.size()-1);
    check(InspectRecording(inside_frame.data(),inside_frame.size(),file)&&!file.complete&&file.owned&&
          MatchesRecording(file,episode),"inside-frame owned MP3 truncation remains repairable");
    other=catalog; other.spotify_uri="spotify:track:two";
    check(RecordingSuffix(catalog)!=RecordingSuffix(other),"stable suffix distinguishes recording identity");
    if(argc>1) {
        std::ifstream input(argv[1],std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(input),{});
        check(InspectRecording(bytes.data(),bytes.size(),file)&&file.complete,"real complete captured file validated");
        std::printf("Validated %zu bytes, %u Hz, %u bits\n",bytes.size(),file.quality.rate,file.quality.bits);
    }
    std::puts("PASS: complete recording integrity and conservative identity matching");
}
