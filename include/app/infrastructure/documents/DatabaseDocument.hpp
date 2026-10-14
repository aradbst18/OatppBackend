#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/infrastructure/database/IDatabase.hpp"
#include "app/infrastructure/documents/DocumentValue.hpp"

namespace app::infrastructure::documents
{

/**
 * Generic SQLite/SQLCipher inspector. Opens any database file through the
 * active backend (see DatabaseFactory) and converts schema + content into
 * the neutral DocumentValue tree, independent of application tables. This is the
 * database analogue of JsonDocument/TomlDocument/XmlDocument/YamlDocument:
 * future code can read arbitrary database files (system files, other apps'
 * files) without touching SQL or the sqlite API directly.
 *
 * Read-only by design: no writes, no DDL, no PRAGMA except key application
 * and introspection queries. Table names are always whitelisted against
 * sqlite_master before use, so identifier interpolation is safe.
 *
 * Key rule mirrors the backend: SQLCIPHER builds require a non-empty key
 * (SqlcipherDatabase refuses empty keys); SQLITE builds forbid a key
 * (passing one is errors::config::Invalid, not a silent ignore).
 * Error mapping: open/key failures surface the backend's
 * errors::database::ConnectionFailed; unknown tables and bad SQL surface
 * errors::database::QueryFailed with the path embedded.
 */
std::shared_ptr<infrastructure::database::IDatabase> openDocumentDatabase(const std::string& path, const std::string& key = "");

/**
 * User table names in sqlite_master order, excluding sqlite_% internals.
 */
std::vector<std::string> listDatabaseTables(const std::string& path, const std::string& key = "");

/**
 * The CREATE statement for one table (its `sql` in sqlite_master).
 * Throws errors::database::QueryFailed when the table does not exist.
 */
std::string databaseTableSchema(const std::string& path, const std::string& table, const std::string& key = "");

/**
 * One table as a DocumentValue Map: {"schema": String, "columns": List of String,
 * "rows": List of Maps (column name -> String cell)}. Throws
 * errors::database::QueryFailed for unknown tables.
 */
DocumentValue dumpDatabaseTable(const std::string& path, const std::string& table, const std::string& key = "");

/**
 * Whole database as a DocumentValue Map: {"backend": String, "path": String,
 * "tables": Map of table name -> dumpDatabaseTable()}. Feed it to
 * serializeJson() (or any DocumentValue serializer) to snapshot a database file.
 */
DocumentValue dumpDatabase(const std::string& path, const std::string& key = "");

}  // namespace app::infrastructure::documents
