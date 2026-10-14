#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/config/AppSettings.hpp"

namespace app::infrastructure::database
{

/**
 * Minimal database abstraction. The application talks only to this interface;
 * SQLite vs SQLCipher is a compile-time selection (see DatabaseFactory).
 * SQL is always executed with bound parameters (? placeholders) so there is
 * no string-interpolated SQL anywhere in the codebase.
 */
class IDatabase
{
public:
    virtual ~IDatabase() = default;

    virtual void open() = 0;
    virtual void close() noexcept = 0;
    virtual bool isOpen() const noexcept = 0;

    /// Execute a statement with positional bound parameters. Returns rows for
    /// SELECT (each row is a vector of text values), empty for DDL/DML.
    /// Throws app::errors::database::{QueryFailed,ConnectionFailed} on failure.
    virtual std::vector<std::vector<std::string>> execute(const std::string& sql, const std::vector<std::string>& params = {}) = 0;

    virtual void executeVoid(const std::string& sql, const std::vector<std::string>& params = {})
    {
        execute(sql, params);
    }

    virtual std::string backendName() const noexcept = 0;
};

}  // namespace app::infrastructure::database
