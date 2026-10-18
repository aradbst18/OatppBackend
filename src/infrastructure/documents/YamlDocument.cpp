#include "app/infrastructure/documents/YamlDocument.hpp"

#ifndef APP_WITH_YAML_PARSER
#error "YamlDocument.cpp requires APP_WITH_YAML_PARSER (configure with -DAPP_ENABLE_YAML_PARSER=ON)"
#endif  // !APP_WITH_YAML_PARSER

#include <cctype>

#include <yaml-cpp/yaml.h>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"

namespace app::infrastructure::documents
{

namespace
{

bool isIntegerLiteral(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }

    std::size_t pos = 0;
    if (text[pos] == '+' || text[pos] == '-')
    {
        ++pos;
    }

    if (pos == text.size())
    {
        return false;
    }

    for (; pos < text.size(); ++pos)
    {
        if (!std::isdigit(static_cast<unsigned char>(text[pos])))
        {
            return false;
        }
    }

    return true;
}

DocumentValue scalarFromString(const std::string& text)
{
    // NOTE: "" stays a String (explicit empty, e.g. path=""). Only the
    // named null literals become Null; a missing key is already Null before
    // reaching here (IsNull check in fromYamlNode).
    if (text == "null" || text == "Null" || text == "NULL" || text == "~")
    {
        return DocumentValue::null();
    }

    if (text == "true" || text == "True" || text == "TRUE")
    {
        return DocumentValue(true);
    }

    if (text == "false" || text == "False" || text == "FALSE")
    {
        return DocumentValue(false);
    }

    if (isIntegerLiteral(text))
    {
        try
        {
            return DocumentValue(static_cast<std::int64_t>(std::stoll(text)));
        }
        catch (const std::exception&)
        {
            return DocumentValue(text);
        }
    }

    try
    {
        std::size_t consumed = 0;
        const double number = std::stod(text, &consumed);
        if (consumed == text.size())
        {
            return DocumentValue(number);
        }
    }
    catch (const std::exception&)
    {
    }

    return DocumentValue(text);
}

DocumentValue fromYamlNode(const YAML::Node& node, const std::string& source)
{
    if (!node || node.IsNull())
    {
        return DocumentValue::null();
    }

    if (node.IsScalar())
    {
        try
        {
            return scalarFromString(node.as<std::string>());
        }
        catch (const YAML::Exception& ex)
        {
            throw errors::serialization::ParsingFailed("Invalid YAML scalar in " + source + ": " + ex.what());
        }
    }

    if (node.IsSequence())
    {
        DocumentValue::List out;
        out.reserve(node.size());
        for (const YAML::Node& item : node)
        {
            out.push_back(fromYamlNode(item, source));
        }

        return DocumentValue(std::move(out));
    }

    if (node.IsMap())
    {
        DocumentValue::Map out;
        for (const auto& entry : node)
        {
            if (!entry.first.IsScalar())
            {
                throw errors::serialization::ParsingFailed("YAML mapping keys must be scalars in " + source);
            }

            out.emplace(entry.first.as<std::string>(), fromYamlNode(entry.second, source));
        }

        return DocumentValue(std::move(out));
    }

    throw errors::serialization::ParsingFailed("Unsupported YAML node in " + source);
}

void emitYamlValue(YAML::Emitter& out, const DocumentValue& value, const std::string& source)
{
    switch (value.type())
    {
        case DocumentValue::Type::Null:
            out << YAML::Null;
            return;
        case DocumentValue::Type::Bool:
            out << value.asBool();
            return;
        case DocumentValue::Type::Int:
            out << value.asInt();
            return;
        case DocumentValue::Type::Double:
            out << value.asDouble();
            return;
        case DocumentValue::Type::String:
            out << value.asString();
            return;
        case DocumentValue::Type::List:
        {
            out << YAML::BeginSeq;
            for (const DocumentValue& item : value.asList())
            {
                emitYamlValue(out, item, source);
            }

            out << YAML::EndSeq;
            return;
        }
        case DocumentValue::Type::Map:
        {
            out << YAML::BeginMap;
            for (const auto& [key, item] : value.asMap())
            {
                out << YAML::Key << key << YAML::Value;
                emitYamlValue(out, item, source);
            }

            out << YAML::EndMap;
            return;
        }
    }

    throw errors::serialization::SerializationFailed("Unsupported DocumentValue node for YAML in " + source);
}

}  // namespace

DocumentValue parseYaml(const std::string& text, const std::string& source)
{
    try
    {
        return fromYamlNode(YAML::Load(text), source);
    }
    catch (const errors::serialization::ParsingFailed&)
    {
        throw;
    }
    catch (const YAML::Exception& ex)
    {
        throw errors::serialization::ParsingFailed(std::string("Invalid YAML in ") + source + ": " + ex.what());
    }
    catch (const std::exception& ex)
    {
        throw errors::serialization::ParsingFailed(std::string("Invalid YAML in ") + source + ": " + ex.what());
    }
}

std::string serializeYaml(const DocumentValue& value)
{
    try
    {
        YAML::Emitter out;
        emitYamlValue(out, value, "<value>");
        if (!out.good())
        {
            throw errors::serialization::SerializationFailed(std::string("Failed to encode YAML: ") +
                                                             out.GetLastError());
        }

        return std::string(out.c_str());
    }
    catch (const errors::serialization::SerializationFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::serialization::SerializationFailed(std::string("Failed to encode YAML: ") + ex.what());
    }
}

DocumentValue loadYamlFile(const std::string& path)
{
    return parseYaml(readTextFile(path), path);
}

void saveYamlFile(const std::string& path, const DocumentValue& value)
{
    writeTextFile(path, serializeYaml(value));
}

}  // namespace app::infrastructure::documents
