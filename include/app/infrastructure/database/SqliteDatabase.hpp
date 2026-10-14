#pragma once

// SqliteDatabase is the base class for both backends (SqlcipherDatabase derives
// from it), so it must stay includable in SQLITE and SQLCIPHER builds. Only
// fail when neither backend macro is defined (e.g. apply_app_definitions()
// was not applied to the target).
#if !defined(APP_DATABASE_SQLITE) && !defined(APP_DATABASE_SQLCIPHER)
#error "No database backend defined (require APP_DATABASE_SQLITE or APP_DATABASE_SQLCIPHER via -DAPP_DATABASE_BACKEND=SQLITE|SQLCIPHER)"
#endif  // !APP_DATABASE_SQLITE && !APP_DATABASE_SQLCIPHER

#include <mutex>
#include <string>

#include "app/infrastructure/database/IDatabase.hpp"

struct sqlite3;

namespace app::infrastructure::database
{

/**
 * Plain SQLite backend (no encryption). Uses the system SQLite3 library
 * through prepared statements with bound parameters.
 */
class SqliteDatabase : public IDatabase
{
public:
    explicit SqliteDatabase(std::string path);
    ~SqliteDatabase() override;

    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;

    void open() override;
    void close() noexcept override;
    bool isOpen() const noexcept override;

    std::vector<std::vector<std::string>> execute(const std::string& sql, const std::vector<std::string>& params = {}) override;

    std::string backendName() const noexcept override
    {
        return "SQLITE";
    }

    const std::string& path() const noexcept
    {
        return path_;
    }

protected:
    sqlite3* handle() noexcept
    {
        return db_;
    }

    std::mutex& mutex() noexcept
    {
        return mutex_;
    }

    void ensureOpen() const;

private:
    std::string path_;
    sqlite3* db_{nullptr};
    mutable std::mutex mutex_;
};

}  // namespace app::infrastructure::database
