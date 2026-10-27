#include "support/TestRunner.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "app/application/Application.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"

namespace
{

namespace fs = std::filesystem;
using app::infrastructure::readTextFile;
using app::infrastructure::writePrivateTextFile;

fs::path pendingMarker(const fs::path& directory)
{
    const auto name = directory.filename().string();
    unsigned char digest[SHA256_DIGEST_LENGTH];
    EXPECT_TRUE(::SHA256(reinterpret_cast<const unsigned char*>(name.data()), name.size(), digest) != nullptr);
    constexpr char hex[] = "0123456789abcdef";
    std::string encoded;
    for (const auto byte : digest)
    {
        encoded += hex[byte >> 4];
        encoded += hex[byte & 15];
    }
    return directory.parent_path() / (".oatpp-dir-" + encoded + ".pending");
}

struct SyncProbe
{
    std::vector<fs::path> directories;
    fs::path destination;
    std::string content;
    bool failDirectorySync = false;
    bool failBeforePublication = false;
    fs::path failingDirectory;
    bool failFileSync = false;
    bool interrupted = false;
    bool interruptDirectorySync = false;
    fs::path pauseDirectory;
    int readyDescriptor = -1;
    int releaseDescriptor = -1;
    fs::path foreignMarker;
    uid_t foreignUid = 0;
    int foreignUnlinkAttempts = 0;
    bool replaceMarkerOnUnlink = false;
};

SyncProbe* activeProbe = nullptr;

struct Fixture
{
    fs::path root;
    SyncProbe probe;

    Fixture()
    {
        const auto pattern = (fs::temp_directory_path() / "oatpp-private-file-XXXXXX").string();
        std::vector<char> temporary(pattern.begin(), pattern.end());
        temporary.push_back('\0');
        const char* directory = ::mkdtemp(temporary.data());
        EXPECT_TRUE(directory != nullptr);
        root = directory;
        probe.destination = root / "app.conf";
        probe.content = "private configuration";
        activeProbe = &probe;
    }

    ~Fixture()
    {
        activeProbe = nullptr;
        fs::remove_all(root);
    }

    bool synced(const fs::path& directory) const
    {
        return std::find(probe.directories.begin(), probe.directories.end(), fs::canonical(directory)) !=
               probe.directories.end();
    }
};

struct Pipe
{
    int descriptors[2];
    Pipe()
    {
        EXPECT_EQ(::pipe(descriptors), 0);
    }
    ~Pipe()
    {
        ::close(descriptors[0]);
        ::close(descriptors[1]);
    }
};

struct Child
{
    pid_t pid;
    ~Child()
    {
        if (pid > 0)
        {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
        }
    }
    int wait()
    {
        int status = 0;
        EXPECT_EQ(::waitpid(pid, &status, 0), pid);
        pid = -1;
        return status;
    }
};

}  // namespace

extern "C" int __real_fsync(int descriptor);
extern "C" int __real_lstat(const char* path, struct stat* status);
extern "C" int __real_unlink(const char* path);

extern "C" int __wrap_lstat(const char* path, struct stat* status)
{
    const int result = __real_lstat(path, status);
    if (result == 0 && activeProbe && activeProbe->foreignMarker == path)
    {
        status->st_uid = activeProbe->foreignUid;
    }
    return result;
}

extern "C" int __wrap_unlink(const char* path)
{
    if (activeProbe && activeProbe->foreignMarker == path)
    {
        ++activeProbe->foreignUnlinkAttempts;
        if (activeProbe->replaceMarkerOnUnlink)
        {
            activeProbe->foreignUid = ::geteuid() == 0 ? 1 : 0;
        }
        errno = EPERM;
        return -1;
    }
    return __real_unlink(path);
}

extern "C" int __wrap_fsync(int descriptor)
{
    if (activeProbe)
    {
        struct stat status{};
        if (::fstat(descriptor, &status) == 0)
        {
            if (S_ISDIR(status.st_mode))
            {
                const auto directory = fs::read_symlink("/proc/self/fd/" + std::to_string(descriptor));
                activeProbe->directories.push_back(directory);
                if (directory == activeProbe->pauseDirectory)
                {
                    char signal = 'x';
                    if (::write(activeProbe->readyDescriptor, &signal, 1) != 1 ||
                        ::read(activeProbe->releaseDescriptor, &signal, 1) != 1)
                    {
                        ::_exit(2);
                    }
                    activeProbe->pauseDirectory.clear();
                }
                const bool published = fs::exists(activeProbe->destination) &&
                                       readTextFile(activeProbe->destination.string()) == activeProbe->content;
                if (published || activeProbe->failBeforePublication)
                {
                    if (activeProbe->interruptDirectorySync && !activeProbe->interrupted)
                    {
                        activeProbe->interrupted = true;
                        errno = EINTR;
                        return -1;
                    }
                    if (activeProbe->failDirectorySync &&
                        (activeProbe->failingDirectory.empty() ||
                         activeProbe->directories.back() == activeProbe->failingDirectory))
                    {
                        errno = EIO;
                        return -1;
                    }
                }
            }
            else if (activeProbe->failFileSync)
            {
                errno = EIO;
                return -1;
            }
        }
    }
    return __real_fsync(descriptor);
}

