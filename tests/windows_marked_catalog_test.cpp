#include "../native/ui/marked_catalog.h"
#include "../native/media/marked_recording.h"
#include "../native/tooling/history_settings.h"
#include "../native/tooling/async_log.h"
#include "../native/vendor/sqlite/sqlite3.h"
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace {
const std::string uri = "spotify:track:0123456789abcdefghijAB";
const std::string early = "2025-01-01T10:00:01Z";
const std::string late = "2025-02-01T10:00:01Z";
std::atomic<bool> block_reader{false}, reader_entered{false}, fail_reader{false}, backfill{false};
std::atomic<unsigned> starts{0}, finishes{0}, cancels{0}, read_warnings{0}, reads{0};
std::atomic<unsigned> replies_ready{0}, query_failures{0};
void ReplyReady() {
    // Delivery must be signalled outside the worker lock, after publication.
    assert(history::MarkedCatalogRevision() != 0);
    history::MarkedQueryReply reply;
    assert(history::TakeMarkedReply(reply) && reply.request == "notify");
    ++replies_ready;
}
template<class Predicate> void WaitFor(Predicate predicate) {
    for (int attempt = 0; attempt != 2000; ++attempt) {
        if (predicate()) return;
        std::this_thread::sleep_for(5ms);
    }
    assert(false);
}
void Write(const fs::path& path, const std::string& date) {
    std::ofstream out(path);
    out << date << '\n';
}
std::string Query() {
    assert(history::QueueMarkedQuery("request_1", {uri}));
    history::MarkedQueryReply reply;
    WaitFor([&] { return history::TakeMarkedReply(reply); });
    assert(reply.request == "request_1");
    if (reply.dates.empty()) return {};
    assert(reply.dates.size() == 1 && reply.dates.front().first == uri);
    return reply.dates.front().second;
}
}

namespace history {
Settings GetSettings() {
    Settings settings;
    settings.metadata = backfill;
    return settings;
}
std::wstring WindowsPath(const std::wstring& path) { return path; }
void LogEvent(LogKind kind, const char*, const std::string& detail) {
    if (kind == LogKind::Warning && detail == "could not read saved dates; query can be retried") ++query_failures;
    if (kind == LogKind::Activity && detail == "rebuild started") ++starts;
    if (kind == LogKind::Activity && detail == "rebuild cancelled") ++cancels;
    if (kind == LogKind::Finished && detail.find("scanned=") != std::string::npos &&
        detail.find(" indexed=") != std::string::npos && detail.find(" missing=") != std::string::npos &&
        detail.find(" errors=") != std::string::npos) ++finishes;
    if (kind == LogKind::Warning && detail.find("older.ogg") != std::string::npos &&
        detail.find("transient test error") != std::string::npos) ++read_warnings;
}
bool ReadMarkedRecording(const std::wstring& path, MarkedRecording& record, std::string& error) {
    ++reads;
    if (block_reader) {
        reader_entered = true;
        WaitFor([] { return !block_reader.load(); });
    }
    if (fail_reader) {
        error = "transient test error";
        return false;
    }
    std::ifstream in{fs::path(path)};
    if (!std::getline(in, record.saved_utc)) return false;
    record.uri = uri;
    record.complete = record.owned = true;
    return true;
}
bool BackfillMarkedDate(const std::wstring& path, unsigned) {
    if (!backfill) return false;
    Write(fs::path(path), late);
    return true;
}
}

