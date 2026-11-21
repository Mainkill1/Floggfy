#include "marked_catalog.h"
#include "../media/marked_recording.h"
#include "../tooling/async_log.h"
#include "../tooling/history_settings.h"
#include "../tooling/windows_handle.h"
#include "../media/recording_formats.h"
#include "../tooling/sqlite.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>

namespace history {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t kFileQueueLimit = 512;
constexpr std::size_t kQueryLimit = 256;
constexpr std::size_t kRowsPerPage = 32;

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

    void ClearWork() {
        rebuilding = query_pending = false;
        files.clear();
        query.reset();
        reply.reset();
    }
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

bool IsPlainDirectory(const std::wstring& native_path) {
    const auto attributes = GetFileAttributesW(native_path.c_str());
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
    auto path = WindowsPath(root.native());
    if (!IsPlainDirectory(path)) return FileStatus::Unavailable;
    path.reserve(path.size() + 1 + relative.native().size());
    // Check every directory, since GetFileAttributesExW follows intermediate junctions.
    for (auto item = relative.begin(); item != relative.end(); ++item) {
        const auto& part = item->native();
        if (part.empty() || part == L"." || part == L".." ||
            part.find(L':') != std::wstring::npos ||
            part.find(L'\0') != std::wstring::npos) return FileStatus::Missing;
        if (path.back() != L'\\' && path.back() != L'/') path += L'\\';
        path += part;
        const bool file = std::next(item) == relative.end();
        if (!file && !AllowedDirectory(*item)) return FileStatus::Missing;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
            const auto error = GetLastError();
            return win32::PathMissing(error)
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

struct Row {
    std::string path, uri, date;
    Stamp stamp;
};

enum class Mutation { Unchanged, Changed, Failed };

class Database {
    struct Queries {
        sqlite::Connection db;
        sqlite::Statement select, upsert, erase;

        explicit Queries(sqlite::Connection connection)
            : db(std::move(connection)),
              upsert(sqlite::Prepare(db,
                  "INSERT INTO marked_files(path,uri,saved_utc,bytes,write_time) VALUES(?,?,?,?,?)"
                  " ON CONFLICT(path) DO UPDATE SET uri=excluded.uri,saved_utc=excluded.saved_utc,"
                  " bytes=excluded.bytes,write_time=excluded.write_time"
                  " WHERE uri<>excluded.uri OR saved_utc<>excluded.saved_utc OR bytes<>excluded.bytes"
                  " OR write_time<>excluded.write_time")),
              erase(sqlite::Prepare(db, "DELETE FROM marked_files WHERE path=?")) {
            std::string sql = "SELECT path,uri,saved_utc,bytes,write_time FROM marked_files WHERE uri IN (?";
            for (std::size_t i = 1; i < kQueryLimit; ++i) sql += ",?";
            sql += ") AND path>'' AND (uri,path)>(?,?) ORDER BY uri,path LIMIT " + std::to_string(kRowsPerPage);
            select = sqlite::Prepare(db, sql.c_str());
        }
    };
    std::optional<Queries> queries_;
public:
    void Close() { queries_.reset(); }

    bool Open(const fs::path& root) {
        if (queries_) return true;
        if (!IsPlainDirectory(WindowsPath(root.native()))) return false;
        const auto path = root / L"Floggfy-marked.db";
        const auto db_attributes = GetFileAttributesW(WindowsPath(path.native()).c_str());
        if (db_attributes != INVALID_FILE_ATTRIBUTES &&
            (db_attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
        const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
        auto db = sqlite::Open(path.u8string(), flags, "win32-longpath");
        if (!db || sqlite3_busy_timeout(db.get(), 100) != SQLITE_OK) return false;
        constexpr auto schema =
            "CREATE TABLE IF NOT EXISTS marked_files("
            "path TEXT PRIMARY KEY COLLATE NOCASE, uri TEXT NOT NULL, saved_utc TEXT NOT NULL,"
            "bytes INTEGER NOT NULL, write_time INTEGER NOT NULL);"
            "CREATE INDEX IF NOT EXISTS marked_files_uri_path ON marked_files(uri,path);";
        if (sqlite3_exec(db.get(), schema, nullptr, nullptr, nullptr) != SQLITE_OK) return false;
        queries_.emplace(std::move(db));
        if (queries_->select && queries_->upsert && queries_->erase) return true;
        Close();
        return false;
    }

    bool ReadPage(const std::vector<std::string>& uris, const std::string& after_uri,
                  const std::string& after_path, std::vector<Row>& rows) {
        rows.clear();
        if (uris.empty()) return true;
        sqlite::Execution select(queries_->select);
        for (std::size_t i = 0; i < uris.size(); ++i)
            if (!select.BindText(i + 1, uris[i])) return false;
        if (!select.BindText(kQueryLimit + 1, after_uri) ||
            !select.BindText(kQueryLimit + 2, after_path)) return false;
        int status;
        while ((status = select.Step()) == SQLITE_ROW) {
            const Stamp stamp{static_cast<std::uint64_t>(select.Integer(3)),
                              static_cast<std::uint64_t>(select.Integer(4))};
            rows.push_back({select.Text(0), select.Text(1), select.Text(2), stamp});
        }
        return status == SQLITE_DONE;
    }

    Mutation Upsert(const Row& row) {
        sqlite::Execution insert(queries_->upsert);
        if (!insert.BindText(1, row.path) || !insert.BindText(2, row.uri) ||
            !insert.BindText(3, row.date) || !insert.BindInteger(4, row.stamp.bytes) ||
            !insert.BindInteger(5, row.stamp.written)) {
            WarnFile("could not bind file update", row.path);
            return Mutation::Failed;
        }
        return Update(insert, row.path);
    }

    Mutation Erase(const std::string& path) {
        sqlite::Execution erase(queries_->erase);
        if (!erase.BindText(1, path)) {
            WarnFile("could not bind file removal", path);
            return Mutation::Failed;
        }
        return Update(erase, path);
    }
private:
    Mutation Update(sqlite::Execution& statement, const std::string& path) {
        if (statement.Step() == SQLITE_DONE) {
            return sqlite3_changes(queries_->db.get()) != 0 ? Mutation::Changed : Mutation::Unchanged;
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
                           std::uint64_t epoch, std::uint64_t scan, Row* cached = nullptr,
                           ScanStats* stats = nullptr) {
    if (!Current(epoch, scan)) return {};
    if (stats) ++stats->scanned;
    Stamp stamp;
    const auto status = ReadStamp(root, relative, stamp);
    if (cached && status == FileStatus::Available && stamp == cached->stamp && ValidMarkedDate(cached->date))
        return std::move(*cached);

    auto key = cached ? std::move(cached->path) : relative.u8string();
    const auto record_change = [&](Mutation result, bool removed) {
        Changed(epoch, result);
        if (!stats) return;
        if (result == Mutation::Failed) ++stats->errors;
        else if (removed) ++stats->missing;
        else ++stats->indexed;
    };
    // Prune when a path is observed missing or excluded. Access failures retain its evidence.
    if (status == FileStatus::Missing && Current(epoch, scan)) record_change(db.Erase(key), true);
    if (status == FileStatus::Unavailable && stats) {
        ++stats->errors;
        WarnFile("could not access recording", key);
    }
    if (status != FileStatus::Available) return {};

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
        record_change(db.Erase(key), true);
        return {};
    }
    Row row{std::move(key), std::move(record.uri), std::move(record.saved_utc), stamp};
    record_change(db.Upsert(row), false);
    return row;
}

struct Scan {
    // Win32 enumeration uses extended paths and exposes the reparse bit before descent.
    struct Directory {
        fs::path relative;
        win32::FindHandle search;
        bool started = false;
        explicit Directory(fs::path path) : relative(std::move(path)) {}
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
            bool available = IsPlainDirectory(WindowsPath(path.native()));
            for (const auto& name : directory.relative) {
                if (!available) break;
                path /= name;
                available = IsPlainDirectory(WindowsPath(path.native()));
            }
            if (!available) {
                ++stats.errors;
                WarnFile("could not access recording directory", directory.relative.u8string());
                directories.pop_back();
                return true;
            }
            const auto pattern = (path / L"*").native();
            directory.search.value = FindFirstFileW(WindowsPath(pattern).c_str(), &entry);
            found = directory.search.value != INVALID_HANDLE_VALUE;
        } else {
            found = FindNextFileW(directory.search.value, &entry) != FALSE;
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
    std::vector<std::string> dates(query.uris.size());
    std::vector<Row> rows;
    std::string after_uri, after_path;
    for (;;) {
        if (!QueryCurrent(epoch, query.sequence)) return;
        if (!db.ReadPage(query.uris, after_uri, after_path, rows)) {
            ReleaseFailedQuery(epoch, query.sequence);
            Warn("could not read saved dates; query can be retried");
            return;
        }
        const bool has_next_page = rows.size() == kRowsPerPage;
        if (has_next_page) {
            // Refresh consumes cached rows and can change their stored URIs.
            after_uri = rows.back().uri;
            after_path = rows.back().path;
        }
        for (auto& cached : rows) {
            if (!QueryCurrent(epoch, query.sequence)) return;
            const auto path = fs::u8path(cached.path);
            auto row = Refresh(db, root, path, epoch, 0, &cached);
            if (!row) continue;
            const auto slot = static_cast<std::size_t>(
                std::find(query.uris.begin(), query.uris.end(), row->uri) - query.uris.begin());
            if (slot < dates.size() && dates[slot] < row->date) dates[slot] = std::move(row->date);
        }
        if (!has_next_page) break;
    }
    for (std::size_t i = 0; i < dates.size(); ++i)
        if (!dates[i].empty()) reply.dates.emplace_back(query.uris[i], std::move(dates[i]));
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

void FinishScan(std::uint64_t epoch, std::uint64_t sequence, const ScanStats& stats) {
    auto& state = Shared();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.epoch != epoch || !state.rebuilding || state.scan != sequence) return;
        state.rebuilding = false;
        ++state.revision;
    }
    LogEvent(LogKind::Finished, "marked-catalog", stats.Summary());
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
                    state.rebuilding || scan.sequence != 0;
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
                FinishScan(epoch, next_scan, scan.stats);
            }
        } catch (const std::exception&) {
            Warn("could not process catalog work");
            if (query) ReleaseFailedQuery(epoch, query->sequence);
            else if (file.empty() && next_scan) {
                ++scan.stats.errors;
                FinishScan(epoch, next_scan, scan.stats);
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
    state.ClearWork();
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
        state.ClearWork();
        worker = std::exchange(state.worker, nullptr);
        state.wake.notify_one();
    }
    if (worker) {
        WaitForSingleObject(worker, 500);
        CloseHandle(worker);
    }
}
}
