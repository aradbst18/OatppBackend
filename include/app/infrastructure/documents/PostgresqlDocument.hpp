#pragma once

#ifndef APP_WITH_POSTGRESQL
#error "PostgresqlDocument.hpp requires APP_WITH_POSTGRESQL (configure with -DAPP_ENABLE_POSTGRESQL=ON)"
#endif  // !APP_WITH_POSTGRESQL

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/infrastructure/database/IDatabase.hpp"
#include "app/infrastructure/documents/DocumentValue.hpp"

namespace app::infrastructure::documents
{

/**
 * Generic PostgreSQL inspector: the server counterpart of SqlcipherDocument
 * (which covers files). Converts schema + content into the
 * neutral DocumentValue tree, independent of application tables, so any
 * reachable server database can be snapshotted without touching raw SQL or
 * connection handling.
 *
 * Read-only by design (only SELECTs against information_schema plus the
 * dumped tables). Table names are always whitelisted against
 * information_schema before use, so identifier interpolation is safe.
 * Keys (passwords) never appear in outputs: dumpPostgresqlDatabase() records
 * a key-free DSN, mirroring the never-log-keys rule.
 *
 * Naming follows database.path (socket dir or TCP host) and database.key
 * (scram-sha-256 password).
 *
 * PostgreSQL exposes no single-statement table DDL (that is pg_dump
 * territory), so postgresqlTableSchema() returns "" by design; tables,
 * columns and rows are complete. NULL cells arrive as "", matching the
 * SQLCipher inspector. Error mapping mirrors SqlcipherDocument:
 * errors::database::ConnectionFailed on connect, errors::database::QueryFailed
 * otherwise, always with the target embedded.
 */
std::shared_ptr<infrastructure::database::IDatabase> openPostgresqlDatabase(const std::string& path, std::uint16_t port,
                                                                            const std::string& dbname,
                                                                            const std::string& user,
                                                                            const std::string& key);

/**
 * User table names in information_schema order (base tables in the public
 * schema; PostgreSQL catalogs excluded).
 */
std::vector<std::string> listPostgresqlTables(const std::string& path, std::uint16_t port, const std::string& dbname,
                                              const std::string& user, const std::string& key);

/**
 * Always "" (see above): kept for API symmetry with sqlcipherTableSchema().
 * Throws errors::database::QueryFailed for unknown tables.
 */
std::string postgresqlTableSchema(const std::string& path, std::uint16_t port, const std::string& dbname,
                                  const std::string& table, const std::string& user, const std::string& key);

/**
 * One table as a DocumentValue Map: {"schema": "" (see above), "columns":
 * List of String, "rows": List of Maps (column name -> String cell)}.
 */
DocumentValue dumpPostgresqlTable(const std::string& path, std::uint16_t port, const std::string& dbname,
                                  const std::string& table, const std::string& user, const std::string& key);

/**
 * Whole database as a DocumentValue Map: {"backend": "POSTGRESQL", "dsn":
 * key-free String, "tables": Map of table name -> dumpPostgresqlTable()}.
 */
DocumentValue dumpPostgresqlDatabase(const std::string& path, std::uint16_t port, const std::string& dbname,
                                     const std::string& user, const std::string& key);

}  // namespace app::infrastructure::documents