int main() {
    const auto base = fs::temp_directory_path() /
        (L"floggfy-marked-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto root = base / L"original";
    const auto other = base / L"other";
    fs::create_directories(root / L"Unmatched");
    fs::create_directories(root / L"DumpStreams");
    fs::create_directories(other);
    const auto a = root / L"older.ogg";
    const auto b = root / L"new.flac";
    Write(a, early);
    Write(b, late);
    Write(root / L"Unmatched" / L"ignored.ogg", late);
    Write(root / L"DumpStreams" / L"ignored.flac", late);
    Write(root / L"ignored.flac.partial", late);
    history::ConfigureMarkedCatalog(root.wstring(), true);
    assert(Query().empty());
    assert(history::QueueMarkedQuery("notify", {uri}, ReplyReady));
    WaitFor([] { return replies_ready.load() == 1; });
    auto revision = history::MarkedCatalogRevision();
    history::ConfigureMarkedCatalog(root.wstring(), true);
    assert(history::MarkedCatalogRevision() == revision);
    history::QueueMarkedFile(root.wstring(), a.wstring());
    history::QueueMarkedFile(root.wstring(), b.wstring());
    WaitFor([&] { return Query() == late; });
    assert(fs::is_regular_file(root / L"Floggfy-marked.db"));

    sqlite3* competing = nullptr;
    assert(sqlite3_open((root / L"Floggfy-marked.db").u8string().c_str(), &competing) == SQLITE_OK);
    assert(sqlite3_exec(competing, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(history::QueueMarkedQuery("locked_query", {uri}));
    history::MarkedQueryReply locked;
    WaitFor([&] { return query_failures.load() || history::TakeMarkedReply(locked); });
    assert(locked.request.empty());
    assert(sqlite3_exec(competing, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(sqlite3_close(competing) == SQLITE_OK);
    assert(history::QueueMarkedQuery("locked_query", {uri}));
    WaitFor([&] { return history::TakeMarkedReply(locked); });
    assert(locked.request == "locked_query" && locked.dates.size() == 1 && locked.dates[0].second == late);

    fs::remove(b);
    assert(Query() == early);
    Write(a, late + "\nchanged");
    fail_reader = true;
    assert(Query().empty());
    assert(read_warnings == 1);
    fail_reader = false;
    assert(Query() == late);
    Write(a, early + "\nchanged again");
    assert(Query() == early);

    Write(a, late + "\nchanged for duplicate request");
    const auto before_retry = reads.load();
    block_reader = true;
    reader_entered = false;
    fail_reader = true;
    assert(history::QueueMarkedQuery("retry", {uri}));
    WaitFor([] { return reader_entered.load(); });
    assert(history::QueueMarkedQuery("retry", {uri}));
    block_reader = false;
    history::MarkedQueryReply repeated;
    WaitFor([&] { return history::TakeMarkedReply(repeated); });
    assert(repeated.request == "retry" && repeated.dates.empty());
    assert(reads == before_retry + 1);
    fail_reader = false;
    assert(history::QueueMarkedQuery("retry", {uri}));
    WaitFor([&] { return history::TakeMarkedReply(repeated); });
    assert(repeated.request == "retry" && repeated.dates.size() == 1 && repeated.dates[0].second == late);
    Write(a, early + "\nrestore early date");
    assert(Query() == early);

    reader_entered = false;
    Write(root / L"new-from-scan.m4a", late);
    block_reader = true;
    assert(history::ToggleMarkedRebuild());
    WaitFor([] { return reader_entered.load(); });
    assert(history::MarkedRebuildRunning() && history::ToggleMarkedRebuild());
    block_reader = false;
    assert(Query() == early);

    assert(history::QueueMarkedQuery("stale", {uri}));
    history::ConfigureMarkedCatalog(other.wstring(), true);
    history::MarkedQueryReply reply;
    assert(!history::TakeMarkedReply(reply));
    assert(Query().empty());
    history::ConfigureMarkedCatalog(root.wstring(), true);
    assert(Query() == early);
    assert(history::ToggleMarkedRebuild());
    WaitFor([] { return !history::MarkedRebuildRunning(); });
    assert(Query() == late);
    fs::remove(root / L"new-from-scan.m4a");
    assert(Query() == early);

    const auto dirt = root / L"more";
    fs::create_directory(dirt);
    for (int index = 0; index != 35; ++index) {
        Write(dirt / (L"duplicate-" + std::to_wstring(index) + L".mp3"), early);
    }
    const auto legacy = root / L"z-legacy.ogg";
    Write(legacy, {});
    assert(history::ToggleMarkedRebuild());
    WaitFor([] { return !history::MarkedRebuildRunning(); });
    assert(Query() == early);
    backfill = true;
    assert(history::ToggleMarkedRebuild());
    WaitFor([] { return !history::MarkedRebuildRunning(); });
    assert(Query() == late);
    assert(starts == 4 && cancels == 1 && finishes == 3);

    assert(!history::QueueMarkedQuery("invalid token", {uri}));
    assert(!history::QueueMarkedQuery("large", std::vector<std::string>(257, uri)));
    history::ConfigureMarkedCatalog(root.wstring(), false);
    assert(!history::QueueMarkedQuery("disabled", {uri}));
    assert(!history::TakeMarkedReply(reply) && !history::ToggleMarkedRebuild());
    history::StopMarkedCatalog();
    fs::remove_all(base);
}
