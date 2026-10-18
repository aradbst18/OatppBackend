#include "app/infrastructure/documents/XmlDocument.hpp"

#ifndef APP_WITH_XML_PARSER
#error "XmlDocument.cpp requires APP_WITH_XML_PARSER (configure with -DAPP_ENABLE_XML_PARSER=ON)"
#endif  // !APP_WITH_XML_PARSER

#include <sstream>

#include <pugixml.hpp>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"

namespace app::infrastructure::documents
{

namespace
{

bool isElement(const pugi::xml_node& node)
{
    return node.type() == pugi::node_element;
}

DocumentValue fromXmlElement(const pugi::xml_node& element, const std::string& source)
{
    DocumentValue::Map out;

    for (const pugi::xml_attribute& attr : element.attributes())
    {
        out.emplace(std::string("@") + attr.name(), DocumentValue(attr.as_string("")));
    }

    const std::string text = element.text().as_string("");
    const bool hasText = !text.empty();

    DocumentValue::Map children;
    std::map<std::string, std::size_t> counts;
    for (const pugi::xml_node& child : element.children())
    {
        if (!isElement(child))
        {
            continue;
        }

        ++counts[child.name()];
    }

    std::map<std::string, DocumentValue::List> repeated;
    for (const pugi::xml_node& child : element.children())
    {
        if (!isElement(child))
        {
            continue;
        }

        const std::string name = child.name();
        DocumentValue childValue = fromXmlElement(child, source);
        if (counts[name] > 1)
        {
            repeated[name].push_back(std::move(childValue));
        }
        else
        {
            children.emplace(name, std::move(childValue));
        }
    }

    for (auto& [name, items] : repeated)
    {
        children.emplace(name, DocumentValue(std::move(items)));
    }

    for (auto& [key, item] : children)
    {
        out.emplace(std::move(key), std::move(item));
    }

    if (out.empty())
    {
        // Leaf element: plain text becomes a String (empty element -> "").
        (void)source;
        return DocumentValue(text);
    }

    if (hasText)
    {
        out.emplace("#text", DocumentValue(text));
    }

    return DocumentValue(std::move(out));
}

void appendXmlValue(pugi::xml_node& parent, const char* name, const DocumentValue& value, const std::string& source)
{
    switch (value.type())
    {
        case DocumentValue::Type::Null:
            parent.append_child(name);
            return;
        case DocumentValue::Type::Bool:
            parent.append_child(name).text().set(value.asBool() ? "true" : "false");
            return;
        case DocumentValue::Type::Int:
            parent.append_child(name).text().set(std::to_string(value.asInt()).c_str());
            return;
        case DocumentValue::Type::Double:
        {
            std::ostringstream text;
            text << value.asDouble();
            parent.append_child(name).text().set(text.str().c_str());
            return;
        }
        case DocumentValue::Type::String:
            parent.append_child(name).text().set(value.asString().c_str());
            return;
        case DocumentValue::Type::List:
        {
            for (const DocumentValue& item : value.asList())
            {
                appendXmlValue(parent, name, item, source);
            }

            return;
        }
        case DocumentValue::Type::Map:
        {
            pugi::xml_node element = parent.append_child(name);
            for (const auto& [key, item] : value.asMap())
            {
                if (key.rfind("@", 0) == 0 && key.size() > 1)
                {
                    if (!item.isString())
                    {
                        throw errors::serialization::SerializationFailed("XML attribute '" + key +
                                                                         "' must be a String for " + source);
                    }

                    element.append_attribute(key.substr(1).c_str()).set_value(item.asString().c_str());
                }
                else if (key == "#text")
                {
                    if (!item.isString())
                    {
                        throw errors::serialization::SerializationFailed("XML #text must be a String for " + source);
                    }

                    element.text().set(item.asString().c_str());
                }
                else
                {
                    appendXmlValue(element, key.c_str(), item, source);
                }
            }

            return;
        }
    }

    throw errors::serialization::SerializationFailed("Unsupported DocumentValue node for XML in " + source);
}

}  // namespace

DocumentValue parseXml(const std::string& text, const std::string& source)
{
    pugi::xml_document doc;
    const pugi::xml_parse_result ok = doc.load_string(text.c_str());
    if (!ok)
    {
        throw errors::serialization::ParsingFailed(std::string("Invalid XML in ") + source + ": " + ok.description());
    }

    pugi::xml_node root;
    for (const pugi::xml_node& child : doc.children())
    {
        if (isElement(child))
        {
            if (root)
            {
                throw errors::serialization::ParsingFailed("XML document must have exactly one root element in " +
                                                           source);
            }

            root = child;
        }
    }

    if (!root)
    {
        throw errors::serialization::ParsingFailed("XML document has no root element in " + source);
    }

    DocumentValue::Map document;
    document.emplace(root.name(), fromXmlElement(root, source));
    return DocumentValue(std::move(document));
}

std::string serializeXml(const DocumentValue& value)
{
    if (!value.isMap() || value.asMap().size() != 1)
    {
        throw errors::serialization::SerializationFailed("XML document must be a Map with exactly one root entry");
    }

    try
    {
        pugi::xml_document doc;
        auto decl = doc.prepend_child(pugi::node_declaration);
        decl.append_attribute("version") = "1.0";
        decl.append_attribute("encoding") = "UTF-8";

        const auto& [rootName, rootValue] = *value.asMap().begin();
        appendXmlValue(doc, rootName.c_str(), rootValue, "<value>");

        std::ostringstream out;
        doc.save(out, "  ");
        return out.str();
    }
    catch (const errors::serialization::SerializationFailed&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::serialization::SerializationFailed(std::string("Failed to encode XML: ") + ex.what());
    }
}

DocumentValue loadXmlFile(const std::string& path)
{
    return parseXml(readTextFile(path), path);
}

void saveXmlFile(const std::string& path, const DocumentValue& value)
{
    writeTextFile(path, serializeXml(value));
}

}  // namespace app::infrastructure::documents
