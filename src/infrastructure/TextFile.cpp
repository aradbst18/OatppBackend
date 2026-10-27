#include "app/infrastructure/TextFile.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "app/errors/AppException.hpp"

namespace app::infrastructure
{

namespace fs = std::filesystem;

namespace
{

int syncDescriptor(int descriptor)
{
    int result;
    do
    {
        result = ::fsync(descriptor);
    } while (result != 0 && errno == EINTR);
    return result;
}

class Descriptor
{
public:
    Descriptor(int descriptor, const fs::path& path) : descriptor_(descriptor)
    {
        if (descriptor_ < 0)
        {
            throw errors::filesystem::WriteFailed("Cannot open config publication path: " + path.string());
        }
    }
    ~Descriptor()
    {
        ::close(descriptor_);
    }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    void sync(const fs::path& path) const
    {
        if (syncDescriptor(descriptor_) != 0)
        {
            throw errors::filesystem::WriteFailed("Failed to flush config publication path: " + path.string());
        }
    }

private:
    int descriptor_;
};

void syncDirectory(const fs::path& directory)
{
    const Descriptor descriptor{::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC), directory};
    descriptor.sync(directory);
}

fs::path directoryMarker(const fs::path& directory)
{
    // A bounded, deterministic sibling name lets independent publishers find
    // the same pending creation, including for maximum-length directory names.
    const auto name = directory.filename().string();
    unsigned char digest[SHA256_DIGEST_LENGTH];
    if (::SHA256(reinterpret_cast<const unsigned char*>(name.data()), name.size(), digest) == nullptr)
    {
        throw errors::filesystem::WriteFailed("Cannot name config directory marker: " + directory.string());
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string encoded;
    for (const auto byte : digest)
    {
        encoded += hex[byte >> 4];
        encoded += hex[byte & 15];
    }
    return directory.parent_path() / (".oatpp-dir-" + encoded + ".pending");
}

bool isPrivateDirectoryMarker(const fs::path& marker)
{
    struct stat status{};
    // Inspect the entry itself: a symlink, FIFO or another account's file is
    // not evidence of an unfinished publication by this application account.
    if (::lstat(marker.c_str(), &status) != 0)
    {
        if (errno == ENOENT)
        {
            return false;
        }
        throw errors::filesystem::WriteFailed("Cannot inspect config directory marker: " + marker.string());
    }
    return S_ISREG(status.st_mode) && status.st_uid == ::geteuid() && status.st_size == 0 &&
           (status.st_mode & 07777) == (S_IRUSR | S_IWUSR);
}

void removeDirectoryMarker(const fs::path& marker)
{
    if (!isPrivateDirectoryMarker(marker))
    {
        return;
    }
    // Another publisher may already have finished this directory.
    if (::unlink(marker.c_str()) != 0 && errno != ENOENT)
    {
        // In a shared sticky directory, another account may claim the name
        // after a cooperating publisher removes our marker. Leave it alone.
        if (!isPrivateDirectoryMarker(marker))
        {
            return;
        }
        throw errors::filesystem::WriteFailed("Cannot remove config directory marker: " + marker.string());
    }
}

void preparePrivateDirectory(fs::path directory)
{
    std::error_code error;
    const auto status = fs::status(directory, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        throw errors::filesystem::WriteFailed("Cannot inspect config directory: " + directory.string());
    }
    if (fs::exists(status) && !fs::is_directory(status))
    {
        throw errors::filesystem::WriteFailed("Config parent is not a directory: " + directory.string());
    }
    if (fs::is_directory(status))
    {
        directory = fs::canonical(directory, error);
        if (error)
        {
            throw errors::filesystem::WriteFailed("Cannot resolve config directory: " + directory.string());
        }
    }
    if (directory == directory.root_path())
    {
        return;
    }
    if (fs::is_directory(status))
    {
        // An existing directory without a private marker is the durable boundary. Publishers
        // finish the parent before creating children, so no higher entry can
        // still be pending. Inspecting a known marker needs only search access.
        const auto marker = directoryMarker(directory);
        if (isPrivateDirectoryMarker(marker))
        {
            const auto parent = directory.parent_path();
            syncDirectory(directory);
            syncDirectory(parent);
            removeDirectoryMarker(marker);
            syncDirectory(parent);
        }
        return;
    }

    preparePrivateDirectory(directory.parent_path());
    const auto parent = fs::canonical(directory.parent_path(), error);
    if (error)
    {
        throw errors::filesystem::WriteFailed("Cannot resolve config directory: " + directory.string());
    }
    directory = parent / directory.filename();
    if (directory.filename() == "." || directory.filename() == "..")
    {
        // Preserve pathname traversal: a missing component before ".." must
        // actually be created before the original file path can be opened.
        preparePrivateDirectory(directory);
        return;
    }
    const auto marker = directoryMarker(directory);
    const Descriptor parentDescriptor{::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC), parent};

    // Persist the pending record BEFORE mkdir makes the directory visible.
    // A racing publisher or a restart can then complete the necessary parent
    // sync without guessing whether an existing directory is already durable.
    // The parent is already prepared, so this nested write only stages and
    // publishes the empty marker with the usual atomic, private file writer.
    writePrivateTextFile(marker.string(), "", false);
    if (!isPrivateDirectoryMarker(marker))
    {
        if (fs::is_directory(directory, error))
        {
            // A racing publisher may have finished and removed the marker.
            preparePrivateDirectory(directory);
            return;
        }
        // A foreign entry must never stand in for our durable pending record.
        throw errors::filesystem::WriteFailed("Cannot publish private config directory marker: " + marker.string());
    }
    error.clear();
    fs::create_directory(directory, error);
    if (error)
    {
        throw errors::filesystem::WriteFailed("Cannot create config directory: " + directory.string());
    }
    syncDirectory(directory);
    parentDescriptor.sync(parent);
    removeDirectoryMarker(marker);
    parentDescriptor.sync(parent);
}

fs::path privateParentDirectory(const std::string& path)
{
    const auto parent = fs::path(path).parent_path();
    std::error_code error;
    const auto absolute = fs::absolute(parent.empty() ? fs::path(".") : parent, error);
    if (error)
    {
        throw errors::filesystem::WriteFailed("Cannot resolve config directory for: " + path);
    }
    preparePrivateDirectory(absolute);
    const auto directory = fs::canonical(absolute, error);
    if (error)
    {
        throw errors::filesystem::WriteFailed("Cannot resolve config directory for: " + path);
    }
    return directory;
}

}  // namespace

