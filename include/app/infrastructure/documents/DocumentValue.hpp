#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace app::infrastructure::documents
{

/**
 * Neutral in-memory document node shared by every text format.
 *
 * JSON, TOML, XML and YAML all parse into this tree and serialize back from
 * it, so future code can read/write arbitrary files without touching the
 * format libraries directly. AppSettings mapping is a thin layer on top
 * (see *ConfigStore): each store converts DocumentValue <-> AppSettings and adds
 * validation, legacy-key rejection and backend-specific rules.
 *
 * Type mapping per format (see *Document for details):
 * - JSON: null/bool/int/double/string/array/object map directly.
 * - TOML: table->Map, array->List, bool/int/double/string map directly,
 *   datetime->String (ISO-8601).
 * - YAML: null (~, null, empty)->Null, true/false->Bool, integers->Int,
 *   floats->Double, strings->String, sequences->List, maps->Map.
 * - XML: an element with only text becomes a String; with children becomes
 *   a Map (child name->child DocumentValue, repeated names merge into a List);
 *   attributes use "@name" keys, mixed text uses "#text". A document is a
 *   Map with exactly one root entry (e.g. {"app": {...}}).
 *
 * Wrong-type access throws errors::serialization::DeserializationFailed.
 * Missing-key access via at() throws the same; find()/contains() are the
 * non-throwing probes. operator[] on a mutable DocumentValue builds maps
 * (a Null turns into a Map on first write).
 */
class DocumentValue
{
public:
    enum class Type
    {
        Null,
        Bool,
        Int,
        Double,
        String,
        List,
        Map
    };

    using List = std::vector<DocumentValue>;
    using Map = std::map<std::string, DocumentValue>;

    DocumentValue();
    DocumentValue(std::nullptr_t);
    DocumentValue(bool value);
    DocumentValue(int value);
    DocumentValue(long value);
    DocumentValue(long long value);
    DocumentValue(unsigned int value);
    DocumentValue(unsigned long value);
    DocumentValue(unsigned long long value);
    DocumentValue(double value);
    DocumentValue(const char* value);
    DocumentValue(const std::string& value);
    DocumentValue(std::string&& value);
    DocumentValue(const List& value);
    DocumentValue(List&& value);
    DocumentValue(const Map& value);
    DocumentValue(Map&& value);

    static DocumentValue null();
    static DocumentValue makeList();
    static DocumentValue makeMap();

    Type type() const noexcept;
    bool isNull() const noexcept;
    bool isBool() const noexcept;
    bool isInt() const noexcept;
    bool isDouble() const noexcept;
    bool isNumber() const noexcept;
    bool isString() const noexcept;
    bool isList() const noexcept;
    bool isMap() const noexcept;

    bool asBool() const;
    std::int64_t asInt() const;
    double asDouble() const;
    const std::string& asString() const;
    const List& asList() const;
    List& asList();
    const Map& asMap() const;
    Map& asMap();

    bool contains(const std::string& key) const noexcept;
    const DocumentValue* find(const std::string& key) const noexcept;
    DocumentValue* find(const std::string& key) noexcept;
    const DocumentValue& at(const std::string& key) const;
    DocumentValue& operator[](const std::string& key);
    DocumentValue& operator[](const char* key);
    std::size_t size() const noexcept;
    bool empty() const noexcept;

    bool operator==(const DocumentValue& other) const noexcept;
    bool operator!=(const DocumentValue& other) const noexcept;

private:
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, List, Map>;
    Storage data_;
};

/**
 * Non-throwing child probe. Returns nullptr when parent is not a Map or the
 * key is absent. Never throws.
 */
const DocumentValue* child(const DocumentValue& parent, const std::string& key) noexcept;

/**
 * String field with fallback. Returns fallback when the key is absent;
 * throws errors::serialization::DeserializationFailed when present but not
 * a String.
 */
std::string stringOr(const DocumentValue& parent, const std::string& key, const std::string& fallback);

/**
 * Integer field with fallback. Accepts Int and integral Double values;
 * throws errors::serialization::DeserializationFailed on other types.
 */
std::int64_t intOr(const DocumentValue& parent, const std::string& key, std::int64_t fallback);

}  // namespace app::infrastructure::documents
