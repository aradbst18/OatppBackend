#pragma once

#include <cstdint>
#include <limits>

#include "oatpp/core/Types.hpp"
#include "oatpp/core/parser/Caret.hpp"
#include "oatpp/parser/json/mapping/Deserializer.hpp"

namespace app::serialization
{

/**
 * Range-checked integer deserializers.
 *
 * Background: oatpp parses every JSON integer with a 64-bit routine and then
 * narrows it into the DTO field type. Without an explicit check, `64654646`
 * silently wraps when the field is an Int8. These functions install per-type
 * MIN/MAX guards so out-of-range input fails deserialization with a
 * "Value out of range" error (surfaced as HTTP 400) instead of wrapping.
 *
 * Registered for every oatpp integer/float type in AppObjectMapper.hpp.
 */
template <typename Wrapper, std::int64_t Min, std::int64_t Max>
oatpp::Void deserializeSignedIntWithRange(oatpp::parser::json::mapping::Deserializer* deserializer,
                                          oatpp::parser::Caret& caret, const oatpp::Type* const type)
{
    (void)deserializer;
    (void)type;

    if (caret.isAtText("null", true))
    {
        return Wrapper();
    }

    const std::int64_t value = caret.parseInt();
    if (caret.hasError())
    {
        return Wrapper();
    }

    if (value < Min || value > Max)
    {
        caret.setError("Value out of range");
        return Wrapper();
    }

    return Wrapper(static_cast<typename Wrapper::UnderlyingType>(value));
}

template <typename Wrapper, std::uint64_t Min, std::uint64_t Max>
oatpp::Void deserializeUnsignedIntWithRange(oatpp::parser::json::mapping::Deserializer* deserializer,
                                            oatpp::parser::Caret& caret, const oatpp::Type* const type)
{
    (void)deserializer;
    (void)type;

    if (caret.isAtText("null", true))
    {
        return Wrapper();
    }

    const std::uint64_t value = caret.parseUnsignedInt();
    if (caret.hasError())
    {
        return Wrapper();
    }

    if (value < Min || value > Max)
    {
        caret.setError("Value out of range");
        return Wrapper();
    }

    return Wrapper(static_cast<typename Wrapper::UnderlyingType>(value));
}

template <typename Wrapper, typename RangePolicy>
oatpp::Void deserializeFloatWithRange(oatpp::parser::json::mapping::Deserializer* deserializer,
                                      oatpp::parser::Caret& caret, const oatpp::Type* const type)
{
    (void)deserializer;
    (void)type;

    if (caret.isAtText("null", true))
    {
        return Wrapper();
    }

    const double value = caret.parseFloat64();
    if (caret.hasError())
    {
        return Wrapper();
    }

    if (value < RangePolicy::min() || value > RangePolicy::max())
    {
        caret.setError("Value out of range");
        return Wrapper();
    }

    return Wrapper(static_cast<typename Wrapper::UnderlyingType>(value));
}

struct Float32Range
{
    static constexpr double min()
    {
        return static_cast<double>(std::numeric_limits<float>::lowest());
    }

    static constexpr double max()
    {
        return static_cast<double>((std::numeric_limits<float>::max)());
    }
};

struct Float64Range
{
    static constexpr double min()
    {
        return std::numeric_limits<double>::lowest();
    }

    static constexpr double max()
    {
        return (std::numeric_limits<double>::max)();
    }
};

}  // namespace app::serialization