std::string readTextFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw errors::filesystem::NotFound("Cannot open config file: " + path);
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void ensureParentDirectory(const std::string& path)
{
    fs::path p(path);
    if (p.has_parent_path())
    {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        if (ec)
        {
            throw errors::filesystem::WriteFailed("Cannot create directory for: " + path);
        }
    }
}

void writeTextFile(const std::string& path, const std::string& content)
{
    ensureParentDirectory(path);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        throw errors::filesystem::WriteFailed("Cannot write config file: " + path);
    }

    out << content;
    out.flush();
    if (!out)
    {
        throw errors::filesystem::WriteFailed("Failed to write config file: " + path);
    }
}

void syncParentDirectories(const std::string& path)
{
    syncDirectory(privateParentDirectory(path));
}

void writePrivateTextFile(const std::string& path, const std::string& content, bool overwrite)
{
    privateParentDirectory(path);
    const std::string pattern = path + ".tmp.XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');

    int descriptor = ::mkstemp(temporary.data());
    if (descriptor < 0)
    {
        throw errors::filesystem::WriteFailed("Cannot create private config file: " + path);
    }

    try
    {
        if (::fchmod(descriptor, S_IRUSR | S_IWUSR) != 0)
        {
            throw errors::filesystem::WriteFailed("Cannot restrict config file permissions: " + path);
        }

        std::size_t written = 0;
        while (written < content.size())
        {
            const ssize_t count = ::write(descriptor, content.data() + written, content.size() - written);
            if (count < 0 && errno == EINTR)
            {
                continue;
            }
            if (count <= 0)
            {
                throw errors::filesystem::WriteFailed("Failed to write private config file: " + path);
            }
            written += static_cast<std::size_t>(count);
        }

        const int syncResult = syncDescriptor(descriptor);
        const int closeResult = ::close(descriptor);
        descriptor = -1;
        if (syncResult != 0 || closeResult != 0)
        {
            throw errors::filesystem::WriteFailed("Failed to flush private config file: " + path);
        }

        if (overwrite)
        {
            std::error_code error;
            fs::rename(temporary.data(), path, error);
            if (error)
            {
                throw errors::filesystem::WriteFailed("Cannot replace private config file: " + path);
            }
        }
        else
        {
            // Publish the fully written file without replacing another
            // startup's configuration. Both callers subsequently load it.
            if (::link(temporary.data(), path.c_str()) != 0 && errno != EEXIST)
            {
                throw errors::filesystem::WriteFailed("Cannot create private config file: " + path);
            }
            ::unlink(temporary.data());
        }
        syncParentDirectories(path);
    }
    catch (...)
    {
        if (descriptor >= 0)
        {
            ::close(descriptor);
        }
        ::unlink(temporary.data());
        throw;
    }
}

bool fileExists(const std::string& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

}  // namespace app::infrastructure
