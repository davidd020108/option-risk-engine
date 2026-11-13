#pragma once

#include <sqlite3.h>

#include <string>

namespace ore {

// Thin RAII wrappers over the SQLite C API. Every failing call throws, so the
// caller never has to check return codes by hand.
class Statement {
public:
    Statement(sqlite3* db, sqlite3_stmt* stmt) : db_(db), stmt_(stmt) {}
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) {
        other.stmt_ = nullptr;
    }

    Statement& bind(int index, double value);
    Statement& bind(int index, int value);
    Statement& bind(int index, const std::string& value);

    bool step();  // true while a row is available
    void reset();

    int column_count() const { return sqlite3_column_count(stmt_); }
    std::string column_name(int i) const { return sqlite3_column_name(stmt_, i); }
    double column_double(int i) const { return sqlite3_column_double(stmt_, i); }
    int column_int(int i) const { return sqlite3_column_int(stmt_, i); }
    std::string column_text(int i) const;
    bool column_is_null(int i) const { return sqlite3_column_type(stmt_, i) == SQLITE_NULL; }
    bool column_is_text(int i) const { return sqlite3_column_type(stmt_, i) == SQLITE_TEXT; }
    bool column_is_integer(int i) const { return sqlite3_column_type(stmt_, i) == SQLITE_INTEGER; }

private:
    void check(int rc) const;
    sqlite3* db_;
    sqlite3_stmt* stmt_;
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database() { sqlite3_close(db_); }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void exec(const std::string& sql);
    void exec_file(const std::string& path);
    Statement prepare(const std::string& sql);
    sqlite3* handle() { return db_; }

private:
    sqlite3* db_ = nullptr;
};

// Opens a transaction and rolls it back unless commit() is called.
class Transaction {
public:
    explicit Transaction(Database& db) : db_(db) { db_.exec("BEGIN"); }
    ~Transaction() {
        if (!done_) sqlite3_exec(db_.handle(), "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void commit() {
        db_.exec("COMMIT");
        done_ = true;
    }

private:
    Database& db_;
    bool done_ = false;
};

std::string read_file(const std::string& path);

}  // namespace ore
