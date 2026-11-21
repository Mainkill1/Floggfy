#include "marked_catalog.h"
#include "marked_recording.h"
#include "async_log.h"
#include "history_settings.h"
#include "recording_formats.h"
#include "vendor/sqlite/sqlite3.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace history {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t kFileQueueLimit = 512;
constexpr std::size_t kQueryLimit = 256;

struct Query {
    std::string request;
    std::vector<std::string> uris;
    std::uint64_t sequence;
    void (*ready)();
};

struct State {
    std::mutex mutex;
    std::condition_variable wake;
    HANDLE worker = nullptr;
    bool stopped = false;
    bool enabled = false;
    bool rebuilding = false;
    std::wstring root;
    std::uint64_t epoch = 0, revision = 0, scan = 0, query_sequence = 0;
    std::deque<std::wstring> files;
    std::shared_ptr<const Query> query;
    bool query_pending = false;
    std::optional<MarkedQueryReply> reply;
};

State& Shared() {
    // The worker can outlive a bounded shutdown wait during process exit.
    static State* const state = new State;
    return *state;
}

void Warn(const std::string& reason) {
    LogEvent(LogKind::Warning, "marked-catalog", reason);
}

std::string LogField(std::string value, std::size_t limit) {
    if (value.size() > limit) {
        value.resize(limit);
        value += "...";
    }
    for (auto& ch : value) {
        if (static_cast<unsigned char>(ch) < 32 || ch == 127) ch = '?';
    }
    return value;
}

void WarnFile(const char* reason, const std::string& path, const std::string& detail = {}) {
    auto message = std::string(reason) + " [" + LogField(path, 300) + "]";
    if (!detail.empty()) message += " (" + LogField(detail, 140) + ")";
    Warn(message);
}

struct ScanStats {
    std::uint64_t scanned = 0, indexed = 0, missing = 0, errors = 0;

    std::string Summary() const {
        return "rebuild finished: scanned=" + std::to_string(scanned) +
            " indexed=" + std::to_string(indexed) + " missing=" + std::to_string(missing) +
            " errors=" + std::to_string(errors);
    }
};

