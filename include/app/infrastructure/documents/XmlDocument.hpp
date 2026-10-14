#pragma once

#include <string>

#include "app/infrastructure/documents/DocumentValue.hpp"

#ifdef APP_WITH_XML_PARSER
namespace app::infrastructure::documents
{

/**
 * Generic XML document layer (see JsonDocument for the pattern).
 *
 * Convention: an element with only text becomes a String; with children
 * becomes a Map (child name -> child DocumentValue, repeated names merge into a
 * List); attributes use "@name" keys and mixed text uses "#text".
 * A document is a Map with exactly one root entry, e.g. {"app": {...}},
 * so serializeXml requires a single-entry Map.
 */
DocumentValue parseXml(const std::string& text, const std::string& source);
std::string serializeXml(const DocumentValue& value);
DocumentValue loadXmlFile(const std::string& path);
void saveXmlFile(const std::string& path, const DocumentValue& value);

}  // namespace app::infrastructure::documents
#endif  // APP_WITH_XML_PARSER
