#include "app/infrastructure/database/SqlcipherDatabase.hpp"

#ifndef APP_WITH_SQLCIPHER
#error "SqlcipherDatabase.cpp requires APP_WITH_SQLCIPHER (configure with -DAPP_ENABLE_SQLCIPHER=ON)"
#endif  // !APP_WITH_SQLCIPHER

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include <sqlite3.h>

#include "oatpp-sqlite/Connection.hpp"
#include "oatpp-sqlite/ConnectionProvider.hpp"
#include "oatpp-sqlite/Executor.hpp"

#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/DatabaseExecutor.hpp"
#include "app/infrastructure/database/OrmSupport.hpp"
#include "app/logging/Logger.hpp"

namespace app::infrastructure::database
{

namespace
{

/**
 * Connection provider that owns every native-handle concern of the file
 * backend: the busy timeout, the optional SQLCipher key (applied through the
 * sqlite3_key C API before any other statement) and the key validation.
 *
 * The adapter hands out connections through this class, so a handle can never
 * reach the executor unkeyed. Keying here instead of with a `PRAGMA key`
 * statement keeps key material out of SQL text entirely.
 */
class SqlcipherConnectionProvider final : public oatpp::sqlite::ConnectionProvider
{
public:
    SqlcipherConnectionProvider(const oatpp::String& filename, std::string key, int busyTimeoutMs)
        : oatpp::sqlite::ConnectionProvider(filename), key_(std::move(key)), busyTimeoutMs_(busyTimeoutMs)
    {
    }

    oatpp::provider::ResourceHandle<oatpp::sqlite::Connection> get() override
    {
        auto connection = oatpp::sqlite::ConnectionProvider::get();
        sqlite3* handle = connection.object->getHandle();

        // Fail closed on busy/locked rather than hanging forever.
        sqlite3_busy_timeout(handle, busyTimeoutMs_);

        if (!key_.empty())
        {
            if (sqlite3_key(handle, key_.data(), static_cast<int>(key_.size())) != SQLITE_OK)
            {
                throw std::runtime_error("failed to apply the SQLCipher key: " + std::string(sqlite3_errmsg(handle)));
            }

            validateKey(handle);
        }

        return connection;
    }

private:
    std::string key_;
    int busyTimeoutMs_;

    // sqlite3_key() accepts a wrong key silently, so the real test is reading
    // the schema. Running it here makes a wrong key (or a plaintext file
    // behind a configured key) fail open() instead of the first query, and a
    // new file is initialized encrypted on first access like any other write.
    static void validateKey(sqlite3* handle)
    {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(handle, "SELECT count(*) FROM sqlite_master;", -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
        {
            rc = sqlite3_step(stmt);
        }

        sqlite3_finalize(stmt);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE)
        {
            throw std::runtime_error("invalid SQLCipher key: " + std::string(sqlite3_errmsg(handle)));
        }
    }
};

}  // namespace

SqlcipherDatabase::SqlcipherDatabase(std::string path, std::string key) : path_(std::move(path)), key_(std::move(key))
{
    initPool();
}

void SqlcipherDatabase::initPool()
{
    auto provider = std::make_shared<SqlcipherConnectionProvider>(oatpp::String(path_), key_, 5000);
    const v_int64 maxConn = (path_ == ":memory:") ? 1 : 10;
    pool_ = oatpp::sqlite::ConnectionPool::createShared(provider, maxConn, std::chrono::seconds(60),
                                                        std::chrono::seconds(5));

    executor_ = std::make_shared<oatpp::sqlite::Executor>(pool_);
    ormExecutor_ = std::make_shared<DatabaseExecutor>(executor_, *this);
}

SqlcipherDatabase::~SqlcipherDatabase()
{
    close();
}

void SqlcipherDatabase::open()
{
    if (isOpen_.load())
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

    if (!pool_ || !executor_)
    {
        initPool();
    }

    try
    {
        // Validate connectivity and key by acquiring and checking an initial connection.
        auto connection = executor_->getConnection();
        if (!connection)
        {
            throw errors::database::ConnectionFailed("Cannot open database '" + path_ + "'");
        }
    }
    catch (const errors::database::ConnectionFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::database::ConnectionFailed("Cannot open database '" + path_ + "': " + ormErrorDetail(ex));
    }

    isOpen_.store(true);
    if (!key_.empty())
    {
        logging::logInfo("database", "Encrypted file backend opened (key applied, not logged)");
    }
}

void SqlcipherDatabase::close() noexcept
{
    isOpen_.store(false);
    if (!pool_)
    {
        return;
    }

    try
    {
        pool_->stop();
    }
    catch (...)
    {
    }
}

bool SqlcipherDatabase::isOpen() const noexcept
{
    return isOpen_.load();
}

void SqlcipherDatabase::ensureOpen() const
{
    if (!isOpen_.load())
    {
        throw errors::database::ConnectionFailed("Database is not open: " + path_);
    }
}

std::vector<std::vector<std::string>> SqlcipherDatabase::execute(const std::string& sql,
                                                                 const std::vector<std::string>& params)
{
    ensureOpen();

    try
    {
        const OrmQuery query = toOrmQuery(sql);
        const auto parameters = toOrmParams(query, params);

        const auto statement =
            executor_->parseQueryTemplate(oatpp::String("query"), oatpp::String(query.text), {}, false);

        const auto result = executor_->execute(statement, parameters, nullptr, nullptr);
        if (!result->isSuccess())
        {
            throw errors::database::QueryFailed("SQL statement failed: " + queryResultError(result));
        }

        const auto rows = result->fetch<oatpp::Vector<oatpp::Vector<oatpp::String>>>(-1);
        if (!result->isSuccess())
        {
            throw errors::database::QueryFailed("SQL statement failed: " + queryResultError(result));
        }

        return toStdRows(rows);
    }
    catch (const errors::database::QueryFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::database::QueryFailed(std::string("SQL statement failed: ") + ormErrorDetail(ex));
    }
}

std::shared_ptr<oatpp::orm::Executor> SqlcipherDatabase::ormExecutor() const
{
    return ormExecutor_;
}

oatpp::provider::ResourceHandle<oatpp::orm::Connection> SqlcipherDatabase::ormConnection()
{
    ensureOpen();
    return executor_->getConnection();
}

std::shared_ptr<oatpp::orm::QueryResult> SqlcipherDatabase::executeOrm(
    const oatpp::data::share::StringTemplate& query, const std::unordered_map<oatpp::String, oatpp::Void>& params,
    const std::shared_ptr<const oatpp::data::mapping::TypeResolver>& typeResolver,
    const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    ensureOpen();

    try
    {
        const auto result = executor_->execute(query, params, typeResolver, connection);
        if (!result->isSuccess())
        {
            throw errors::database::QueryFailed("SQL statement failed: " + queryResultError(result));
        }

        return result;
    }
    catch (const errors::database::QueryFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::database::QueryFailed(std::string("SQL statement failed: ") + ormErrorDetail(ex));
    }
}

}  // namespace app::infrastructure::database