bool Equal(const std::wstring& lhs, const std::wstring& rhs) {
    return CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool AllowedDirectory(const fs::path& name) {
    return !Equal(name.native(), L"Unmatched") && !Equal(name.native(), L"DumpStreams");
}

bool IsPlainDirectory(const fs::path& path) {
    const auto attributes = GetFileAttributesW(WindowsPath(path.native()).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool SupportedFile(const fs::path& path) {
    const auto extension = path.extension().native();
    return std::any_of(kRecordingFormats.begin(), kRecordingFormats.end(),
        [&](const RecordingFormat& format) { return Equal(extension, format.extension); });
}

fs::path Normalize(const std::wstring& path) {
    fs::path result(path);
    result = result.lexically_normal();
    result.make_preferred();
    if (result.has_relative_path() && !result.has_filename()) result = result.parent_path();
    return result;
}

fs::path RelativeTo(const fs::path& root, const fs::path& file) {
    auto part = file.begin();
    for (const auto& directory : root) {
        if (part == file.end() || !Equal(directory.native(), part->native())) return {};
        ++part;
    }
    fs::path relative;
    for (; part != file.end(); ++part) relative /= *part;
    return relative;
}

enum class FileStatus { Available, Missing, Unavailable };

struct Stamp {
    std::uint64_t bytes = 0, written = 0;
    bool operator==(const Stamp& other) const {
        return bytes == other.bytes && written == other.written;
    }
};

FileStatus ReadStamp(const fs::path& root, const fs::path& relative, Stamp& stamp) {
    if (relative.has_root_path() || !SupportedFile(relative)) return FileStatus::Missing;
    if (!IsPlainDirectory(root)) return FileStatus::Unavailable;
    fs::path path = root;
    // Check every directory, since GetFileAttributesExW follows intermediate junctions.
    for (auto item = relative.begin(); item != relative.end(); ++item) {
        const auto& part = *item;
        if (part.empty() || part == L"." || part == L".." ||
            part.native().find(L':') != std::wstring::npos ||
            part.native().find(L'\0') != std::wstring::npos) return FileStatus::Missing;
        path /= part;
        const bool file = std::next(item) == relative.end();
        if (!file && !AllowedDirectory(part)) return FileStatus::Missing;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(WindowsPath(path.native()).c_str(), GetFileExInfoStandard, &data)) {
            const auto error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                ? FileStatus::Missing : FileStatus::Unavailable;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return FileStatus::Missing;
        if (file == bool(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return FileStatus::Missing;
        if (file) {
            stamp.bytes = (std::uint64_t{data.nFileSizeHigh} << 32) | data.nFileSizeLow;
            stamp.written = (std::uint64_t{data.ftLastWriteTime.dwHighDateTime} << 32) |
                data.ftLastWriteTime.dwLowDateTime;
        }
    }
    return FileStatus::Available;
}

class Statement {
    sqlite3_stmt* statement_ = nullptr;
public:
    Statement(sqlite3* db, const char* sql) {
        sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr);
    }
    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    explicit operator bool() const { return statement_ != nullptr; }
    bool Text(int index, const std::string& value) {
        return sqlite3_bind_text(statement_, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    }
    void Number(int index, std::uint64_t value) {
        sqlite3_bind_int64(statement_, index, static_cast<sqlite3_int64>(value));
    }
    int Step() { return sqlite3_step(statement_); }
    std::string Text(int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        return value ? std::string(reinterpret_cast<const char*>(value),
            sqlite3_column_bytes(statement_, column)) : std::string{};
    }
    std::uint64_t Number(int column) const { return sqlite3_column_int64(statement_, column); }
};

struct Row {
    std::string path, uri, date;
    Stamp stamp;
};

enum class Mutation { Unchanged, Changed, Failed };

class Database {
    sqlite3* db_ = nullptr;
public:
    Database() = default;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database() { Close(); }
    void Close() {
        sqlite3_close(db_);
        db_ = nullptr;
    }

    bool Open(const fs::path& root) {
        if (db_) return true;
        if (!IsPlainDirectory(root)) return false;
        const auto path = root / L"Floggfy-marked.db";
        const auto db_attributes = GetFileAttributesW(WindowsPath(path.native()).c_str());
        if (db_attributes != INVALID_FILE_ATTRIBUTES &&
            (db_attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
        const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
        if (sqlite3_open_v2(path.u8string().c_str(), &db_, flags, "win32-longpath") != SQLITE_OK) {
            Close();
            return false;
        }
        sqlite3_busy_timeout(db_, 100);
        constexpr auto schema =
            "CREATE TABLE IF NOT EXISTS marked_files("
            "path TEXT PRIMARY KEY COLLATE NOCASE, uri TEXT NOT NULL, saved_utc TEXT NOT NULL,"
            "bytes INTEGER NOT NULL, write_time INTEGER NOT NULL);"
            "CREATE INDEX IF NOT EXISTS marked_files_uri_path ON marked_files(uri,path);";
        if (sqlite3_exec(db_, schema, nullptr, nullptr, nullptr) == SQLITE_OK) return true;
        Close();
        return false;
    }

    std::optional<std::vector<Row>> Rows(const std::string& uri, const std::string& after) {
        Statement select(db_, "SELECT path,saved_utc,bytes,write_time FROM marked_files"
            " WHERE uri=? AND path>? ORDER BY path LIMIT 32");
        if (!select || !select.Text(1, uri) || !select.Text(2, after)) return std::nullopt;
        std::vector<Row> rows;
        int status;
        while ((status = select.Step()) == SQLITE_ROW) {
            rows.push_back({select.Text(0), uri, select.Text(1), {select.Number(2), select.Number(3)}});
        }
        if (status != SQLITE_DONE) return std::nullopt;
        return rows;
    }

    Mutation Upsert(const Row& row) {
        Statement insert(db_,
            "INSERT INTO marked_files(path,uri,saved_utc,bytes,write_time) VALUES(?,?,?,?,?)"
            " ON CONFLICT(path) DO UPDATE SET uri=excluded.uri,saved_utc=excluded.saved_utc,"
            " bytes=excluded.bytes,write_time=excluded.write_time"
            " WHERE uri<>excluded.uri OR saved_utc<>excluded.saved_utc OR bytes<>excluded.bytes"
            " OR write_time<>excluded.write_time");
        if (!insert) {
            WarnFile("could not prepare file update", row.path);
            return Mutation::Failed;
        }
        insert.Text(1, row.path);
        insert.Text(2, row.uri);
        insert.Text(3, row.date);
        insert.Number(4, row.stamp.bytes);
        insert.Number(5, row.stamp.written);
        return Update(insert, row.path);
    }

    Mutation Erase(const std::string& path) {
        Statement erase(db_, "DELETE FROM marked_files WHERE path=?");
        if (!erase) {
            WarnFile("could not prepare file removal", path);
            return Mutation::Failed;
        }
        erase.Text(1, path);
        return Update(erase, path);
    }
private:
    Mutation Update(Statement& statement, const std::string& path) {
        if (statement.Step() == SQLITE_DONE) {
            return sqlite3_changes(db_) != 0 ? Mutation::Changed : Mutation::Unchanged;
        }
        WarnFile("could not change file evidence", path);
        return Mutation::Failed;
    }
};

bool Current(std::uint64_t epoch, std::uint64_t scan = 0) {
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    return !state.stopped && state.epoch == epoch &&
        (!scan || (state.rebuilding && state.scan == scan));
}

void Changed(std::uint64_t epoch, Mutation result) {
    if (result != Mutation::Changed) return;
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.epoch == epoch) ++state.revision;
}

std::optional<Row> Refresh(Database& db, const fs::path& root, const fs::path& relative,
                           std::uint64_t epoch, std::uint64_t scan, const Row* cached = nullptr,
                           ScanStats* stats = nullptr) {
    if (!Current(epoch, scan)) return {};
    const auto key = cached ? cached->path : relative.u8string();
    if (stats) ++stats->scanned;
    Stamp stamp;
    const auto status = ReadStamp(root, relative, stamp);
    // Prune when a path is observed missing or excluded. Access failures retain its evidence.
    if (status == FileStatus::Missing && Current(epoch, scan)) {
        const auto result = db.Erase(key);
        Changed(epoch, result);
        if (stats) {
            if (result == Mutation::Failed) ++stats->errors;
            else ++stats->missing;
        }
    }
    if (status == FileStatus::Unavailable && stats) {
        ++stats->errors;
        WarnFile("could not access recording", key);
    }
    if (status != FileStatus::Available) return {};
    if (cached && stamp == cached->stamp && ValidMarkedDate(cached->date)) return *cached;

    const auto path = (root / relative).native();
    MarkedRecording record;
    std::string error;
    if (!ReadMarkedRecording(path, record, error)) {
        if (stats) ++stats->errors;
        WarnFile("could not read recording metadata", key, error);
        return {};
    }
    if (scan && record.complete && record.owned && ValidSpotifyTrackUri(record.uri) &&
        record.saved_utc.empty() && Current(epoch, scan)) {
        const auto settings = GetSettings();
        if (settings.downloads && settings.metadata && settings.update_existing_metadata) {
            if (!Current(epoch, scan)) return {};
            const bool backfilled = BackfillMarkedDate(path, settings.generation);
            if (!Current(epoch, scan)) return {};
            if (!backfilled || ReadStamp(root, relative, stamp) != FileStatus::Available ||
                !ReadMarkedRecording(path, record, error)) {
                if (stats) ++stats->errors;
                WarnFile("could not backfill recording date", key, error);
                return {};
            }
        } else {
            WarnFile("recording date is missing and metadata updates are disabled", key);
        }
    }
    Stamp after;
    if (!Current(epoch, scan)) return {};
    if (ReadStamp(root, relative, after) != FileStatus::Available || !(stamp == after)) {
        if (stats) ++stats->errors;
        WarnFile("recording changed during catalog read", key);
        return {};
    }
    if (!record.complete || !record.owned || !ValidSpotifyTrackUri(record.uri) ||
        !ValidMarkedDate(record.saved_utc)) {
        const auto result = db.Erase(key);
        Changed(epoch, result);
        if (stats) {
            if (result == Mutation::Failed) ++stats->errors;
            else ++stats->missing;
        }
        return {};
    }
    Row row{key, std::move(record.uri), std::move(record.saved_utc), stamp};
    const auto result = db.Upsert(row);
    Changed(epoch, result);
    if (stats) {
        if (result == Mutation::Failed) ++stats->errors;
        else ++stats->indexed;
    }
    return row;
}

struct Scan {
    // Win32 enumeration uses extended paths and exposes the reparse bit before descent.
    struct Directory {
        fs::path relative;
        HANDLE handle = INVALID_HANDLE_VALUE;
        bool started = false;
        explicit Directory(fs::path path) : relative(std::move(path)) {}
        ~Directory() { if (handle != INVALID_HANDLE_VALUE) FindClose(handle); }
        Directory(const Directory&) = delete;
        Directory& operator=(const Directory&) = delete;
    };
    std::deque<Directory> directories;
    std::uint64_t sequence = 0;
    ScanStats stats;
    void Reset(std::uint64_t scan) {
        directories.clear();
        sequence = scan;
        stats = {};
        if (scan) directories.emplace_back(fs::path{});
    }
    bool Step(Database& db, const fs::path& root, std::uint64_t epoch) {
        if (directories.empty()) return false;
        auto& directory = directories.back();
        WIN32_FIND_DATAW entry{};
        bool found;
        if (!directory.started) {
            directory.started = true;
            auto path = root;
            bool available = IsPlainDirectory(path);
            for (const auto& name : directory.relative) {
                if (!available) break;
                path /= name;
                available = IsPlainDirectory(path);
            }
            if (!available) {
                ++stats.errors;
                WarnFile("could not access recording directory", directory.relative.u8string());
                directories.pop_back();
                return true;
            }
            const auto pattern = (path / L"*").native();
            directory.handle = FindFirstFileW(WindowsPath(pattern).c_str(), &entry);
            found = directory.handle != INVALID_HANDLE_VALUE;
        } else {
            found = FindNextFileW(directory.handle, &entry) != FALSE;
        }
        if (!found) {
            const auto error = GetLastError();
            if (error != ERROR_NO_MORE_FILES && error != ERROR_FILE_NOT_FOUND) {
                ++stats.errors;
                WarnFile("could not enumerate recording directory", directory.relative.u8string());
            }
            directories.pop_back();
            return true;
        }
        const fs::path name(entry.cFileName);
        if (name == L"." || name == L".." || (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return true;
        }
        const auto relative = directory.relative / name;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (AllowedDirectory(name)) directories.emplace_back(relative);
        } else if (SupportedFile(relative)) {
            Refresh(db, root, relative, epoch, sequence, nullptr, &stats);
        }
        return true;
    }
};

bool QueryCurrent(std::uint64_t epoch, std::uint64_t sequence) {
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    return !state.stopped && state.epoch == epoch && state.query_sequence == sequence;
}

// No successful reply exists for this token. Let the renderer retry it on its
// existing backoff without caching absent dates or disturbing a newer request.
void ReleaseFailedQuery(std::uint64_t epoch, std::uint64_t sequence) {
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.epoch == epoch && state.query_sequence == sequence) state.query.reset();
}

void AnswerQuery(Database& db, bool available, const fs::path& root, const Query& query,
                 std::uint64_t epoch) {
    if (!available) {
        ReleaseFailedQuery(epoch, query.sequence);
        return;
    }
    MarkedQueryReply reply;
    reply.request = query.request;
    std::unordered_map<std::string, std::string> latest;
    for (const auto& uri : query.uris) latest.try_emplace(uri);
    for (const auto& uri : query.uris) {
        std::string after;
        for (;;) {
            if (!QueryCurrent(epoch, query.sequence)) return;
            const auto rows = db.Rows(uri, after);
            if (!rows) {
                ReleaseFailedQuery(epoch, query.sequence);
                Warn("could not read saved dates; query can be retried");
                return;
            }
            if (rows->empty()) break;
            after = rows->back().path;
            for (const auto& cached : *rows) {
                if (!QueryCurrent(epoch, query.sequence)) return;
                const auto row = Refresh(db, root, fs::u8path(cached.path), epoch, 0, &cached);
                if (!row) continue;
                const auto date = latest.find(row->uri);
                if (date != latest.end() && date->second < row->date) date->second = row->date;
            }
        }
    }
    for (const auto& uri : query.uris) {
        auto& date = latest.at(uri);
        if (!date.empty()) reply.dates.emplace_back(uri, std::move(date));
    }
    auto& state = Shared();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.stopped || state.epoch != epoch || state.query_sequence != query.sequence) return;
        reply.revision = state.revision;
        state.reply = std::move(reply);
    }
    // Wake the renderer when the result is ready instead of waiting for its heartbeat.
    if (query.ready) query.ready();
}

bool FinishScan(std::uint64_t epoch, std::uint64_t sequence) {
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.epoch != epoch || !state.rebuilding || state.scan != sequence) return false;
    state.rebuilding = false;
    ++state.revision;
    return true;
}

DWORD WINAPI Run(void*) {
    auto& state = Shared();
    std::uint64_t epoch = 0;
    fs::path root;
    Database db;
    Scan scan;
    bool open_warning = false;
    for (;;) {
        std::shared_ptr<const Query> query;
        std::wstring file, next_root;
        std::uint64_t next_epoch, next_scan;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.wake.wait(lock, [&] {
                return state.stopped || state.epoch != epoch || state.query_pending || !state.files.empty() ||
                    state.rebuilding || scan.sequence != (state.rebuilding ? state.scan : 0);
            });
            if (state.stopped) return 0;
            next_epoch = state.epoch;
            if (next_epoch != epoch && state.enabled) next_root = state.root;
            next_scan = state.rebuilding ? state.scan : 0;
            if (state.query_pending) {
                query = state.query;
                state.query_pending = false;
            } else if (!state.files.empty()) {
                file = std::move(state.files.front());
                state.files.pop_front();
            }
        }
        try {
            if (next_epoch != epoch) {
                db.Close();
                scan.Reset(0);
                epoch = next_epoch;
                root.clear();
                root = Normalize(next_root);
                open_warning = false;
            }
            if (scan.sequence != next_scan) scan.Reset(next_scan);
            if (root.empty() || (!query && file.empty() && !next_scan) || !Current(epoch)) continue;
            const bool available = db.Open(root);
            if (!available && !open_warning) Warn("could not open the save root database");
            open_warning = !available;
            if (query) {
                AnswerQuery(db, available, root, *query, epoch);
            } else if (!file.empty()) {
                const auto relative = RelativeTo(root, Normalize(file));
                if (available && !relative.empty()) Refresh(db, root, relative, epoch, 0);
            } else if (!available || !scan.Step(db, root, epoch)) {
                if (!available) ++scan.stats.errors;
                if (FinishScan(epoch, next_scan)) {
                    LogEvent(LogKind::Finished, "marked-catalog", scan.stats.Summary());
                }
            }
        } catch (const std::exception&) {
            Warn("could not process catalog work");
            if (query) ReleaseFailedQuery(epoch, query->sequence);
            else if (file.empty() && next_scan && FinishScan(epoch, next_scan)) {
                ++scan.stats.errors;
                LogEvent(LogKind::Finished, "marked-catalog", scan.stats.Summary());
            }
        }
    }
}

bool Start(State& state) {
    if (!state.worker) {
        state.worker = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
        if (state.worker) SetThreadPriority(state.worker, THREAD_PRIORITY_BELOW_NORMAL);
    }
    return state.worker != nullptr;
}
} // namespace