TEST_CASE("private file: creation and replacement sync the published directory entry")
{
    Fixture fixture;
    for (const bool overwrite : {false, true})
    {
        fixture.probe.directories.clear();
        writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content, overwrite);
        EXPECT_TRUE(fixture.synced(fixture.root));
        EXPECT_EQ(readTextFile(fixture.probe.destination.string()), fixture.probe.content);
        fixture.probe.content += " updated";
    }
}

TEST_CASE("private file: publication syncs newly created parents and their directory entries")
{
    Fixture fixture;
    fixture.probe.destination = fixture.root / "new" / "nested" / "app.conf";
    writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content, false);
    EXPECT_TRUE(fixture.synced(fixture.root / "new" / "nested"));
    EXPECT_TRUE(fixture.synced(fixture.root / "new"));
    EXPECT_TRUE(fixture.synced(fixture.root));
}

TEST_CASE("private file: parent traversal preserves missing components before dot-dot")
{
    Fixture fixture;
    fixture.probe.destination = fixture.root / "created" / ".." / "app.conf";
    writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content, false);
    EXPECT_TRUE(fs::is_directory(fixture.root / "created"));
    EXPECT_EQ(readTextFile((fixture.root / "app.conf").string()), fixture.probe.content);
}

TEST_CASE("private file: directory sync failures prevent successful publication returns")
{
    Fixture fixture;
    fixture.probe.failDirectorySync = true;
    for (const bool overwrite : {false, true})
    {
        fs::remove(fixture.probe.destination);
        EXPECT_THROW_AS(writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content, overwrite),
                        app::errors::filesystem::WriteFailed);
        EXPECT_EQ(readTextFile(fixture.probe.destination.string()), fixture.probe.content);
        EXPECT_EQ(std::distance(fs::directory_iterator(fixture.root), fs::directory_iterator()), 1);
    }
}

TEST_CASE("private file: an ancestor sync failure also prevents successful publication")
{
    Fixture fixture;
    fixture.probe.destination = fixture.root / "new" / "nested" / "app.conf";
    fixture.probe.failDirectorySync = true;
    fixture.probe.failBeforePublication = true;
    fixture.probe.failingDirectory = fixture.root / "new";
    EXPECT_THROW_AS(writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content, false),
                    app::errors::filesystem::WriteFailed);
    EXPECT_TRUE(fixture.synced(fixture.root / "new"));
    EXPECT_FALSE(fs::exists(fixture.probe.destination));
    EXPECT_FALSE(fs::exists(fixture.root / "new" / "nested"));
    fixture.probe.failDirectorySync = false;
    fixture.probe.directories.clear();
    writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content, false);
    EXPECT_TRUE(fixture.synced(fixture.root));
    EXPECT_TRUE(fixture.synced(fixture.root / "new"));
    EXPECT_EQ(readTextFile(fixture.probe.destination.string()), fixture.probe.content);
}

TEST_CASE("private file: losing a creation race still syncs the winning configuration")
{
    Fixture fixture;
    app::infrastructure::writeTextFile(fixture.probe.destination.string(), fixture.probe.content);
    fixture.probe.failDirectorySync = true;
    EXPECT_THROW_AS(writePrivateTextFile(fixture.probe.destination.string(), "losing secrets", false),
                    app::errors::filesystem::WriteFailed);
    EXPECT_EQ(readTextFile(fixture.probe.destination.string()), fixture.probe.content);
}

TEST_CASE("private file: startup finding an existing config still requires durable directory entries")
{
    Fixture fixture;
    app::infrastructure::writeTextFile(fixture.probe.destination.string(), fixture.probe.content);
    fixture.probe.failDirectorySync = true;
    EXPECT_THROW_AS(app::application::ensureConfigExists(fixture.probe.destination.string()),
                    app::errors::filesystem::WriteFailed);
    EXPECT_EQ(readTextFile(fixture.probe.destination.string()), fixture.probe.content);
}

