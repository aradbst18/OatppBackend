#pragma once

#include <string>

#include "app/infrastructure/documents/DocumentValue.hpp"

#ifdef APP_WITH_YAML_PARSER
namespace app::infrastructure::documents
{

/**
 * Generic YAML document layer (see JsonDocument for the pattern).
 * Scalars infer Null/Bool/Int/Double/String; sequences become Lists and
 * mappings become Maps. An empty document parses as Null.
 */
DocumentValue parseYaml(const std::string& text, const std::string& source);

std::string serializeYaml(const DocumentValue& value);

DocumentValue loadYamlFile(const std::string& path);

void saveYamlFile(const std::string& path, const DocumentValue& value);

}  // namespace app::infrastructure::documents
#endif  // APP_WITH_YAML_PARSER
