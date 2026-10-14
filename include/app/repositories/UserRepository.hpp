#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/infrastructure/database/IDatabase.hpp"

namespace app::repositories
{

struct UserRecord
{
    std::string username;
    std::string passwordHash;  // hex of the active APP_PASSWORD_HASH_ALGO (see auth::passwordHashHex),
                               // never a plaintext password
    std::string role;          // canonical role string ("admin", ...)
};

/// Base repository: owns the IDatabase handle and the tiny `fail()` helper.
/// Concrete repositories add one table-specific query set each.
class BaseRepository
{
public:
    explicit BaseRepository(std::shared_ptr<infrastructure::database::IDatabase> db);
    virtual ~BaseRepository() = default;

protected:
    std::shared_ptr<infrastructure::database::IDatabase> db_;
};

/**
 * Demo user store used by AuthService. Schema is created idempotently so the
 * application can boot on an empty database file.
 */
class UserRepository : public BaseRepository
{
public:
    using BaseRepository::BaseRepository;

    void ensureSchema();
    void upsert(const UserRecord& record);
    std::optional<UserRecord> findByUsername(const std::string& username);
    std::vector<UserRecord> listAll();
    void remove(const std::string& username);
};

}  // namespace app::repositories