TEST_CASE("private file: startup works below a pre-existing search-only ancestor")
{
    if (::geteuid() == 0)
    {
        std::cout << "[SKIP] search-only permissions require a non-root test user\n";
        return;
    }
    Fixture fixture;
    const auto applicationDirectory = fixture.root / "application";
    fs::create_directory(applicationDirectory);
    fixture.probe.destination = applicationDirectory / "app.conf";
    app::application::ensureConfigExists(fixture.probe.destination.string());
    const auto original = readTextFile(fixture.probe.destination.string());

    struct SearchOnlyDirectory
    {
        fs::path path;
        SearchOnlyDirectory(fs::path directory, fs::perms permissions) : path(std::move(directory))
        {
            fs::permissions(path, permissions);
        }
        ~SearchOnlyDirectory()
        {
            fs::permissions(path, fs::perms::owner_all);
        }
    };

    for (const auto permissions : {fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                                   fs::perms::owner_exec | fs::perms::owner_write})
    {
        const SearchOnlyDirectory searchOnly{fixture.root, permissions};
        errno = 0;
        const int descriptor = ::open(fixture.root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (descriptor >= 0)
        {
            ::close(descriptor);
        }
        EXPECT_EQ(descriptor, -1);
        EXPECT_EQ(errno, EACCES);

        fixture.probe.directories.clear();
        const auto loaded = app::application::loadSettingsOrDefaults(fixture.probe.destination.string());
        EXPECT_TRUE(loaded.validate().empty());
        EXPECT_EQ(readTextFile(fixture.probe.destination.string()), original);
        EXPECT_EQ(fixture.probe.directories.size(), std::size_t{1});
        EXPECT_TRUE(fixture.synced(applicationDirectory));

        // New configs and nested parents also work within the accessible app
        // directory, without opening or modifying the search-only ancestor.
        const auto freshPath = applicationDirectory / "new" / "nested" / "app.conf";
        fs::remove_all(applicationDirectory / "new");
        const auto fresh = app::application::loadSettingsOrDefaults(freshPath.string());
        EXPECT_TRUE(fresh.validate().empty());
        EXPECT_TRUE(fixture.synced(applicationDirectory / "new"));
        EXPECT_FALSE(fixture.synced(fixture.root));
    }
}

TEST_CASE("private file: established directories ignore invalid marker types and contents")
{
    Fixture fixture;
    const auto applicationDirectory = fixture.root / "application";
    fs::create_directory(applicationDirectory);
    fixture.probe.destination = applicationDirectory / "app.conf";
    app::application::ensureConfigExists(fixture.probe.destination.string());
    const auto original = readTextFile(fixture.probe.destination.string());
    const auto marker = pendingMarker(applicationDirectory);
    const auto target = fixture.root / "unrelated-file";
    writePrivateTextFile(target.string(), "preserve this file");

    for (const int kind : {0, 1, 2, 3, 4})
    {
        switch (kind)
        {
            case 0:
                fs::create_directory(marker);
                break;
            case 1:
                fs::create_symlink(target, marker);
                break;
            case 2:
                EXPECT_EQ(::mkfifo(marker.c_str(), 0600), 0);
                break;
            case 3:
                writePrivateTextFile(marker.string(), "not a recovery marker");
                break;
            case 4:
                writePrivateTextFile(marker.string(), "");
                fs::permissions(marker, fs::perms::all);
                break;
        }
        fixture.probe.directories.clear();
        const auto loaded = app::application::loadSettingsOrDefaults(fixture.probe.destination.string());
        EXPECT_TRUE(loaded.validate().empty());
        EXPECT_EQ(readTextFile(fixture.probe.destination.string()), original);
        EXPECT_TRUE(fs::symlink_status(marker).type() != fs::file_type::not_found);
        EXPECT_FALSE(fixture.synced(fixture.root));
        EXPECT_EQ(readTextFile(target.string()), std::string("preserve this file"));
        fs::remove(marker);
    }
}

TEST_CASE("private file: an invalid marker cannot authorize new directory creation")
{
    Fixture fixture;
    const auto applicationDirectory = fixture.root / "application";
    fixture.probe.destination = applicationDirectory / "app.conf";
    const auto marker = pendingMarker(applicationDirectory);
    fs::create_directory(marker);
    EXPECT_THROW_AS(writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content, false),
                    app::errors::filesystem::WriteFailed);
    EXPECT_FALSE(fs::exists(applicationDirectory));
    EXPECT_TRUE(fs::is_directory(marker));
}

