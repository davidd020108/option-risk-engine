#include "db.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ore {

void Statement::check(int rc) const {
    if (rc != SQLITE_OK) throw std::runtime_error(std::string("sqlite: ") + sqlite3_errmsg(db_));
}

Statement& Statement::bind(int index, double value) {
    check(sqlite3_bind_double(stmt_, index, value));
    return *this;
}

Statement& Statement::bind(int index, int value) {
    check(sqlite3_bind_int(stmt_, index, value));
    return *this;
}

Statement& Statement::bind(int index, const std::string& value) {
    check(sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT));
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw std::runtime_error(std::string("sqlite step: ") + sqlite3_errmsg(db_));
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

std::string Statement::column_text(int i) const {
    const unsigned char* text = sqlite3_column_text(stmt_, i);
    return text ? reinterpret_cast<const char*>(text) : "";
}

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("cannot open database " + path + ": " + msg);
    }
    exec("PRAGMA foreign_keys = ON");
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("sqlite exec: " + msg);
    }
}

void Database::exec_file(const std::string& path) { exec(read_file(path)); }

Statement Database::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("sqlite prepare: ") + sqlite3_errmsg(db_));
    }
    return Statement(db_, stmt);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace ore
