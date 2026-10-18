#pragma once

#include <string>

#include "app/infrastructure/documents/DocumentValue.hpp"

#ifdef APP_WITH_TOML_PARSER
namespace app::infrastructure::documents
{

/**
 * Generic TOML document layer (see JsonDocument for the pattern).
 * Tables become Maps, arrays become Lists, datetimes become ISO-8601
 * Strings. A document must be a table at the top level.
 */
DocumentValue parseToml(const std::string& text, const std::string& source);

std::string serializeToml(const DocumentValue& value);

DocumentValue loadTomlFile(const std::string& path);

void saveTomlFile(const std::string& path, const DocumentValue& value);

}  // namespace app::infrastructure::documents
#endif  // APP_WITH_TOML_PARSER
