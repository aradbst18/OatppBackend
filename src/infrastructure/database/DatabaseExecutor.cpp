#include "app/infrastructure/database/DatabaseExecutor.hpp"

#include <iostream>

#include "app/infrastructure/database/IDatabase.hpp"

namespace app::infrastructure::database
{

namespace
{
thread_local oatpp::provider::ResourceHandle<oatpp::orm::Connection> t_activeTxConnection;
}

DatabaseExecutor::DatabaseExecutor(const std::shared_ptr<oatpp::orm::Executor>& backendExecutor, IDatabase& database)
    : backend_(backendExecutor),
      database_(database),
      beginTemplate_(backendExecutor->parseQueryTemplate("orm-begin", "BEGIN", {}, false)),
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
    const auto& conn = connection ? connection : t_activeTxConnection;
    return database_.executeOrm(queryTemplate, params, typeResolver, conn);
}

std::shared_ptr<oatpp::orm::QueryResult> DatabaseExecutor::begin(
    const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    auto conn = connection ? connection : getConnection();
    t_activeTxConnection = conn;

    try
    {
        return database_.executeOrm(beginTemplate_, {}, nullptr, conn);
    }
    catch (...)
    {
        t_activeTxConnection = nullptr;
        throw;
    }
}

std::shared_ptr<oatpp::orm::QueryResult> DatabaseExecutor::commit(
    const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    const auto conn = connection ? connection : t_activeTxConnection;

    try
    {
        auto res = database_.executeOrm(commitTemplate_, {}, nullptr, conn);
        t_activeTxConnection = nullptr;
        return res;
    }
    catch (...)
    {
        t_activeTxConnection = nullptr;
        throw;
    }
}

std::shared_ptr<oatpp::orm::QueryResult> DatabaseExecutor::rollback(
    const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    const auto conn = connection ? connection : t_activeTxConnection;

    try
    {
        auto res = database_.executeOrm(rollbackTemplate_, {}, nullptr, conn);
        t_activeTxConnection = nullptr;
        return res;
    }
    catch (...)
    {
        t_activeTxConnection = nullptr;
        throw;
    }
}

v_int64 DatabaseExecutor::getSchemaVersion(const oatpp::String& suffix,
                                           const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    return backend_->getSchemaVersion(suffix, connection ? connection : database_.ormConnection());
}

void DatabaseExecutor::migrateSchema(const oatpp::String& script, v_int64 newVersion, const oatpp::String& suffix,
                                     const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection)
{
    backend_->migrateSchema(script, newVersion, suffix, connection ? connection : database_.ormConnection());
}

}  // namespace app::infrastructure::database
