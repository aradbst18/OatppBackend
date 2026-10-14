#pragma once

#include <string>

#include "app/infrastructure/documents/DocumentValue.hpp"

#ifdef APP_WITH_JSON_PARSER
namespace app::infrastructure::documents
{

/**
 * Generic JSON document layer. Parses any JSON text/file into the neutral
 * DocumentValue tree and serializes back, independent of AppSettings. Errors use
 * errors::serialization::ParsingFailed / SerializationFailed with the
 * originating source path embedded.
 */
DocumentValue parseJson(const std::string& text, const std::string& source);
std::string serializeJson(const DocumentValue& value);
DocumentValue loadJsonFile(const std::string& path);
void saveJsonFile(const std::string& path, const DocumentValue& value);

}  // namespace app::infrastructure::documents
#endif  // APP_WITH_JSON_PARSER
