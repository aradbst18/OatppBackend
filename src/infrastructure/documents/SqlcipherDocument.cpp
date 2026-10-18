#include "app/infrastructure/documents/SqlcipherDocument.hpp"

#ifndef APP_WITH_SQLCIPHER
#error "SqlcipherDocument.cpp requires APP_WITH_SQLCIPHER (configure with -DAPP_ENABLE_SQLCIPHER=ON)"
#endif  // !APP_WITH_SQLCIPHER

#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/SqlcipherDatabase.hpp"

namespace app::infrastructure::documents
{

namespace
{

// File access bypasses DatabaseFactory on purpose: the factory serves the
// single configured user database (APP_USERS_DATABASE_TYPE), while these
// helpers open arbitrary file databases (plaintext or encrypted, via the
// optional key) — including files whose encryption differs from the user
// database itself.
std::shared_ptr<infrastructure::database::IDatabase> openFile(const std::string& path, const std::string& key,
                                                              const std::string& what)
{
    auto db = std::make_shared<infrastructure::database::SqlcipherDatabase>(path, key);

    try
    {
        db->open();
    }
    catch (const errors::database::ConnectionFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::database::ConnectionFailed("Cannot open database '" + path + "': " + ex.what());
    }

    return db;
}

std::string quoteIdentifier(const std::string& name)
{
    std::string out = "\"";
    for (char c : name)
    {
        out += (c == '"') ? "\"\"" : std::string(1, c);
    }

    out += "\"";
    return out;
}

std::vector<std::string> tablesOf(infrastructure::database::IDatabase& db)
{
    const auto rows = db.execute(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY "
        "name");

    std::vector<std::string> names;
    names.reserve(rows.size());
    for (const auto& row : rows)
    {
        if (!row.empty())
        {
            names.push_back(row[0]);
        }
    }

    return names;
}

void requireKnownTable(infrastructure::database::IDatabase& db, const std::string& table, const std::string& path)
{
    const auto rows = db.execute("SELECT name FROM sqlite_master WHERE type = 'table' AND name = ?", {table});
    if (rows.empty())
    {
        throw errors::database::QueryFailed("Unknown table '" + table + "' in '" + path + "'");
    }
}

DocumentValue dumpOpenTable(infrastructure::database::IDatabase& db, const std::string& table, const std::string& path)
{
    requireKnownTable(db, table, path);

    const auto schemaRows = db.execute("SELECT sql FROM sqlite_master WHERE type = 'table' AND name = ?", {table});
    const std::string schema = (!schemaRows.empty() && !schemaRows[0].empty()) ? schemaRows[0][0] : "";

    const auto infoRows = db.execute("PRAGMA table_info(" + quoteIdentifier(table) + ")");
    std::vector<std::string> columns;
    columns.reserve(infoRows.size());
    for (const auto& row : infoRows)
    {
        if (row.size() > 1)
        {
            columns.push_back(row[1]);
        }
    }

    const auto dataRows = db.execute("SELECT * FROM " + quoteIdentifier(table));
    DocumentValue::List rows;
    rows.reserve(dataRows.size());
    for (const auto& data : dataRows)
    {
        DocumentValue::Map record;
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            const std::string column = (i < columns.size()) ? columns[i] : ("col" + std::to_string(i));
            record.emplace(column, DocumentValue(data[i]));
        }

        rows.emplace_back(std::move(record));
    }

    DocumentValue::List columnValues;
    columnValues.reserve(columns.size());
    for (const std::string& column : columns)
    {
        columnValues.emplace_back(column);
    }

    DocumentValue out = DocumentValue::makeMap();
    out["schema"] = schema;
    out["columns"] = std::move(columnValues);
    out["rows"] = std::move(rows);
    return out;
}

}  // namespace

std::shared_ptr<infrastructure::database::IDatabase> openSqlcipherDatabase(const std::string& path,
                                                                           const std::string& key)
{
    return openFile(path, key, "document");
}

std::vector<std::string> listSqlcipherTables(const std::string& path, const std::string& key)
{
    auto db = openFile(path, key, "inspection");
    return tablesOf(*db);
}

std::string sqlcipherTableSchema(const std::string& path, const std::string& table, const std::string& key)
{
    auto db = openFile(path, key, "inspection");
    requireKnownTable(*db, table, path);

    const auto rows = db->execute("SELECT sql FROM sqlite_master WHERE type = 'table' AND name = ?", {table});
    if (rows.empty() || rows[0].empty())
    {
        throw errors::database::QueryFailed("Unknown table '" + table + "' in '" + path + "'");
    }

    return rows[0][0];
}

DocumentValue dumpSqlcipherTable(const std::string& path, const std::string& table, const std::string& key)
{
    auto db = openFile(path, key, "dump");
    return dumpOpenTable(*db, table, path);
}

DocumentValue dumpSqlcipherDatabase(const std::string& path, const std::string& key)
{
    auto db = openFile(path, key, "dump");

    DocumentValue::Map tables;
    for (const std::string& name : tablesOf(*db))
    {
        tables.emplace(name, dumpOpenTable(*db, name, path));
    }

    DocumentValue out = DocumentValue::makeMap();
    out["backend"] = db->backendName();
    out["path"] = path;
    out["tables"] = std::move(tables);
    return out;
}

}  // namespace app::infrastructure::documents
