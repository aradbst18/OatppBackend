#include "app/repositories/UserRepository.hpp"

#include "app/errors/AppException.hpp"

namespace app::repositories
{

BaseRepository::BaseRepository(std::shared_ptr<infrastructure::database::IDatabase> db) : db_(std::move(db))
{
    if (!db_)
    {
        throw errors::database::ConnectionFailed("UserRepository requires a database");
    }
}

void UserRepository::ensureSchema()
{
    db_->executeVoid(
        "CREATE TABLE IF NOT EXISTS users ("
        " username TEXT PRIMARY KEY,"
        " password_hash TEXT NOT NULL,"
        " role TEXT NOT NULL"
        ")");
}

void UserRepository::upsert(const UserRecord& record)
{
    if (record.username.empty() || record.passwordHash.empty() || record.role.empty())
    {
        throw errors::validation::InvalidValue("UserRecord fields must not be empty");
    }

    db_->executeVoid(
        "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?) "
        "ON CONFLICT(username) DO UPDATE SET password_hash=excluded.password_hash, "
        "role=excluded.role",
        {record.username, record.passwordHash, record.role});
}

std::optional<UserRecord> UserRepository::findByUsername(const std::string& username)
{
    const auto rows = db_->execute("SELECT username, password_hash, role FROM users WHERE username = ?", {username});
    if (rows.empty())
    {
        return std::nullopt;
    }

    return UserRecord{rows[0][0], rows[0][1], rows[0][2]};
}

std::vector<UserRecord> UserRepository::listAll()
{
    const auto rows = db_->execute("SELECT username, password_hash, role FROM users ORDER BY username");
    std::vector<UserRecord> out;
    out.reserve(rows.size());

    for (const auto& r : rows)
    {
        out.push_back(UserRecord{r[0], r[1], r[2]});
    }

    return out;
}

void UserRepository::remove(const std::string& username)
{
    db_->executeVoid("DELETE FROM users WHERE username = ?", {username});
}

}  // namespace app::repositories