TEST_CASE("private file: a foreign marker cannot block an established application directory")
{
    Fixture fixture;
    const auto applicationDirectory = fixture.root / "application";
    fs::create_directory(applicationDirectory);
    fixture.probe.destination = applicationDirectory / "app.conf";
    app::application::ensureConfigExists(fixture.probe.destination.string());
    const auto original = readTextFile(fixture.probe.destination.string());
    const auto marker = pendingMarker(applicationDirectory);
    writePrivateTextFile(marker.string(), "");
    fixture.probe.foreignMarker = marker;
    fixture.probe.foreignUid = ::geteuid() == 0 ? 1 : 0;
    fixture.probe.directories.clear();
    const auto loaded = app::application::loadSettingsOrDefaults(fixture.probe.destination.string());
    EXPECT_TRUE(loaded.validate().empty());
    EXPECT_EQ(fixture.probe.foreignUnlinkAttempts, 0);
    EXPECT_TRUE(fs::is_regular_file(marker));
    EXPECT_FALSE(fixture.synced(fixture.root));
    EXPECT_EQ(readTextFile(fixture.probe.destination.string()), original);
    fixture.probe.foreignMarker.clear();
}

TEST_CASE("private file: a foreign marker cannot authorize creating a missing directory")
{
    Fixture fixture;
    const auto applicationDirectory = fixture.root / "application";
    const auto marker = pendingMarker(applicationDirectory);
    writePrivateTextFile(marker.string(), "");
    fixture.probe.foreignMarker = marker;
    fixture.probe.foreignUid = ::geteuid() == 0 ? 1 : 0;
    EXPECT_THROW_AS(writePrivateTextFile((applicationDirectory / "app.conf").string(), fixture.probe.content),
                    app::errors::filesystem::WriteFailed);
    EXPECT_FALSE(fs::exists(applicationDirectory));
    EXPECT_TRUE(fs::is_regular_file(marker));
    EXPECT_EQ(fixture.probe.foreignUnlinkAttempts, 0);
    fixture.probe.foreignMarker.clear();
}

TEST_CASE("private file: foreign replacement during marker cleanup is preserved")
{
    for (const bool replaced : {false, true})
    {
        Fixture fixture;
        const auto applicationDirectory = fixture.root / "application";
        fs::create_directory(applicationDirectory);
        fixture.probe.destination = applicationDirectory / "app.conf";
        app::application::ensureConfigExists(fixture.probe.destination.string());
        const auto original = readTextFile(fixture.probe.destination.string());
        const auto marker = pendingMarker(applicationDirectory);
        writePrivateTextFile(marker.string(), "");
        fixture.probe.foreignMarker = marker;
        fixture.probe.foreignUid = ::geteuid();
        fixture.probe.replaceMarkerOnUnlink = replaced;
        if (replaced)
        {
            const auto loaded = app::application::loadSettingsOrDefaults(fixture.probe.destination.string());
            EXPECT_TRUE(loaded.validate().empty());
        }
        else
        {
            // Permission failures for a genuine marker must still abort startup.
            EXPECT_THROW_AS(app::application::loadSettingsOrDefaults(fixture.probe.destination.string()),
                            app::errors::filesystem::WriteFailed);
        }
        EXPECT_EQ(fixture.probe.foreignUnlinkAttempts, 1);
        EXPECT_TRUE(fs::is_regular_file(marker));
        EXPECT_EQ(readTextFile(fixture.probe.destination.string()), original);
        fixture.probe.foreignMarker.clear();
    }
}

