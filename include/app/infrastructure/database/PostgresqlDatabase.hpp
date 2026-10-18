#pragma once

#ifndef APP_WITH_POSTGRESQL
#error "PostgresqlDatabase.hpp requires APP_WITH_POSTGRESQL (configure with -DAPP_ENABLE_POSTGRESQL=ON)"
#endif  // !APP_WITH_POSTGRESQL

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "oatpp/core/provider/Provider.hpp"
#include "oatpp/orm/Connection.hpp"

#include "app/infrastructure/database/IDatabase.hpp"

namespace oatpp::postgresql
{
class Connection;
class Executor;
}  // namespace oatpp::postgresql

namespace app::infrastructure::database
{

class DatabaseExecutor;

/**
 * PostgreSQL backend over the oatpp-postgresql adapter (libpq underneath):
 * its ConnectionProvider connects (scram-sha-256 via the connection values
 * below) and its Executor prepares, binds and executes every statement,
 * while IDatabase keeps repositories and inspectors backend-agnostic.
 *
 * Unlike the file backend there is no ConnectionProvider subclass: every
 * connection parameter travels in the conninfo string (host/port/dbname/
 * user/password/connect_timeout) and a failed connect throws before a handle
 * exists, so no per-connection state has to be injected.
 *
 * SQL uses `?` placeholders exactly like the SQLCipher-backed file backend;
 * they are translated to the adapter's named parameters before execution
 * (see OrmSupport.hpp for the rules: quoted text and comments are never
 * rewritten, and `:` is rejected outside single-quoted literals — write
 * cast(x as type), never `x::type`). Never put a literal `?` in SQL passed
 * here.
 *
 * The adapter's result mapper reads text-compatible column types only
 * (TEXT/VARCHAR/CHAR/BPCHAR), so anything else must be selected with a cast:
 * `SELECT cast(1 as text)`, `SELECT cast(datname as text)`, ... All result
 * values come back as text (NULL arrives as "", matching the file backend).
 * Repositories are not limited by that: they run typed `QUERY` templates
 * through ormExecutor()/executeOrm() (see DatabaseExecutor), where the
 * adapter maps text and integer columns to the DTO field types.
 * Connections are blocking with a bounded connect timeout and guarded by a
 * mutex, mirroring SqlcipherDatabase. Key (password) material is held only in
 * memory, never logged, and has no accessor by design.
 *
 * Unified naming: `path` is the Unix socket directory (e.g.
 * "data/postgresql", passed to libpq as host=) or a TCP host when the
 * server listens on TCP; `key` is the scram-sha-256 role password.
 */
class PostgresqlDatabase : public IDatabase
{
public:
    PostgresqlDatabase(std::string path, std::uint16_t port, std::string dbname, std::string user, std::string key,
                       int connectTimeoutSec = 5);
    ~PostgresqlDatabase() override;

    PostgresqlDatabase(const PostgresqlDatabase&) = delete;
    PostgresqlDatabase& operator=(const PostgresqlDatabase&) = delete;

    void open() override;
    void close() noexcept override;
    bool isOpen() const noexcept override;

    std::vector<std::vector<std::string>> execute(const std::string& sql,
                                                  const std::vector<std::string>& params = {}) override;

    std::shared_ptr<oatpp::orm::Executor> ormExecutor() const override;

    std::shared_ptr<oatpp::orm::QueryResult> executeOrm(
        const oatpp::data::share::StringTemplate& query, const std::unordered_map<oatpp::String, oatpp::Void>& params,
        const std::shared_ptr<const oatpp::data::mapping::TypeResolver>& typeResolver = nullptr,
        const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection = nullptr) override;

    oatpp::provider::ResourceHandle<oatpp::orm::Connection> ormConnection() override;

    std::string backendName() const noexcept override
    {
        return "POSTGRESQL";
    }

    const std::string& path() const noexcept
    {
        return path_;
    }

    const std::string& host() const noexcept
    {
        return path_;
    }

    std::uint16_t port() const noexcept
    {
        return port_;
    }

    const std::string& dbname() const noexcept
    {
        return dbname_;
    }

    const std::string& user() const noexcept
    {
        return user_;
    }

private:
    void ensureOpen() const;
    void initPool();

    std::string path_;
    std::uint16_t port_;
    std::string dbname_;
    std::string user_;
    std::string key_;
    int connectTimeoutSec_;
    std::atomic<bool> isOpen_{false};
    std::shared_ptr<oatpp::provider::Provider<oatpp::postgresql::Connection>> pool_;
    std::shared_ptr<oatpp::postgresql::Executor> executor_;
    std::shared_ptr<DatabaseExecutor> ormExecutor_;
};

}  // namespace app::infrastructure::database
