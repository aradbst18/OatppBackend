#include "support/TestRunner.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/PostgresqlLocalServer.hpp"
#include "app/logging/Logger.hpp"

namespace
{

namespace fs = std::filesystem;
using app::errors::database::ConnectionFailed;
using app::infrastructure::database::PostgresqlLocalServer;

struct InitdbCompleted
{
};
struct CommandException
{
};

enum class Fault
{
    None,
    Create,
    Permissions,
    Write,
    Close,
    RemoveOnce,
    RemoveAlways
};

struct Probe
{
    fs::path socketDirectory;
    fs::path passwordFile;
    std::string password = "private-bootstrap-password";
    std::string logs;
    int descriptor = -1;
    int commands = 0;
    int writes = 0;
    int removals = 0;
    int initdbResult = 0;
    bool interruptWrite = false;
    bool partialWrites = false;
    bool throwCommand = false;
    bool foreignOwner = false;
    mode_t creationMode = 0;
    Fault fault = Fault::None;
};

Probe* activeProbe = nullptr;

std::string readFile(const fs::path& path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct Fixture
{
    fs::path root;
    Probe probe;

    Fixture()
    {
        std::string pattern = (fs::temp_directory_path() / "oatpp-initdb-XXXXXX").string();
        EXPECT_TRUE(::mkdtemp(pattern.data()) != nullptr);
        root = pattern;
        probe.socketDirectory = root / "socket";
        EXPECT_EQ(::mkdir(probe.socketDirectory.c_str(), 0700), 0);
        activeProbe = &probe;
        app::logging::Logger::instance().configure(
            app::logging::Level::Info,
            [this](app::logging::Level, const std::string&, const std::string& message)
            {
                probe.logs += message + '\n';
            });
    }

    ~Fixture()
    {
        activeProbe = nullptr;
        app::logging::Logger::instance().resetToDefault();
        fs::remove_all(root);
    }

    void provision()
    {
        PostgresqlLocalServer server(probe.socketDirectory.string(), 0, "testdb", "testuser", probe.password);
        server.ensureProvisioned();
    }

    void expectClean() const
    {
        EXPECT_FALSE(probe.passwordFile.empty());
        EXPECT_FALSE(fs::exists(probe.passwordFile));
        EXPECT_EQ(::fcntl(probe.descriptor, F_GETFD), -1);
        EXPECT_EQ(errno, EBADF);
        EXPECT_EQ(probe.logs.find(probe.password), std::string::npos);
    }
};

struct ScopedUmask
{
    mode_t previous;
    explicit ScopedUmask(mode_t mask) : previous(::umask(mask))
    {
    }
    ~ScopedUmask()
    {
        ::umask(previous);
    }
};

}  // namespace

extern "C" int __real_mkstemp(char* pattern);
extern "C" ssize_t __real_write(int descriptor, const void* buffer, size_t count);
extern "C" int __real_fchmod(int descriptor, mode_t mode);
extern "C" int __real_close(int descriptor);
extern "C" int __real_unlink(const char* path);
extern "C" int __real_lstat(const char* path, struct stat* status);

extern "C" int __wrap_mkstemp(char* pattern)
{
    if (activeProbe && activeProbe->fault == Fault::Create)
    {
        errno = EACCES;
        return -1;
    }
    const int descriptor = __real_mkstemp(pattern);
    if (activeProbe && descriptor >= 0)
    {
        activeProbe->descriptor = descriptor;
        activeProbe->passwordFile = pattern;
        struct stat status{};
        EXPECT_EQ(::fstat(descriptor, &status), 0);
        activeProbe->creationMode = status.st_mode & 0777;
    }
    return descriptor;
}

extern "C" ssize_t __wrap_write(int descriptor, const void* buffer, size_t count)
{
    if (activeProbe && descriptor == activeProbe->descriptor)
    {
        ++activeProbe->writes;
        if (activeProbe->interruptWrite && activeProbe->writes == 1)
        {
            errno = EINTR;
            return -1;
        }
        if (activeProbe->fault == Fault::Write)
        {
            errno = ENOSPC;
            return -1;
        }
        if (activeProbe->partialWrites)
        {
            count = std::min(count, size_t{3});
        }
    }
    return __real_write(descriptor, buffer, count);
}

extern "C" int __wrap_fchmod(int descriptor, mode_t mode)
{
    if (activeProbe && descriptor == activeProbe->descriptor && activeProbe->fault == Fault::Permissions)
    {
        errno = EPERM;
        return -1;
    }
    return __real_fchmod(descriptor, mode);
}

extern "C" int __wrap_close(int descriptor)
{
    const int result = __real_close(descriptor);
    if (activeProbe && descriptor == activeProbe->descriptor && activeProbe->fault == Fault::Close)
    {
        errno = EIO;
        return -1;
    }
    return result;
}

extern "C" int __wrap_unlink(const char* path)
{
    if (activeProbe && activeProbe->passwordFile == path)
    {
        ++activeProbe->removals;
        if (activeProbe->fault == Fault::RemoveAlways ||
            (activeProbe->fault == Fault::RemoveOnce && activeProbe->removals == 1))
        {
            errno = EACCES;
            return -1;
        }
    }
    return __real_unlink(path);
}

extern "C" int __wrap_lstat(const char* path, struct stat* status)
{
    const int result = __real_lstat(path, status);
    if (result == 0 && activeProbe && activeProbe->foreignOwner && activeProbe->socketDirectory == path)
    {
        status->st_uid = ::geteuid() == 1 ? 2 : 1;
    }
    return result;
}

extern "C" int __wrap_system(const char* command)
{
    EXPECT_TRUE(activeProbe != nullptr);
    auto& probe = *activeProbe;
    const std::string cmd(command);
    const std::string marker = "--pwfile='";
    const auto start = cmd.find(marker);
    if (start == std::string::npos)
    {
        // Stop before pg_ctl starts a server or any database connection is opened.
        EXPECT_TRUE(cmd.find(" start") != std::string::npos);
        EXPECT_FALSE(fs::exists(probe.passwordFile));
        throw InitdbCompleted{};
    }

    ++probe.commands;
    const auto end = cmd.find('\'', start + marker.size());
    const fs::path passwordFile = cmd.substr(start + marker.size(), end - start - marker.size());
    EXPECT_EQ(passwordFile, probe.passwordFile);
    EXPECT_EQ(probe.creationMode & 0077, 0);
    struct stat status{};
    EXPECT_EQ(::lstat(passwordFile.c_str(), &status), 0);
    EXPECT_TRUE(S_ISREG(status.st_mode));
    EXPECT_EQ(status.st_mode & 0777, 0600);
    EXPECT_EQ(::fcntl(probe.descriptor, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(readFile(passwordFile), probe.password);
    EXPECT_EQ(cmd.find(probe.password), std::string::npos);
    if (probe.throwCommand)
    {
        throw CommandException{};
    }
    return probe.initdbResult;
}

TEST_CASE("postgresql_local: password is private at creation and removed before server startup")
{
    Fixture fixture;
    const ScopedUmask mask(0000);
    EXPECT_THROW_AS(fixture.provision(), InitdbCompleted);
    EXPECT_EQ(fixture.probe.creationMode, 0600);
    fixture.expectClean();
    const auto first = fixture.probe.passwordFile;
    EXPECT_THROW_AS(fixture.provision(), InitdbCompleted);
    EXPECT_FALSE(first == fixture.probe.passwordFile);
    fixture.expectClean();
}

TEST_CASE("postgresql_local: old fixed files and symlink targets remain untouched")
{
    for (const bool symlink : {false, true})
    {
        Fixture fixture;
        const auto victim = fixture.root / "victim";
        const auto oldPath = fixture.probe.socketDirectory / ".initpw.tmp";
        const auto existing = symlink ? victim : oldPath;
        std::ofstream(existing) << "preserve me";
        EXPECT_EQ(::chmod(existing.c_str(), 0644), 0);
        if (symlink)
        {
            fs::create_symlink(victim, oldPath);
        }
        EXPECT_THROW_AS(fixture.provision(), InitdbCompleted);
        EXPECT_EQ(readFile(existing), std::string("preserve me"));
        struct stat status{};
        EXPECT_EQ(::stat(existing.c_str(), &status), 0);
        EXPECT_EQ(status.st_mode & 0777, 0644);
        EXPECT_TRUE(fs::exists(oldPath));
        fixture.expectClean();
    }
}

TEST_CASE("postgresql_local: interrupted and partial writes preserve the full password")
{
    Fixture fixture;
    fixture.probe.interruptWrite = true;
    fixture.probe.partialWrites = true;
    const ScopedUmask mask(0777);
    EXPECT_THROW_AS(fixture.provision(), InitdbCompleted);
    EXPECT_TRUE(fixture.probe.writes > 2);
    fixture.expectClean();
}

TEST_CASE("postgresql_local: failed file creation never launches initdb")
{
    Fixture fixture;
    fixture.probe.fault = Fault::Create;
    EXPECT_THROW_AS(fixture.provision(), ConnectionFailed);
    EXPECT_EQ(fixture.probe.commands, 0);
    EXPECT_TRUE(fs::is_empty(fixture.probe.socketDirectory));
}

TEST_CASE("postgresql_local: permission write and close failures remove the file and abort initdb")
{
    for (const auto fault : {Fault::Permissions, Fault::Write, Fault::Close})
    {
        Fixture fixture;
        fixture.probe.fault = fault;
        EXPECT_THROW_AS(fixture.provision(), ConnectionFailed);
        EXPECT_EQ(fixture.probe.commands, 0);
        fixture.expectClean();
    }
}

TEST_CASE("postgresql_local: command failures and exceptions remove the password file")
{
    Fixture fixture;
    fixture.probe.initdbResult = 1;
    EXPECT_THROW_AS(fixture.provision(), ConnectionFailed);
    fixture.expectClean();
    fixture.probe.throwCommand = true;
    EXPECT_THROW_AS(fixture.provision(), CommandException);
    fixture.expectClean();
}

TEST_CASE("postgresql_local: deletion errors are reported and cleanup retries during unwinding")
{
    Fixture fixture;
    fixture.probe.fault = Fault::RemoveOnce;
    EXPECT_THROW_AS(fixture.provision(), ConnectionFailed);
    EXPECT_EQ(fixture.probe.removals, 2);
    fixture.expectClean();
}

TEST_CASE("postgresql_local: failed exception cleanup preserves the original error and logs a warning")
{
    Fixture fixture;
    fixture.probe.fault = Fault::RemoveAlways;
    fixture.probe.throwCommand = true;
    EXPECT_THROW_AS(fixture.provision(), CommandException);
    EXPECT_TRUE(fs::exists(fixture.probe.passwordFile));
    EXPECT_TRUE(fixture.probe.logs.find("cleanup") != std::string::npos);
    EXPECT_EQ(fixture.probe.logs.find(fixture.probe.password), std::string::npos);
}

TEST_CASE("postgresql_local: untrusted directories are rejected before creating a password")
{
    for (const bool ancestor : {false, true})
    {
        Fixture fixture;
        EXPECT_EQ(::chmod((ancestor ? fixture.root : fixture.probe.socketDirectory).c_str(), 0777), 0);
        EXPECT_THROW_AS(fixture.provision(), ConnectionFailed);
        EXPECT_TRUE(fixture.probe.passwordFile.empty());
        EXPECT_EQ(fixture.probe.commands, 0);
    }
    Fixture fixture;
    fixture.probe.foreignOwner = true;
    EXPECT_THROW_AS(fixture.provision(), ConnectionFailed);
    EXPECT_TRUE(fixture.probe.passwordFile.empty());
}

TEST_CASE("postgresql_local: a private directory under a trusted sticky parent is supported")
{
    Fixture fixture;
    EXPECT_EQ(::chmod(fixture.root.c_str(), 01777), 0);
    EXPECT_THROW_AS(fixture.provision(), InitdbCompleted);
    fixture.expectClean();
}
