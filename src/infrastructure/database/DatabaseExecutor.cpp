#include "app/infrastructure/database/DatabaseExecutor.hpp"

#include <unordered_map>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/IDatabase.hpp"

namespace app::infrastructure::database
{

namespace
{
using ConnectionHandle = oatpp::provider::ResourceHandle<oatpp::orm::Connection>;
thread_local std::unordered_map<const DatabaseExecutor*, ConnectionHandle> t_transactions;
}  // namespace

DatabaseExecutor::DatabaseExecutor(const std::shared_ptr<oatpp::orm::Executor>& backendExecutor, IDatabase& database)
    : backend_(backendExecutor),
      database_(database),
      beginTemplate_(backendExecutor->parseQueryTemplate(
          "orm-begin", database.backendName() == "SQLCIPHER" ? "BEGIN IMMEDIATE" : "BEGIN", {}, false)),
      commitTemplate_(backendExecutor->parseQueryTemplate("orm-commit", "COMMIT", {}, false)),
      rollbackTemplate_(backendExecutor->parseQueryTemplate("orm-rollback", "ROLLBACK", {}, false))
{
}

std::shared_ptr<oatpp::data::mapping::TypeResolver> DatabaseExecutor::createTypeResolver()
{
    return backend_->createTypeResolver();
}

oatpp::provider::ResourceHandle<oatpp::orm::Connection> DatabaseExecutor::getConnection()
{
    const auto active = t_transactions.find(this);
    if (active != t_transactions.end())
    {
        return active->second;
    }
    return database_.ormConnection();
}

oatpp::data::share::StringTemplate DatabaseExecutor::parseQueryTemplate(const oatpp::String& name,
                                                                        const oatpp::String& text,
                                                                        const ParamsTypeMap& paramsTypeMap,
                                                                        bool prepare)
{
    return backend_->parseQueryTemplate(name, text, paramsTypeMap, prepare);
}

std::shared_ptr<oatpp::orm::QueryResult> DatabaseExecutor::execute(
    const StringTemplate& queryTemplate, const std::unordered_map<oatpp::String, oatpp::Void>& params,
    const std::shared_ptr<const oatpp::data::mapping::TypeResolver>& typeResolver,
    const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    const auto active = t_transactions.find(this);
    const auto conn = connection ? connection : (active != t_transactions.end() ? active->second : ConnectionHandle{});
    return database_.executeOrm(queryTemplate, params, typeResolver, conn);
}

std::shared_ptr<oatpp::orm::QueryResult> DatabaseExecutor::begin(
    const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    if (t_transactions.count(this) != 0)
    {
        throw errors::database::TransactionFailed("Nested transactions are not supported");
    }
    auto conn = connection ? connection : getConnection();
    auto result = database_.executeOrm(beginTemplate_, {}, nullptr, conn);
    t_transactions.emplace(this, conn);
    return result;
}

std::shared_ptr<oatpp::orm::QueryResult> DatabaseExecutor::commit(
    const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    const auto active = t_transactions.find(this);
    if (active == t_transactions.end() || (connection && connection.object != active->second.object))
    {
        throw errors::database::TransactionFailed("Transaction does not belong to this executor");
    }
    // Retain context on failure so Transaction's destructor can roll back.
    auto result = database_.executeOrm(commitTemplate_, {}, nullptr, active->second);
    t_transactions.erase(active);
    return result;
}

std::shared_ptr<oatpp::orm::QueryResult> DatabaseExecutor::rollback(
    const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    const auto active = t_transactions.find(this);
    if (active == t_transactions.end() || (connection && connection.object != active->second.object))
    {
        throw errors::database::TransactionFailed("Transaction does not belong to this executor");
    }

    try
    {
        auto res = database_.executeOrm(rollbackTemplate_, {}, nullptr, active->second);
        t_transactions.erase(active);
        return res;
    }
    catch (...)
    {
        t_transactions.erase(active);
        throw;
    }
}

v_int64 DatabaseExecutor::getSchemaVersion(const oatpp::String& suffix,
                                           const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    return backend_->getSchemaVersion(suffix, connection ? connection : getConnection());
}

void DatabaseExecutor::migrateSchema(const oatpp::String& script, v_int64 newVersion, const oatpp::String& suffix,
                                     const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    backend_->migrateSchema(script, newVersion, suffix, connection ? connection : getConnection());
}

}  // namespace app::infrastructure::database
