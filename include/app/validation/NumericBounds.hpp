#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

namespace app::validation
{

/**
 * Closed numeric range used both for documentation and runtime checks.
 * Mirrors the limits enforced by the oatpp deserializers in
 * serialization/RangeValidatingDeserializer.hpp so unit tests can assert the
 * contract without involving JSON parsing.
 */
template <typename T>
struct NumericBounds
{
    static_assert(std::is_arithmetic<T>::value, "NumericBounds requires an arithmetic type");

    T min, max;
    constexpr bool contains(T value) const noexcept
    {
        return value >= min && value <= max;
    }
};

template <typename T>
constexpr NumericBounds<T> boundsFor() noexcept;

template <>
constexpr NumericBounds<std::int8_t> boundsFor<std::int8_t>() noexcept
{
    return {std::numeric_limits<std::int8_t>::lowest(), std::numeric_limits<std::int8_t>::max()};
}

template <>
constexpr NumericBounds<std::uint8_t> boundsFor<std::uint8_t>() noexcept
{
    return {std::numeric_limits<std::uint8_t>::lowest(), std::numeric_limits<std::uint8_t>::max()};
}

template <>
constexpr NumericBounds<std::int16_t> boundsFor<std::int16_t>() noexcept
{
    return {std::numeric_limits<std::int16_t>::lowest(), std::numeric_limits<std::int16_t>::max()};
}

template <>
constexpr NumericBounds<std::uint16_t> boundsFor<std::uint16_t>() noexcept
{
    return {std::numeric_limits<std::uint16_t>::lowest(), std::numeric_limits<std::uint16_t>::max()};
}

template <>
constexpr NumericBounds<std::int32_t> boundsFor<std::int32_t>() noexcept
{
    return {std::numeric_limits<std::int32_t>::lowest(), std::numeric_limits<std::int32_t>::max()};
}

template <>
constexpr NumericBounds<std::uint32_t> boundsFor<std::uint32_t>() noexcept
{
    return {std::numeric_limits<std::uint32_t>::lowest(), std::numeric_limits<std::uint32_t>::max()};
}

template <>
constexpr NumericBounds<std::int64_t> boundsFor<std::int64_t>() noexcept
{
    return {std::numeric_limits<std::int64_t>::lowest(), std::numeric_limits<std::int64_t>::max()};
}

template <>
constexpr NumericBounds<std::uint64_t> boundsFor<std::uint64_t>() noexcept
{
    return {std::numeric_limits<std::uint64_t>::lowest(), std::numeric_limits<std::uint64_t>::max()};
}

template <>
constexpr NumericBounds<float> boundsFor<float>() noexcept
{
    return {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()};
}

template <>
constexpr NumericBounds<double> boundsFor<double>() noexcept
{
    return {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max()};
}

/// Check a 64-bit candidate against the bounds of T without narrowing first.
/// Used to validate raw JSON integers before they are stored in a DTO field.
template <typename T>
bool fitsInBounds(std::int64_t candidate) noexcept
{
    const auto b = boundsFor<T>();
    // Compare in a wide type to avoid overflow on narrowing.
    auto lo = static_cast<std::int64_t>(b.min);
    auto hi = static_cast<std::int64_t>(b.max);

    if constexpr (std::is_unsigned<T>::value)
    {
        if (candidate < 0)
        {
            return false;
        }

        // For 32-bit unsigned and below the int64 comparison is exact.
        if constexpr (sizeof(T) < sizeof(std::uint64_t))
        {
            return candidate <= hi;
        }
        else
        {
            return true;  // uint64 handled by fitsUnsignedInBounds
        }
    }
    else
    {
        if constexpr (sizeof(T) >= sizeof(std::int64_t))
        {
            (void)lo;
            (void)hi;

            return true;
        }
        else
        {
            return candidate >= lo && candidate <= hi;
        }
    }
}

/// Unsigned variant for values that may exceed INT64_MAX.
template <typename T>
bool fitsUnsignedInBounds(std::uint64_t candidate) noexcept
{
    const auto b = boundsFor<T>();
    if constexpr (std::is_signed<T>::value)
    {
        auto hi = static_cast<std::uint64_t>(b.max);
        return candidate <= hi;
    }
    else
    {
        auto hi = static_cast<std::uint64_t>(b.max);
        auto lo = static_cast<std::uint64_t>(b.min);
        return candidate >= lo && candidate <= hi;
    }
}

}  // namespace app::validation
