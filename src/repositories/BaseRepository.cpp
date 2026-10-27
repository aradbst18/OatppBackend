#include "app/repositories/BaseRepository.hpp"

#include <utility>

namespace app::repositories
{

namespace
{

// Runs in the DbClient base-class initializer, before db_ is assigned: a
// repository without a database cannot work and must fail at construction.
std::shared_ptr<oatpp::orm::Executor> executorOf(const std::shared_ptr<infrastructure::database::IDatabase>& db)
{
    if (!db)
    {
        throw errors::database::ConnectionFailed("BaseRepository requires a database");
    }

    return db->ormExecutor();
}

}  // namespace

BaseRepository::BaseRepository(std::shared_ptr<infrastructure::database::IDatabase> db, std::string tableName)
    : oatpp::orm::DbClient(executorOf(db)), db_(std::move(db)), tableName_(std::move(tableName))
{
}

const std::string& BaseRepository::tableName() const noexcept
{
    return tableName_;
}

bool BaseRepository::sharesDatabaseWith(const BaseRepository& other) const noexcept
{
    return db_ == other.db_;
}

const std::shared_ptr<infrastructure::database::IDatabase>& BaseRepository::database() const noexcept
{
    return db_;
}

bool BaseRepository::tableExists()
{
    const auto result = database()->backendName() == "POSTGRESQL" ? postgresqlTableExistsImp(oatpp::String(tableName_))
                                                                  : sqlcipherTableExistsImp(oatpp::String(tableName_));

    const auto rows = fetch<oatpp::Vector<oatpp::Fields<oatpp::Int64>>>(result);
    return !rows->empty() && rows->front()["count"].getValue(0) > 0;
}

bool BaseRepository::columnExists(const std::string& column)
{
    const auto result = database()->backendName() == "POSTGRESQL" ? postgresqlColumnsImp(oatpp::String(tableName_))
                                                                  : sqlcipherColumnsImp(oatpp::String(tableName_));
    const auto rows = fetch<oatpp::Vector<oatpp::Fields<oatpp::String>>>(result);
    for (const auto& row : *rows)
    {
        const auto name = row["name"];
        if (name && std::string(*name) == column)
        {
            return true;
        }
    }
    return false;
}

}  // namespace app::repositories
