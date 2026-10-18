#pragma once

#include <memory>
#include <string>

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/orm/DbClient.hpp"

#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/IDatabase.hpp"
#include "app/infrastructure/database/OrmSupport.hpp"

#include OATPP_CODEGEN_BEGIN(DbClient)

namespace app::repositories
{

/**
 * Base class of every repository: an `oatpp::orm::DbClient` whose statements
 * are declared with the `QUERY` macro (named `:param` templates, typed DTO
 * results) — the repository model documented in docs/guides/adding-repository.md.
 *
 * The executor comes from `IDatabase::ormExecutor()` (a DatabaseExecutor
 * around the active backend), so every statement a repository runs takes the
 * database mutex and reuses its open handle: repositories never see a
 * connection and stay backend-agnostic. Execute-stage failures already throw
 * `database::QueryFailed` from there; `fetch<Wrapper>()` in this class covers
 * the fetch stage and reports failures the same way.
 *
 * This class owns the database handle and the table name. A child repository:
 * - passes its table name to this constructor and builds its SQL from
 *   `tableName()` in its own `QUERY` templates;
 * - defines its row DTO (fields map to columns by name, see UserDto);
 * - validates its arguments and translates outcomes into the domain errors of
 *   `app::errors::database` (RecordNotFound, Conflict, ...).
 */
class BaseRepository : public oatpp::orm::DbClient
{
public:
    virtual ~BaseRepository() = default;

protected:
    BaseRepository(std::shared_ptr<infrastructure::database::IDatabase> db, std::string tableName);

    /// Table this repository owns; `QUERY` templates build their SQL from it.
    const std::string& tableName() const noexcept;

    /// The database handle (backend name, open connection, ...).
    const std::shared_ptr<infrastructure::database::IDatabase>& database() const noexcept;

    /// True when the table exists in the active backend's catalog.
    bool tableExists();

    /// Typed rows of a statement that executed successfully. Covers the fetch
    /// stage itself (SQLite step errors, adapter/mapper type mismatches on
    /// either backend) and turns failures into database::QueryFailed.
    template <typename Wrapper>
    Wrapper fetch(const std::shared_ptr<oatpp::orm::QueryResult>& result) const
    {
        try
        {
            auto rows = result->fetch<Wrapper>();
            if (!result->isSuccess())
            {
                throw errors::database::QueryFailed("SQL statement failed: " +
                                                    infrastructure::database::queryResultError(result));
            }

            return rows;
        }
        catch (const errors::database::QueryFailed&)
        {
            throw;
        }
        catch (const std::exception& ex)
        {
            throw errors::database::QueryFailed("SQL statement failed: " + std::string(ex.what()));
        }
    }

private:
    // Catalog lookups are the repository layer's one backend-specific SQL;
    // tableExists() picks the right one from IDatabase::backendName().
    QUERY(sqlcipherTableExistsImp, "SELECT count(*) AS count FROM sqlite_master WHERE type = 'table' AND name = :name;",
          PARAM(oatpp::String, name))

    QUERY(postgresqlTableExistsImp, "SELECT count(*) AS count FROM information_schema.tables WHERE table_name = :name;",
          PARAM(oatpp::String, name))

    std::shared_ptr<infrastructure::database::IDatabase> db_;
    std::string tableName_;
};

}  // namespace app::repositories

#include OATPP_CODEGEN_END(DbClient)
