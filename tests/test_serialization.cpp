#include "support/TestRunner.hpp"

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"

#include "app/serialization/AppObjectMapper.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

class RangeTestDto : public oatpp::DTO
{
    DTO_INIT(RangeTestDto, DTO /* Extends */)
    DTO_FIELD(Int8, small);
    DTO_FIELD(UInt8, tiny);
    DTO_FIELD(Int16, medium);
    DTO_FIELD(String, name);
};

#include OATPP_CODEGEN_END(DTO)

namespace
{

oatpp::Object<RangeTestDto> parse(const std::shared_ptr<oatpp::parser::json::mapping::ObjectMapper>& mapper,
                                  const std::string& json, bool& error)
{
    try
    {
        auto obj = mapper->readFromString<oatpp::Object<RangeTestDto>>(json);
        error = false;

        return obj;
    }
    catch (...)
    {
        error = true;

        return nullptr;
    }
}

}  // namespace

TEST_CASE("serialization: valid values parse")
{
    auto mapper = app::serialization::createAppObjectMapper();
    bool error = true;

    auto dto = parse(mapper, R"({"small":100,"tiny":200,"medium":300,"name":"ok"})", error);

    EXPECT_FALSE(error);
    EXPECT_TRUE(dto);
    EXPECT_EQ(static_cast<int>(*dto->small), 100);
}

TEST_CASE("serialization: Int8 rejects 64654646")
{
    auto mapper = app::serialization::createAppObjectMapper();
    bool error = false;

    auto dto = parse(mapper, R"({"small":64654646})", error);

    EXPECT_TRUE(error);
    EXPECT_TRUE(!dto);
}

TEST_CASE("serialization: Int8 rejects boundary overflow")
{
    auto mapper = app::serialization::createAppObjectMapper();
    bool error = false;

    parse(mapper, R"({"small":128})", error);
    EXPECT_TRUE(error);

    parse(mapper, R"({"small":-129})", error);
    EXPECT_TRUE(error);

    parse(mapper, R"({"small":127})", error);
    EXPECT_FALSE(error);
}

TEST_CASE("serialization: UInt8 rejects negative")
{
    auto mapper = app::serialization::createAppObjectMapper();
    bool error = false;

    parse(mapper, R"({"tiny":-1})", error);
    EXPECT_TRUE(error);

    parse(mapper, R"({"tiny":256})", error);
    EXPECT_TRUE(error);
}

TEST_CASE("serialization: malformed json errors")
{
    auto mapper = app::serialization::createAppObjectMapper();
    bool error = false;

    parse(mapper, R"({"small":)", error);

    EXPECT_TRUE(error);
}

TEST_CASE("serialization: missing optional fields are null")
{
    auto mapper = app::serialization::createAppObjectMapper();
    bool error = true;

    auto dto = parse(mapper, R"({})", error);

    EXPECT_FALSE(error);
    EXPECT_TRUE(dto);
    EXPECT_TRUE(!dto->small);
    EXPECT_TRUE(!dto->name);
}

TEST_CASE("serialization: null values accepted as empty")
{
    auto mapper = app::serialization::createAppObjectMapper();
    bool error = true;

    auto dto = parse(mapper, R"({"small":null})", error);

    EXPECT_FALSE(error);
    EXPECT_TRUE(dto);
    EXPECT_TRUE(!dto->small);
}
