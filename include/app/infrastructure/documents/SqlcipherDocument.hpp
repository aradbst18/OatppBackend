#pragma once

#ifndef APP_WITH_SQLCIPHER
#error "SqlcipherDocument.hpp requires APP_WITH_SQLCIPHER (configure with -DAPP_ENABLE_SQLCIPHER=ON)"
#endif  // !APP_WITH_SQLCIPHER

#include <memory>
#include <string>
#include <vector>

#include "app/infrastructure/database/IDatabase.hpp"
#include "app/infrastructure/documents/DocumentValue.hpp"

namespace app::infrastructure::documents
{

/**
 * Generic file-database inspector (SQLCipher library). Opens any file
 * database directly (independent of the user database selection) and
 * converts schema + content into the neutral DocumentValue tree, independent
 * of application tables. This is the database analogue of
 * JsonDocument/TomlDocument/XmlDocument/YamlDocument: future code can read
 * arbitrary database files (system files, other apps' files) without
 * touching SQL or the database API directly. For PostgreSQL servers see
 * PostgresqlDocument.
 *
 * Only declared with file database support (APP_ENABLE_SQLCIPHER); including
 * this header without it is a compile error by design, like a disabled
 * parser's store class.
 *
 * Read-only by design: no writes, no DDL, no PRAGMA except key application
 * and introspection queries. Table names are always whitelisted against
 * sqlite_master before use, so identifier interpolation is safe.
 *
 * The key is optional (empty means plaintext): the backend is always the
 * SQLCipher library, which opens unencrypted files exactly like standard
 * SQLite.
 * Error mapping: open/key failures surface the backend's
 * errors::database::ConnectionFailed; unknown tables and bad SQL surface
 * errors::database::QueryFailed with the path embedded.
 */
std::shared_ptr<infrastructure::database::IDatabase> openSqlcipherDatabase(const std::string& path,
                                                                           const std::string& key = "");

/**
 * User table names in sqlite_master order, excluding sqlite_% internals.
 */
std::vector<std::string> listSqlcipherTables(const std::string& path, const std::string& key = "");

/**
 * The CREATE statement for one table (its `sql` in sqlite_master).
 * Throws errors::database::QueryFailed when the table does not exist.
 */
std::string sqlcipherTableSchema(const std::string& path, const std::string& table, const std::string& key = "");

/**
 * One table as a DocumentValue Map: {"schema": String, "columns": List of String,
 * "rows": List of Maps (column name -> String cell)}. Throws
 * errors::database::QueryFailed for unknown tables.
 */
DocumentValue dumpSqlcipherTable(const std::string& path, const std::string& table, const std::string& key = "");

/**
 * Whole database as a DocumentValue Map: {"backend": String, "path": String,
 * "tables": Map of table name -> dumpSqlcipherTable()}. Feed it to
 * serializeJson() (or any DocumentValue serializer) to snapshot a database file.
 */
DocumentValue dumpSqlcipherDatabase(const std::string& path, const std::string& key = "");

}  // namespace app::infrastructure::documents
