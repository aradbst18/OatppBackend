#include "app/infrastructure/database/SqliteDatabase.hpp"

#if !defined(APP_DATABASE_SQLITE) && !defined(APP_DATABASE_SQLCIPHER)
#error "SqliteDatabase.cpp requires a database backend (configure with -DAPP_DATABASE_BACKEND=SQLITE|SQLCIPHER)"
#endif  // !APP_DATABASE_SQLITE && !APP_DATABASE_SQLCIPHER

#include <filesystem>

#include <sqlite3.h>

#include "app/errors/AppException.hpp"

namespace app::infrastructure::database
{

SqliteDatabase::SqliteDatabase(std::string path) : path_(std::move(path))
{
}

SqliteDatabase::~SqliteDatabase()
{
    close();
}

void SqliteDatabase::open()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ != nullptr)
    {
        return;
    }

    // Ensure the parent directory exists for file-backed databases.
    if (path_ != ":memory:")
    {
        const auto sep = path_.find_last_of('/');
        if (sep != std::string::npos)
        {
            std::error_code ec;
            std::filesystem::create_directories(path_.substr(0, sep), ec);
            (void)ec;
        }
    }

    sqlite3* db = nullptr;
    const int rc = sqlite3_open(path_.c_str(), &db);
    if (rc != SQLITE_OK)
    {
        const std::string msg = db != nullptr ? sqlite3_errmsg(db) : "sqlite3_open failed";
        if (db != nullptr)
        {
            sqlite3_close(db);
        }

        throw errors::database::ConnectionFailed("Cannot open database '" + path_ + "': " + msg);
    }

    db_ = db;

    // Fail closed on busy/locked rather than hanging forever.
    sqlite3_busy_timeout(db_, 5000);
}

void SqliteDatabase::close() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ != nullptr)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteDatabase::isOpen() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return db_ != nullptr;
}

void SqliteDatabase::ensureOpen() const
{
    if (db_ == nullptr)
    {
        throw errors::database::ConnectionFailed("Database is not open: " + path_);
    }
}

std::vector<std::vector<std::string>> SqliteDatabase::execute(const std::string& sql, const std::vector<std::string>& params)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ensureOpen();

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        throw errors::database::QueryFailed(std::string("SQL prepare failed: ") + sqlite3_errmsg(db_));
    }

    struct Finalizer
    {
        sqlite3_stmt* s;
        ~Finalizer()
        {
            sqlite3_finalize(s);
        }
    } finalizer{stmt};

    for (std::size_t i = 0; i < params.size(); ++i)
    {
        if (sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i].c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        {
            throw errors::database::QueryFailed(std::string("SQL bind failed: ") + sqlite3_errmsg(db_));
        }
    }

    std::vector<std::vector<std::string>> rows;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const int cols = sqlite3_column_count(stmt);
        std::vector<std::string> row;
        row.reserve(static_cast<std::size_t>(cols));

        for (int c = 0; c < cols; ++c)
        {
            const auto* text = sqlite3_column_text(stmt, c);
            row.emplace_back(text != nullptr ? reinterpret_cast<const char*>(text) : "");
        }

        rows.push_back(std::move(row));
    }

    if (rc != SQLITE_DONE)
    {
        throw errors::database::QueryFailed(std::string("SQL step failed: ") + sqlite3_errmsg(db_));
    }

    return rows;
}

}  // namespace app::infrastructure::database
