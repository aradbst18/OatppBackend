#pragma once

#include <memory>

#include "oatpp/parser/json/mapping/ObjectMapper.hpp"

namespace app::serialization
{

/// Build the application-wide JSON object mapper with range-checked
/// deserializers for every oatpp primitive (Int8..UInt64, Float32/64).
/// All controllers and tests must use this factory instead of a default
/// ObjectMapper so validation behavior is consistent.
std::shared_ptr<oatpp::parser::json::mapping::ObjectMapper> createAppObjectMapper();

}  // namespace app::serialization
