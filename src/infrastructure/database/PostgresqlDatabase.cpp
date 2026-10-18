#include "app/infrastructure/database/PostgresqlDatabase.hpp"

#ifndef APP_WITH_POSTGRESQL
#error "PostgresqlDatabase.cpp requires APP_WITH_POSTGRESQL (configure with -DAPP_ENABLE_POSTGRESQL=ON)"
#endif  // !APP_WITH_POSTGRESQL

#include <filesystem>
#include <utility>

#include "oatpp-postgresql/Connection.hpp"
#include "oatpp-postgresql/ConnectionProvider.hpp"
#include "oatpp-postgresql/Executor.hpp"

#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/DatabaseExecutor.hpp"
#include "app/infrastructure/database/OrmSupport.hpp"

namespace app::infrastructure::database
{

namespace
{

// libpq conninfo quoting: backslash and single-quote escaped, always wrapped.
// The string is handed to the adapter's ConnectionProvider (which runs
// PQconnectdb underneath) and never logged or thrown (only libpq's own error
// text leaves this unit), so password material stays in memory.
std::string quoteConninfo(const std::string& value)
{
    std::string out = "'";
    for (char c : value)
    {
        if (c == '\\' || c == '\'')
        {
            out += '\\';
        }

        out += c;
    }

    out += "'";
    return out;
}

}  // namespace

PostgresqlDatabase::PostgresqlDatabase(std::string path, std::uint16_t port, std::string dbname, std::string user,
                                       std::string key, int connectTimeoutSec)
    : path_(std::move(path)),
      port_(port),
      dbname_(std::move(dbname)),
      user_(std::move(user)),
      key_(std::move(key)),
      connectTimeoutSec_(connectTimeoutSec)
{
    // libpq treats host= as a socket directory only when it is absolute
    // (starts with '/'); relative config values like "data/postgresql" are
    // resolved here so socket connections work regardless of CWD. TCP
    // hosts (no slash) pass through untouched.
    if (path_.find('/') != std::string::npos)
    {
        std::error_code ec;
        const std::filesystem::path abs = std::filesystem::absolute(path_, ec);
        if (!ec)
        {
            path_ = abs.string();
        }
    }

    initPool();
}

void PostgresqlDatabase::initPool()
{
    const std::string conninfo = "host=" + quoteConninfo(path_) + " port=" + quoteConninfo(std::to_string(port_)) +
                                 " dbname=" + quoteConninfo(dbname_) + " user=" + quoteConninfo(user_) +
                                 " password=" + quoteConninfo(key_) +
                                 " connect_timeout=" + quoteConninfo(std::to_string(connectTimeoutSec_));

    auto provider = std::make_shared<oatpp::postgresql::ConnectionProvider>(oatpp::String(conninfo));
    pool_ = oatpp::postgresql::ConnectionPool::createShared(provider, 10, std::chrono::seconds(60),
                                                            std::chrono::seconds(5));

    executor_ = std::make_shared<oatpp::postgresql::Executor>(pool_);
    ormExecutor_ = std::make_shared<DatabaseExecutor>(executor_, *this);
}

PostgresqlDatabase::~PostgresqlDatabase()
{
    close();
}

void PostgresqlDatabase::open()
{
    if (isOpen_.load())
    {
        return;
    }

    if (!pool_ || !executor_)
    {
        initPool();
    }

    try
    {
        auto connection = executor_->getConnection();
        if (!connection)
        {
            throw errors::database::ConnectionFailed("Cannot connect to PostgreSQL '" + dbname_ + "' at " + path_);
        }
    }
    catch (const errors::database::ConnectionFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::database::ConnectionFailed("Cannot connect to PostgreSQL '" + dbname_ + "' at " + path_ + ": " +
                                                 ormErrorDetail(ex));
    }

    isOpen_.store(true);
}

void PostgresqlDatabase::close() noexcept
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

bool PostgresqlDatabase::isOpen() const noexcept
{
    return isOpen_.load();
}

void PostgresqlDatabase::ensureOpen() const
{
    if (!isOpen_.load())
    {
        throw errors::database::ConnectionFailed("PostgreSQL database is not open: '" + dbname_ + "' at " + path_);
    }
}

std::vector<std::vector<std::string>> PostgresqlDatabase::execute(const std::string& sql,
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
            throw errors::database::QueryFailed("PostgreSQL query failed on '" + dbname_ +
                                                "': " + queryResultError(result));
        }

        const auto rows = result->fetch<oatpp::Vector<oatpp::Vector<oatpp::String>>>(-1);
        if (!result->isSuccess())
        {
            throw errors::database::QueryFailed("PostgreSQL query failed on '" + dbname_ +
                                                "': " + queryResultError(result));
        }

        return toStdRows(rows);
    }
    catch (const errors::database::QueryFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::database::QueryFailed("PostgreSQL query failed on '" + dbname_ + "': " + ormErrorDetail(ex));
    }
}

std::shared_ptr<oatpp::orm::Executor> PostgresqlDatabase::ormExecutor() const
{
    return ormExecutor_;
}

oatpp::provider::ResourceHandle<oatpp::orm::Connection> PostgresqlDatabase::ormConnection()
{
    ensureOpen();
    return executor_->getConnection();
}

std::shared_ptr<oatpp::orm::QueryResult> PostgresqlDatabase::executeOrm(
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
            throw errors::database::QueryFailed("PostgreSQL query failed on '" + dbname_ +
                                                "': " + queryResultError(result));
        }

        return result;
    }
    catch (const errors::database::QueryFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::database::QueryFailed("PostgreSQL query failed on '" + dbname_ + "': " + ormErrorDetail(ex));
    }
}

}  // namespace app::infrastructure::database