TEST_CASE("private file: multi-user sticky directories preserve foreign markers during startup")
{
    if (::geteuid() != 0)
    {
        std::cout << "[SKIP] multi-user fixture creation requires root; children run as unprivileged UIDs\n";
        return;
    }
    Fixture fixture;
    constexpr uid_t applicationUid = 65532;
    constexpr uid_t attackerUid = 65533;
    const auto applicationDirectory = fixture.root / "application";
    fs::create_directory(applicationDirectory);
    fs::permissions(fixture.root, fs::perms::all | fs::perms::sticky_bit);
    fs::permissions(applicationDirectory, fs::perms::owner_all);
    fixture.probe.destination = applicationDirectory / "app.conf";
    app::application::ensureConfigExists(fixture.probe.destination.string());
    const auto original = readTextFile(fixture.probe.destination.string());
    const auto marker = pendingMarker(applicationDirectory);
    writePrivateTextFile(marker.string(), "");
    EXPECT_EQ(::chown(marker.c_str(), attackerUid, attackerUid), 0);
    EXPECT_EQ(::chown(fixture.probe.destination.c_str(), applicationUid, applicationUid), 0);
    EXPECT_EQ(::chown(applicationDirectory.c_str(), applicationUid, applicationUid), 0);

    Child application{::fork()};
    if (application.pid == 0)
    {
        if (::setgroups(0, nullptr) != 0 || ::setgid(applicationUid) != 0 || ::setuid(applicationUid) != 0)
        {
            ::_exit(2);
        }
        // Prove the kernel enforces the exact sticky-directory failure before
        // invoking startup; the application must never try this deletion.
        if (::unlink(marker.c_str()) != -1 || errno != EPERM)
        {
            ::_exit(3);
        }
        try
        {
            const auto loaded = app::application::loadSettingsOrDefaults(fixture.probe.destination.string());
            if (!loaded.validate().empty() || readTextFile(fixture.probe.destination.string()) != original)
            {
                ::_exit(4);
            }
            ::_exit(0);
        }
        catch (...)
        {
            ::_exit(1);
        }
    }
    EXPECT_TRUE(application.pid > 0);
    const int status = application.wait();
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    struct stat markerStatus{};
    EXPECT_EQ(::lstat(marker.c_str(), &markerStatus), 0);
    EXPECT_EQ(markerStatus.st_uid, attackerUid);
    EXPECT_TRUE(S_ISREG(markerStatus.st_mode));
    EXPECT_EQ(readTextFile(fixture.probe.destination.string()), original);
}

TEST_CASE("private file: concurrent publishers and restarts finish pending directory publication")
{
    for (const bool interrupted : {false, true})
    {
        Fixture fixture;
        fixture.probe.destination = fixture.root / "new" / "nested" / "app.conf";
        Pipe ready;
        Pipe release;
        Child publisher{::fork()};
        if (publisher.pid == 0)
        {
            fixture.probe.pauseDirectory = fixture.root / "new";
            fixture.probe.readyDescriptor = ready.descriptors[1];
            fixture.probe.releaseDescriptor = release.descriptors[0];
            try
            {
                writePrivateTextFile(fixture.probe.destination.string(), "first publisher", false);
                ::_exit(0);
            }
            catch (...)
            {
                ::_exit(1);
            }
        }
        EXPECT_TRUE(publisher.pid > 0);
        struct pollfd signal{ready.descriptors[0], POLLIN, 0};
        EXPECT_EQ(::poll(&signal, 1, 10000), 1);
        char byte;
        EXPECT_EQ(::read(ready.descriptors[0], &byte, 1), 1);
        EXPECT_TRUE(fs::is_directory(fixture.root / "new"));
        EXPECT_FALSE(fs::exists(fixture.probe.destination));

        if (interrupted)
        {
            EXPECT_EQ(::kill(publisher.pid, SIGKILL), 0);
            EXPECT_TRUE(WIFSIGNALED(publisher.wait()));
        }
        writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content, false);
        EXPECT_TRUE(fixture.synced(fixture.root));
        EXPECT_TRUE(fixture.synced(fixture.root / "new"));
        if (!interrupted)
        {
            EXPECT_EQ(::write(release.descriptors[1], &byte, 1), 1);
            const int status = publisher.wait();
            EXPECT_TRUE(WIFEXITED(status));
            EXPECT_EQ(WEXITSTATUS(status), 0);
        }
        EXPECT_EQ(readTextFile(fixture.probe.destination.string()), fixture.probe.content);
        for (const auto& entry : fs::recursive_directory_iterator(fixture.root))
        {
            EXPECT_FALSE(entry.path().extension() == ".pending");
        }
    }
}

TEST_CASE("private file: interrupted directory syncs are retried")
{
    Fixture fixture;
    fixture.probe.interruptDirectorySync = true;
    writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content);
    EXPECT_TRUE(fixture.probe.interrupted);
    EXPECT_TRUE(fixture.probe.directories.size() >= 2);
}

TEST_CASE("private file: file sync failure preserves the previous configuration")
{
    Fixture fixture;
    app::infrastructure::writeTextFile(fixture.probe.destination.string(), "previous secrets");
    fixture.probe.failFileSync = true;
    EXPECT_THROW_AS(writePrivateTextFile(fixture.probe.destination.string(), fixture.probe.content),
                    app::errors::filesystem::WriteFailed);
    EXPECT_EQ(readTextFile(fixture.probe.destination.string()), std::string("previous secrets"));
    EXPECT_EQ(std::distance(fs::directory_iterator(fixture.root), fs::directory_iterator()), 1);
}
