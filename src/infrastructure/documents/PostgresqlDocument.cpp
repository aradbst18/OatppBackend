#include "app/infrastructure/documents/PostgresqlDocument.hpp"

#ifndef APP_WITH_POSTGRESQL
#error "PostgresqlDocument.cpp requires APP_WITH_POSTGRESQL (configure with -DAPP_ENABLE_POSTGRESQL=ON)"
#endif  // !APP_WITH_POSTGRESQL

#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/PostgresqlDatabase.hpp"

namespace app::infrastructure::documents
{

namespace
{

std::shared_ptr<infrastructure::database::IDatabase> openBackend(const std::string& path, std::uint16_t port,
                                                                 const std::string& dbname, const std::string& user,
                                                                 const std::string& key, const std::string& what)
{
    auto db = std::make_shared<infrastructure::database::PostgresqlDatabase>(path, port, dbname, user, key);

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
        throw errors::database::ConnectionFailed("Cannot open PostgreSQL " + what + " '" + dbname + "' at " + path +
                                                 ": " + ex.what());
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

std::string describeTarget(const std::string& path, const std::string& dbname)
{
    return "'" + dbname + "' at " + path;
}

// Catalog and dump queries select text (cast(... as text)) throughout: the
// oatpp-postgresql result mapper maps text-typed columns only, and the casts
// keep every dump working for any column type (see PostgresqlDatabase.hpp).
std::vector<std::string> tablesOf(infrastructure::database::IDatabase& db)
{
    const auto rows = db.execute(
        "SELECT cast(table_name as text) FROM information_schema.tables WHERE table_schema = "
        "'public' AND table_type = 'BASE TABLE' ORDER BY "
        "table_name");

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

void requireKnownTable(infrastructure::database::IDatabase& db, const std::string& table, const std::string& path,
                       const std::string& dbname)
{
    const auto rows = db.execute(
        "SELECT cast(table_name as text) FROM information_schema.tables WHERE table_schema = "
        "'public' AND table_type = 'BASE TABLE' AND table_name = "
        "?",
        {table});

    if (rows.empty())
    {
        throw errors::database::QueryFailed("Unknown table '" + table + "' in " + describeTarget(path, dbname));
    }
}

DocumentValue dumpOpenTable(infrastructure::database::IDatabase& db, const std::string& table, const std::string& path,
                            const std::string& dbname)
{
    requireKnownTable(db, table, path, dbname);

    const auto infoRows = db.execute(
        "SELECT cast(column_name as text) FROM information_schema.columns WHERE table_schema = "
        "'public' AND table_name = ? ORDER BY ordinal_position",
        {table});

    std::vector<std::string> columns;
    columns.reserve(infoRows.size());
    for (const auto& row : infoRows)
    {
        if (!row.empty())
        {
            columns.push_back(row[0]);
        }
    }

    // Cast every column to text (see the text-cast note above). A table with
    // no columns keeps the plain star shape (zero-column rows).
    std::string projection;
    for (const std::string& column : columns)
    {
        if (!projection.empty())
        {
            projection += ", ";
        }

        projection += "cast(" + quoteIdentifier(column) + " as text)";
    }

    const auto dataRows = db.execute(projection.empty() ? ("SELECT * FROM " + quoteIdentifier(table))
                                                        : ("SELECT " + projection + " FROM " + quoteIdentifier(table)));

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
    out["schema"] = "";
    out["columns"] = std::move(columnValues);
    out["rows"] = std::move(rows);
    return out;
}

}  // namespace

std::shared_ptr<infrastructure::database::IDatabase> openPostgresqlDatabase(const std::string& path, std::uint16_t port,
                                                                            const std::string& dbname,
                                                                            const std::string& user,
                                                                            const std::string& key)
{
    return openBackend(path, port, dbname, user, key, "document");
}

std::vector<std::string> listPostgresqlTables(const std::string& path, std::uint16_t port, const std::string& dbname,
                                              const std::string& user, const std::string& key)
{
    auto db = openBackend(path, port, dbname, user, key, "inspection");
    return tablesOf(*db);
}

std::string postgresqlTableSchema(const std::string& path, std::uint16_t port, const std::string& dbname,
                                  const std::string& table, const std::string& user, const std::string& key)
{
    auto db = openBackend(path, port, dbname, user, key, "inspection");
    requireKnownTable(*db, table, path, dbname);
    return "";
}

DocumentValue dumpPostgresqlTable(const std::string& path, std::uint16_t port, const std::string& dbname,
                                  const std::string& table, const std::string& user, const std::string& key)
{
    auto db = openBackend(path, port, dbname, user, key, "dump");
    return dumpOpenTable(*db, table, path, dbname);
}

DocumentValue dumpPostgresqlDatabase(const std::string& path, std::uint16_t port, const std::string& dbname,
                                     const std::string& user, const std::string& key)
{
    auto db = openBackend(path, port, dbname, user, key, "dump");

    DocumentValue::Map tables;
    for (const std::string& name : tablesOf(*db))
    {
        tables.emplace(name, dumpOpenTable(*db, name, path, dbname));
    }

    DocumentValue out = DocumentValue::makeMap();
    out["backend"] = db->backendName();
    out["dsn"] = "host=" + path + " port=" + std::to_string(port) + " dbname=" + dbname + " user=" + user;
    out["tables"] = std::move(tables);
    return out;
}

}  // namespace app::infrastructure::documents
