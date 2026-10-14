#include "app/serialization/AppObjectMapper.hpp"

#include <limits>

#include "app/serialization/RangeValidatingDeserializer.hpp"

namespace app::serialization
{

std::shared_ptr<oatpp::parser::json::mapping::ObjectMapper> createAppObjectMapper()
{
    auto deserializer = std::make_shared<oatpp::parser::json::mapping::Deserializer>();
    namespace cl = oatpp::data::mapping::type::__class;

    deserializer->setDeserializerMethod(
        cl::Int8::CLASS_ID,
        &deserializeSignedIntWithRange<oatpp::Int8, std::numeric_limits<std::int8_t>::lowest(), (std::numeric_limits<std::int8_t>::max)()>);

    deserializer->setDeserializerMethod(
        cl::UInt8::CLASS_ID,
        &deserializeUnsignedIntWithRange<oatpp::UInt8, std::numeric_limits<std::uint8_t>::lowest(), (std::numeric_limits<std::uint8_t>::max)()>);

    deserializer->setDeserializerMethod(
        cl::Int16::CLASS_ID,
        &deserializeSignedIntWithRange<oatpp::Int16, std::numeric_limits<std::int16_t>::lowest(), (std::numeric_limits<std::int16_t>::max)()>);

    deserializer->setDeserializerMethod(
        cl::UInt16::CLASS_ID,
        &deserializeUnsignedIntWithRange<oatpp::UInt16, std::numeric_limits<std::uint16_t>::lowest(), (std::numeric_limits<std::uint16_t>::max)()>);

    deserializer->setDeserializerMethod(
        cl::Int32::CLASS_ID,
        &deserializeSignedIntWithRange<oatpp::Int32, std::numeric_limits<std::int32_t>::lowest(), (std::numeric_limits<std::int32_t>::max)()>);

    deserializer->setDeserializerMethod(
        cl::UInt32::CLASS_ID,
        &deserializeUnsignedIntWithRange<oatpp::UInt32, std::numeric_limits<std::uint32_t>::lowest(), (std::numeric_limits<std::uint32_t>::max)()>);

    deserializer->setDeserializerMethod(
        cl::Int64::CLASS_ID,
        &deserializeSignedIntWithRange<oatpp::Int64, std::numeric_limits<std::int64_t>::lowest(), (std::numeric_limits<std::int64_t>::max)()>);

    deserializer->setDeserializerMethod(
        cl::UInt64::CLASS_ID,
        &deserializeUnsignedIntWithRange<oatpp::UInt64, std::numeric_limits<std::uint64_t>::lowest(), (std::numeric_limits<std::uint64_t>::max)()>);

    deserializer->setDeserializerMethod(cl::Float32::CLASS_ID, &deserializeFloatWithRange<oatpp::Float32, Float32Range>);

    deserializer->setDeserializerMethod(cl::Float64::CLASS_ID, &deserializeFloatWithRange<oatpp::Float64, Float64Range>);

    return std::make_shared<oatpp::parser::json::mapping::ObjectMapper>(std::make_shared<oatpp::parser::json::mapping::Serializer>(), deserializer);
}

}  // namespace app::serialization
