#pragma once

#include "../vendor/sqlite/sqlite3.h"
#include <memory>
#include <string>

namespace sqlite {
struct Close {
    void operator()(sqlite3* db) const noexcept { sqlite3_close_v2(db); }
};
using Connection = std::unique_ptr<sqlite3, Close>;

inline Connection Open(const std::string& path, int flags, const char* vfs = nullptr) {
    sqlite3* db = nullptr;
    const int status = sqlite3_open_v2(path.c_str(), &db, flags, vfs);
    Connection connection(db);
    if (status != SQLITE_OK) return {};
    return connection;
}

struct Finalize {
    void operator()(sqlite3_stmt* statement) const noexcept { sqlite3_finalize(statement); }
};
using Statement = std::unique_ptr<sqlite3_stmt, Finalize>;

inline Statement Prepare(const Connection& db, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    sqlite3_prepare_v2(db.get(), sql, -1, &statement, nullptr);
    return Statement(statement);
}

// One execution borrows a prepared statement. Always release locks and bound values
// before the next execution, including on early return or allocation failure.
class Execution {
    sqlite3_stmt* statement_;
public:
    explicit Execution(const Statement& statement) : statement_(statement.get()) {}
    Execution(const Execution&) = delete;
    Execution& operator=(const Execution&) = delete;
    ~Execution() {
        sqlite3_reset(statement_);
        sqlite3_clear_bindings(statement_);
    }

    bool BindText(int index, const std::string& value) {
        return sqlite3_bind_text64(statement_, index, value.c_str(), value.size(),
            SQLITE_TRANSIENT, SQLITE_UTF8) == SQLITE_OK;
    }
    bool BindInteger(int index, sqlite3_int64 value) {
        return sqlite3_bind_int64(statement_, index, value) == SQLITE_OK;
    }
    int Step() { return sqlite3_step(statement_); }
    std::string Text(int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        return value ? std::string(reinterpret_cast<const char*>(value),
            sqlite3_column_bytes(statement_, column)) : std::string{};
    }
    sqlite3_int64 Integer(int column) const { return sqlite3_column_int64(statement_, column); }
};
}
