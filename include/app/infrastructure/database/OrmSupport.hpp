#pragma once

#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "oatpp/core/Types.hpp"
#include "oatpp/orm/QueryResult.hpp"

namespace app::infrastructure::database
{

/**
 * Shared helpers for the two oatpp ORM backed databases (SqlcipherDatabase over
 * oatpp-sqlite, PostgresqlDatabase over oatpp-postgresql). Both adapters were
 * built for the ORM (named `:param` query templates, typed DTO results),
 * while the application keeps a plain `?`-placeholder, text-row IDatabase
 * contract; these helpers bridge the two so repositories and inspectors stay
 * backend-agnostic.
 */

/**
 * SQL rewritten for the oatpp ORM executors: every `?` placeholder replaced
 * by a named template parameter (`:p0`, `:p1`, ...). `paramNames` holds the
 * generated names in binding order so callers can fill the executor's
 * parameter map positionally (`"p0"` -> first value, ...).
 */
struct OrmQuery
{
    std::string text;
    std::vector<std::string> paramNames;
};

/**
 * Translate `?`-placeholder SQL into an oatpp ORM query template. Both
 * adapters parse named `:name` template parameters, never `?`, so the
 * translation happens here for the file and the PostgreSQL backend alike.
 *
 * `?` inside single-quoted literals, double-quoted identifiers and comments
 * is left untouched (mirroring the adapters, which skip quoted strings when
 * scanning for template parameters). A `:` outside single-quoted literals is
 * rejected with a clear error: the adapters reserve `:` for named
 * parameters, so a PostgreSQL `::` cast (or a colon in a comment) would
 * otherwise corrupt the statement; use cast(x as type) instead of `::`.
 *
 * Throws app::errors::database::QueryFailed when a stray `:` is found.
 */
OrmQuery toOrmQuery(const std::string& sql);

/**
 * Build the executor's parameter map ("p0" -> first value, ...) and enforce
 * that the statement and the caller agree on the parameter count: binding
 * fewer values would leave template parameters missing (the adapters throw
 * on that anyway) and binding more would silently drop them.
 *
 * Throws app::errors::database::QueryFailed on a count mismatch.
 */
std::unordered_map<oatpp::String, oatpp::Void> toOrmParams(const OrmQuery& query,
                                                           const std::vector<std::string>& params);

/**
 * Error text of a finished query. Falls back to a generic message when the
 * adapter has no message (a mid-statement failure can leave its captured
 * message empty).
 */
std::string queryResultError(const std::shared_ptr<oatpp::orm::QueryResult>& result);

/**
 * Convert fetched rows to the IDatabase text-row shape. NULL cells arrive as
 * null oatpp::String and become "" (matching the pre-adapter behavior).
 */
std::vector<std::vector<std::string>> toStdRows(oatpp::Vector<oatpp::Vector<oatpp::String>> rows);

/**
 * Strip the adapters' exception prefix ("[...]: Error. Can't connect. ") so
 * operator-facing messages read like the rest of the application. Falls back
 * to the full message when the prefix is absent.
 */
std::string ormErrorDetail(const std::exception& ex);

}  // namespace app::infrastructure::database