void ConfigureMarkedCatalog(const std::wstring& root, bool enabled) {
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    enabled = enabled && !root.empty();
    if (state.stopped) return;
    if (state.root == root && state.enabled == enabled) {
        if (enabled && !state.worker) Start(state);
        return;
    }
    state.root = root;
    state.enabled = enabled;
    state.rebuilding = false;
    state.files.clear();
    state.query.reset();
    state.query_pending = false;
    state.reply.reset();
    ++state.epoch;
    ++state.revision;
    if (enabled) Start(state);
    state.wake.notify_one();
}

void QueueMarkedFile(const std::wstring& root, const std::wstring& path) {
    auto& state = Shared();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.stopped || !state.enabled || state.root != root || !Start(state)) return;
        if (std::find(state.files.begin(), state.files.end(), path) != state.files.end()) return;
        if (state.files.size() < kFileQueueLimit) {
            state.files.push_back(path);
            state.wake.notify_one();
            return;
        }
    }
    Warn("file queue is full; rebuild can recover skipped files");
}

bool ToggleMarkedRebuild() {
    auto& state = Shared();
    bool starting;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.stopped || !state.enabled || !Start(state)) return false;
        starting = state.rebuilding = !state.rebuilding;
        if (starting) ++state.scan;
        else ++state.revision;
    }
    LogEvent(LogKind::Activity, "marked-catalog", starting ? "rebuild started" : "rebuild cancelled");
    state.wake.notify_one();
    return true;
}

