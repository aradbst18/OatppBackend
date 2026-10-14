#include "app/infrastructure/documents/JsonDocument.hpp"

#ifndef APP_WITH_JSON_PARSER
#error "JsonDocument.cpp requires APP_WITH_JSON_PARSER (configure with -DAPP_ENABLE_JSON_PARSER=ON)"
#endif  // !APP_WITH_JSON_PARSER

#include <sstream>

#include <json/json.h>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"

namespace app::infrastructure::documents
{

namespace
{

DocumentValue fromJsonValue(const Json::Value& node, const std::string& source)
{
    if (node.isNull())
    {
        return DocumentValue::null();
    }

    if (node.isBool())
    {
        return DocumentValue(node.asBool());
    }

    if (node.isInt() || node.isInt64())
    {
        return DocumentValue(node.asInt64());
    }

    if (node.isUInt() || node.isUInt64())
    {
        const Json::UInt64 raw = node.asUInt64();
        if (raw <= static_cast<Json::UInt64>(INT64_MAX))
        {
            return DocumentValue(static_cast<std::int64_t>(raw));
        }

        return DocumentValue(static_cast<double>(raw));
    }

    if (node.isDouble())
    {
        return DocumentValue(node.asDouble());
    }

    if (node.isString())
    {
        return DocumentValue(node.asString());
    }

    if (node.isArray())
    {
        DocumentValue::List out;
        out.reserve(static_cast<std::size_t>(node.size()));
        for (const Json::Value& item : node)
        {
            out.push_back(fromJsonValue(item, source));
        }

        return DocumentValue(std::move(out));
    }

    if (node.isObject())
    {
        DocumentValue::Map out;
        for (const std::string& key : node.getMemberNames())
        {
            out.emplace(key, fromJsonValue(node[key], source));
        }

        return DocumentValue(std::move(out));
    }

    throw errors::serialization::ParsingFailed("Unsupported JSON node in " + source);
}

Json::Value toJsonValue(const DocumentValue& value, const std::string& source)
{
    switch (value.type())
    {
        case DocumentValue::Type::Null:
            return Json::Value(Json::nullValue);
        case DocumentValue::Type::Bool:
            return Json::Value(value.asBool());
        case DocumentValue::Type::Int:
            return Json::Value(Json::Int64(value.asInt()));
        case DocumentValue::Type::Double:
            return Json::Value(value.asDouble());
        case DocumentValue::Type::String:
            return Json::Value(value.asString());
        case DocumentValue::Type::List:
        {
            Json::Value out(Json::arrayValue);
            for (const DocumentValue& item : value.asList())
            {
                out.append(toJsonValue(item, source));
            }

            return out;
        }
        case DocumentValue::Type::Map:
        {
            Json::Value out(Json::objectValue);
            for (const auto& [key, item] : value.asMap())
            {
                out[key] = toJsonValue(item, source);
            }

            return out;
        }
    }

    throw errors::serialization::SerializationFailed("Unsupported DocumentValue node for JSON in " + source);
}

}  // namespace

DocumentValue parseJson(const std::string& text, const std::string& source)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    std::string parseErrors;
    std::istringstream in(text);

    if (!Json::parseFromStream(builder, in, &root, &parseErrors))
    {
        throw errors::serialization::ParsingFailed("Invalid JSON in " + source + ": " + parseErrors);
    }

    try
    {
        return fromJsonValue(root, source);
    }
    catch (const errors::serialization::ParsingFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::serialization::ParsingFailed("Invalid JSON types in " + source + ": " + ex.what());
    }
}

std::string serializeJson(const DocumentValue& value)
{
    try
    {
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "  ";
        return Json::writeString(writer, toJsonValue(value, "<value>"));
    }
    catch (const errors::serialization::SerializationFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::serialization::SerializationFailed(std::string("Failed to encode JSON: ") + ex.what());
    }
}

DocumentValue loadJsonFile(const std::string& path)
{
    return parseJson(readTextFile(path), path);
}

void saveJsonFile(const std::string& path, const DocumentValue& value)
{
    writeTextFile(path, serializeJson(value));
}

}  // namespace app::infrastructure::documents
