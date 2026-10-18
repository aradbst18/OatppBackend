#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "oatpp/core/Types.hpp"
#include "oatpp/core/data/mapping/TypeResolver.hpp"
#include "oatpp/core/data/share/StringTemplate.hpp"
#include "oatpp/core/provider/Provider.hpp"
#include "oatpp/orm/Connection.hpp"
#include "oatpp/orm/Executor.hpp"
#include "oatpp/orm/QueryResult.hpp"

#include "app/config/AppSettings.hpp"

namespace app::infrastructure::database
{

/**
 * Minimal database abstraction. The application talks only to this interface;
 * the concrete backend (SQLCipher-backed file database over oatpp-sqlite, or
 * PostgreSQL over oatpp-postgresql) is a compile-time selection (see
 * DatabaseFactory).
 *
 * Two execution contracts share a connection pool:
 * - execute(): ad-hoc SQL with bound `?` placeholders and text-row results
 *   (inspectors, tests, one-off statements). SQL is always executed with bound
 *   parameters, so there is no string-interpolated SQL anywhere in the
 *   codebase;
 * - the repository (ORM) seam below: the active adapter's `QUERY`-templated
 *   statements with named parameters and typed DTO results, run via
 *   connection pooling through DatabaseExecutor so concurrent requests execute
 *   efficiently without mutex contention.
 */
class IDatabase
{
public:
    virtual ~IDatabase() = default;

    virtual void open() = 0;
    virtual void close() noexcept = 0;
    virtual bool isOpen() const noexcept = 0;

    /// Execute a statement with positional bound parameters (? placeholders
    /// only; a ':' outside single-quoted literals is rejected). Returns rows
    /// for SELECT (each row is a vector of text values), empty for DDL/DML.
    /// Throws app::errors::database::{QueryFailed,ConnectionFailed} on failure.
    virtual std::vector<std::vector<std::string>> execute(const std::string& sql,
                                                          const std::vector<std::string>& params = {}) = 0;

    virtual void executeVoid(const std::string& sql, const std::vector<std::string>& params = {})
    {
        execute(sql, params);
    }

    virtual std::string backendName() const noexcept = 0;

    /// Executor the repository layer runs its `QUERY` templates through: the
    /// active backend adapter's executor wrapped by DatabaseExecutor.
    /// Repositories receive it through BaseRepository and never hold a connection
    /// themselves.
    virtual std::shared_ptr<oatpp::orm::Executor> ormExecutor() const = 0;

    /// Execute one parsed ORM template. This is the repository seam DatabaseExecutor
    /// calls; application code keeps using execute()/executeVoid(). `connection` overrides
    /// the pooled handle (a transaction passes its own); null borrows one from the pool.
    /// Throws app::errors::database::{QueryFailed,ConnectionFailed} on failure.
    virtual std::shared_ptr<oatpp::orm::QueryResult> executeOrm(
        const oatpp::data::share::StringTemplate& query, const std::unordered_map<oatpp::String, oatpp::Void>& params,
        const std::shared_ptr<const oatpp::data::mapping::TypeResolver>& typeResolver = nullptr,
        const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection = nullptr) = 0;

    /// Acquire a connection handle from the pool (repository seam; DatabaseExecutor reports it
    /// as its connection and the startup-time schema helpers run on it). Throws
    /// ConnectionFailed when the database is closed.
    virtual oatpp::provider::ResourceHandle<oatpp::orm::Connection> ormConnection() = 0;
};

}  // namespace app::infrastructure::database
