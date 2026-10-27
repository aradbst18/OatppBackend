#pragma once

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

namespace app::tests
{

/// Override or unset one environment variable for a sequential test, then restore it.
class ScopedEnvironment
{
public:
    ScopedEnvironment(const char* name, const char* value) : name_(name)
    {
        if (const char* previous = std::getenv(name))
        {
            previous_ = previous;
        }
        const int result = value != nullptr ? ::setenv(name, value, 1) : ::unsetenv(name);
        if (result != 0)
        {
            throw std::runtime_error("Could not configure test environment variable " + name_);
        }
    }

    ~ScopedEnvironment()
    {
        if (previous_)
        {
            ::setenv(name_.c_str(), previous_->c_str(), 1);
        }
        else
        {
            ::unsetenv(name_.c_str());
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    std::string name_;
    std::optional<std::string> previous_;
};

}  // namespace app::tests
