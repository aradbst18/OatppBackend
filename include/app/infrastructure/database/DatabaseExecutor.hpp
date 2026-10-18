#pragma once

#include <memory>

#include "oatpp/orm/Executor.hpp"

namespace app::infrastructure::database
{

class IDatabase;

/**
 * ORM executor for the repository layer (see docs/guides/adding-repository.md).
 *
 * Both oatpp adapters were built for the ORM: `oatpp::orm::DbClient`
 * subclasses declare statements with the `QUERY` macro (named `:param`
 * templates, typed DTO results) and run them through an `oatpp::orm::Executor`.
 * This class is that executor, adapting one mutex-guarded IDatabase:
 *
 * - template parsing and type/result mapping stay in the active backend
 *   adapter's executor, so `:param` syntax and row mapping are exactly the
 *   adapter's (SQLite or PostgreSQL) — nothing is re-implemented here;
 * - every statement executes through IDatabase::executeOrm(): database mutex
 *   held, on the open handle. Repositories never pass a connection, so two
 *   threads can never run statements on the same handle concurrently;
 * - transaction control (begin/commit/rollback) is BEGIN/COMMIT/ROLLBACK run
 *   through that same path, so `oatpp::orm::Transaction` works unchanged;
 * - schema-version helpers delegate to the backend adapter with the open
 *   connection (startup-time schema tooling; this application does not use
 *   SchemaMigration).
 *
 * Lifetime: created and owned by the database it adapts and only valid while
 * that database lives. The reference (not a shared_ptr) avoids an ownership
 * cycle; repositories keep their database alive, so the executor cannot
 * outlive its target while in use.
 */
class DatabaseExecutor final : public oatpp::orm::Executor
{
public:
    DatabaseExecutor(const std::shared_ptr<oatpp::orm::Executor>& backendExecutor, IDatabase& database);

    /// Type resolver of the active backend adapter.
    std::shared_ptr<oatpp::data::mapping::TypeResolver> createTypeResolver() override;

    /// The open handle of the adapted database (throws ConnectionFailed when it
    /// is closed).
    oatpp::provider::ResourceHandle<oatpp::orm::Connection> getConnection() override;

    /// Template parsing of the active backend adapter.
    oatpp::data::share::StringTemplate parseQueryTemplate(const oatpp::String& name, const oatpp::String& text,
                                                          const ParamsTypeMap& paramsTypeMap,
                                                          bool prepare = false) override;

    /// Runs the statement through IDatabase::executeOrm() (locked, on the open
    /// handle unless `connection` overrides it).
    std::shared_ptr<oatpp::orm::QueryResult> execute(
        const StringTemplate& queryTemplate, const std::unordered_map<oatpp::String, oatpp::Void>& params,
        const std::shared_ptr<const oatpp::data::mapping::TypeResolver>& typeResolver = nullptr,
        const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection = nullptr) override;

    /// BEGIN on the open handle.
    std::shared_ptr<oatpp::orm::QueryResult> begin(
        const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection = nullptr) override;

    /// COMMIT on the open handle.
    std::shared_ptr<oatpp::orm::QueryResult> commit(
        const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection) override;

    /// ROLLBACK on the open handle.
    std::shared_ptr<oatpp::orm::QueryResult> rollback(
        const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection) override;

    /// Delegates to the backend adapter on the open connection (startup-time).
    v_int64 getSchemaVersion(
        const oatpp::String& suffix = nullptr,
        const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection = nullptr) override;

    /// Delegates to the backend adapter on the open connection (startup-time).
    void migrateSchema(const oatpp::String& script, v_int64 newVersion, const oatpp::String& suffix = nullptr,
                       const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection = nullptr) override;

private:
    std::shared_ptr<oatpp::orm::Executor> backend_;
    IDatabase& database_;
    // Transaction control as ordinary statements so it runs on the same
    // locked path as everything else.
    oatpp::data::share::StringTemplate beginTemplate_;
    oatpp::data::share::StringTemplate commitTemplate_;
    oatpp::data::share::StringTemplate rollbackTemplate_;
};

}  // namespace app::infrastructure::database
