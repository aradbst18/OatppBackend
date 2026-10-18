#pragma once

// File database support is always SQLCipher-backed: cmake/dependencies.cmake
// links the SQLCipher library (the standard SQLite library is never searched)
// and satisfies the oatpp-sqlite adapter's exported SQLite::SQLite3 reference
// with it. Including this header without APP_WITH_SQLCIPHER is a compile error
// by design, which proves file code does not leak into no-file builds.
#ifndef APP_WITH_SQLCIPHER
#error "SqlcipherDatabase.hpp requires APP_WITH_SQLCIPHER (configure with -DAPP_ENABLE_SQLCIPHER=ON)"
#endif  // !APP_WITH_SQLCIPHER

#include <atomic>
#include <memory>
#include <string>

#include "oatpp/core/provider/Provider.hpp"
#include "oatpp/orm/Connection.hpp"

#include "app/infrastructure/database/IDatabase.hpp"

namespace oatpp::sqlite
{
class Connection;
class Executor;
}  // namespace oatpp::sqlite

namespace app::infrastructure::database
{

class DatabaseExecutor;

/**
 * File database backend over the SQLCipher library backed by an oatpp::sqlite::ConnectionPool.
 * Statements run through the oatpp-sqlite adapter: a ConnectionProvider subclass opens
 * native handles and owns its SQLCipher concerns (busy timeout, optional key via the
 * sqlite3_key C API, key validation), and the Executor prepares, binds and
 * steps every statement using pooled connections.
 */
class SqlcipherDatabase : public IDatabase
{
public:
    explicit SqlcipherDatabase(std::string path, std::string key = "");
    ~SqlcipherDatabase() override;

    SqlcipherDatabase(const SqlcipherDatabase&) = delete;
    SqlcipherDatabase& operator=(const SqlcipherDatabase&) = delete;

    void open() override;
    void close() noexcept override;
    bool isOpen() const noexcept override;

    std::vector<std::vector<std::string>> execute(const std::string& sql,
                                                  const std::vector<std::string>& params = {}) override;

    std::shared_ptr<oatpp::orm::Executor> ormExecutor() const override;

    std::shared_ptr<oatpp::orm::QueryResult> executeOrm(
        const oatpp::data::share::StringTemplate& query, const std::unordered_map<oatpp::String, oatpp::Void>& params,
        const std::shared_ptr<const oatpp::data::mapping::TypeResolver>& typeResolver = nullptr,
        const oatpp::provider::ResourceHandle<oatpp::orm::Connection>& connection = nullptr) override;

    oatpp::provider::ResourceHandle<oatpp::orm::Connection> ormConnection() override;

    std::string backendName() const noexcept override
    {
        return "SQLCIPHER";
    }

    const std::string& path() const noexcept
    {
        return path_;
    }

private:
    void ensureOpen() const;
    void initPool();

    std::string path_;
    std::string key_;
    std::atomic<bool> isOpen_{false};
    std::shared_ptr<oatpp::provider::Provider<oatpp::sqlite::Connection>> pool_;
    std::shared_ptr<oatpp::sqlite::Executor> executor_;
    std::shared_ptr<DatabaseExecutor> ormExecutor_;
};

}  // namespace app::infrastructure::database
