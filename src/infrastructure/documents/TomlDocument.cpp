#include "app/infrastructure/documents/TomlDocument.hpp"

#ifndef APP_WITH_TOML_PARSER
#error "TomlDocument.cpp requires APP_WITH_TOML_PARSER (configure with -DAPP_ENABLE_TOML_PARSER=ON)"
#endif  // !APP_WITH_TOML_PARSER

#include <sstream>

#include <toml.hpp>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"

namespace app::infrastructure::documents
{

namespace
{

DocumentValue fromTomlValue(const toml::value& node, const std::string& source)
{
    if (node.is_boolean())
    {
        return DocumentValue(toml::get<bool>(node));
    }

    if (node.is_integer())
    {
        return DocumentValue(toml::get<std::int64_t>(node));
    }

    if (node.is_floating())
    {
        return DocumentValue(toml::get<double>(node));
    }

    if (node.is_string())
    {
        return DocumentValue(toml::get<std::string>(node));
    }

    if (node.is_array())
    {
        const auto& items = toml::get<std::vector<toml::value>>(node);
        DocumentValue::List out;
        out.reserve(items.size());
        for (const toml::value& item : items)
        {
            out.push_back(fromTomlValue(item, source));
        }

        return DocumentValue(std::move(out));
    }

    if (node.is_table())
    {
        const auto& table = toml::get<toml::table>(node);
        DocumentValue::Map out;
        for (const auto& [key, item] : table)
        {
            out.emplace(key, fromTomlValue(item, source));
        }

        return DocumentValue(std::move(out));
    }

    if (node.is_empty())
    {
        return DocumentValue::null();
    }

    // Datetimes (offset/local datetime, local date/time) have no neutral
    // counterpart: keep the ISO-8601 literal as a String so arbitrary files
    // round-trip without data loss.
    try
    {
        std::ostringstream out;
        out << node;
        return DocumentValue(out.str());
    }
    catch (const std::exception& ex)
    {
        throw errors::serialization::ParsingFailed("Unsupported TOML node in " + source + ": " + ex.what());
    }
}

toml::value toTomlValue(const DocumentValue& value, const std::string& source)
{
    switch (value.type())
    {
        case DocumentValue::Type::Bool:
            return toml::value(value.asBool());
        case DocumentValue::Type::Int:
            return toml::value(value.asInt());
        case DocumentValue::Type::Double:
            return toml::value(value.asDouble());
        case DocumentValue::Type::String:
            return toml::value(value.asString());
        case DocumentValue::Type::List:
        {
            std::vector<toml::value> out;
            out.reserve(value.asList().size());
            for (const DocumentValue& item : value.asList())
            {
                out.push_back(toTomlValue(item, source));
            }

            return toml::value(std::move(out));
        }
        case DocumentValue::Type::Map:
        {
            toml::table out;
            for (const auto& [key, item] : value.asMap())
            {
                out.emplace(key, toTomlValue(item, source));
            }

            return toml::value(std::move(out));
        }
        case DocumentValue::Type::Null:
            break;
    }

    throw errors::serialization::SerializationFailed("TOML has no null; cannot encode Null node for " + source);
}

}  // namespace

DocumentValue parseToml(const std::string& text, const std::string& source)
{
    try
    {
        const toml::value root = toml::parse_str(text);
        if (!root.is_table())
        {
            throw errors::serialization::ParsingFailed("TOML root must be a table in " + source);
        }

        return fromTomlValue(root, source);
    }
    catch (const errors::serialization::ParsingFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::serialization::ParsingFailed("Invalid TOML in " + source + ": " + ex.what());
    }
}

std::string serializeToml(const DocumentValue& value)
{
    if (!value.isMap())
    {
        throw errors::serialization::SerializationFailed("TOML document must be a Map at the top level");
    }

    try
    {
        std::ostringstream out;
        out << toml::format(toTomlValue(value, "<value>"));
        return out.str();
    }
    catch (const errors::serialization::SerializationFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::serialization::SerializationFailed(std::string("Failed to encode TOML: ") + ex.what());
    }
}

DocumentValue loadTomlFile(const std::string& path)
{
    try
    {
        const toml::value root = toml::parse(path);
        if (!root.is_table())
        {
            throw errors::serialization::ParsingFailed("TOML root must be a table in " + path);
        }

        return fromTomlValue(root, path);
    }
    catch (const errors::serialization::ParsingFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::serialization::ParsingFailed("Invalid TOML in " + path + ": " + ex.what());
    }
}

void saveTomlFile(const std::string& path, const DocumentValue& value)
{
    writeTextFile(path, serializeToml(value));
}

}  // namespace app::infrastructure::documents
