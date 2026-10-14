#include "app/infrastructure/TextFile.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "app/errors/AppException.hpp"

namespace app::infrastructure
{

namespace fs = std::filesystem;

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

bool fileExists(const std::string& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

}  // namespace app::infrastructure