bool MarkedRebuildRunning() {
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.rebuilding;
}

std::uint64_t MarkedCatalogRevision() {
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.revision;
}

bool QueueMarkedQuery(const std::string& request, const std::vector<std::string>& uris, void (*ready)()) {
    if (request.empty() || request.size() > 64 || uris.size() > kQueryLimit) return false;
    for (const auto ch : request) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
    }
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.stopped || !state.enabled || !Start(state)) return false;
    Query query{request, {}, 0, ready};
    for (const auto& uri : uris) {
        if (ValidSpotifyTrackUri(uri) && std::find(query.uris.begin(), query.uris.end(), uri) == query.uris.end()) {
            query.uris.push_back(uri);
        }
    }
    // Keep exact retries attached to queued work, an active read, or an untaken reply.
    if (state.query && state.query->request == request && state.query->uris == query.uris) return true;
    query.sequence = ++state.query_sequence;
    state.query = std::make_shared<const Query>(std::move(query));
    state.query_pending = true;
    state.reply.reset();
    state.wake.notify_one();
    return true;
}

bool TakeMarkedReply(MarkedQueryReply& reply) {
    auto& state = Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.reply) return false;
    reply = std::move(*state.reply);
    state.reply.reset();
    state.query.reset();
    return true;
}

void StopMarkedCatalog() {
    auto& state = Shared();
    HANDLE worker;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.stopped) return;
        state.stopped = true;
        state.rebuilding = false;
        state.files.clear();
        state.query.reset();
        state.query_pending = false;
        state.reply.reset();
        worker = std::exchange(state.worker, nullptr);
        state.wake.notify_one();
    }
    if (worker) {
        WaitForSingleObject(worker, 500);
        CloseHandle(worker);
    }
}
}
