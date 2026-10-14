#include "app/infrastructure/documents/DocumentValue.hpp"

#include "app/errors/AppException.hpp"

namespace app::infrastructure::documents
{

DocumentValue::DocumentValue() : data_(std::monostate{})
{
}

DocumentValue::DocumentValue(std::nullptr_t) : data_(std::monostate{})
{
}

DocumentValue::DocumentValue(bool value) : data_(value)
{
}

DocumentValue::DocumentValue(int value) : data_(static_cast<std::int64_t>(value))
{
}

DocumentValue::DocumentValue(long value) : data_(static_cast<std::int64_t>(value))
{
}

DocumentValue::DocumentValue(long long value) : data_(static_cast<std::int64_t>(value))
{
}

DocumentValue::DocumentValue(unsigned int value) : data_(static_cast<std::int64_t>(value))
{
}

DocumentValue::DocumentValue(unsigned long value) : data_(static_cast<std::int64_t>(value))
{
}

DocumentValue::DocumentValue(unsigned long long value) : data_(static_cast<std::int64_t>(value))
{
}

DocumentValue::DocumentValue(double value) : data_(value)
{
}

DocumentValue::DocumentValue(const char* value) : data_(value != nullptr ? std::string(value) : std::string())
{
}

DocumentValue::DocumentValue(const std::string& value) : data_(value)
{
}

DocumentValue::DocumentValue(std::string&& value) : data_(std::move(value))
{
}

DocumentValue::DocumentValue(const List& value) : data_(value)
{
}

DocumentValue::DocumentValue(List&& value) : data_(std::move(value))
{
}

DocumentValue::DocumentValue(const Map& value) : data_(value)
{
}

DocumentValue::DocumentValue(Map&& value) : data_(std::move(value))
{
}

DocumentValue DocumentValue::null()
{
    return DocumentValue();
}

DocumentValue DocumentValue::makeList()
{
    return DocumentValue(List{});
}

DocumentValue DocumentValue::makeMap()
{
    return DocumentValue(Map{});
}

DocumentValue::Type DocumentValue::type() const noexcept
{
    switch (data_.index())
    {
        case 1:
            return Type::Bool;
        case 2:
            return Type::Int;
        case 3:
            return Type::Double;
        case 4:
            return Type::String;
        case 5:
            return Type::List;
        case 6:
            return Type::Map;
        default:
            return Type::Null;
    }
}

bool DocumentValue::isNull() const noexcept
{
    return std::holds_alternative<std::monostate>(data_);
}

bool DocumentValue::isBool() const noexcept
{
    return std::holds_alternative<bool>(data_);
}

bool DocumentValue::isInt() const noexcept
{
    return std::holds_alternative<std::int64_t>(data_);
}

bool DocumentValue::isDouble() const noexcept
{
    return std::holds_alternative<double>(data_);
}

bool DocumentValue::isNumber() const noexcept
{
    return isInt() || isDouble();
}

bool DocumentValue::isString() const noexcept
{
    return std::holds_alternative<std::string>(data_);
}

bool DocumentValue::isList() const noexcept
{
    return std::holds_alternative<List>(data_);
}

bool DocumentValue::isMap() const noexcept
{
    return std::holds_alternative<Map>(data_);
}

bool DocumentValue::asBool() const
{
    if (const bool* value = std::get_if<bool>(&data_))
    {
        return *value;
    }

    throw errors::serialization::DeserializationFailed("DocumentValue is not a Bool");
}

std::int64_t DocumentValue::asInt() const
{
    if (const std::int64_t* value = std::get_if<std::int64_t>(&data_))
    {
        return *value;
    }

    if (const double* value = std::get_if<double>(&data_))
    {
        const double raw = *value;
        const auto coerced = static_cast<std::int64_t>(raw);
        if (static_cast<double>(coerced) == raw)
        {
            return coerced;
        }
    }

    throw errors::serialization::DeserializationFailed("DocumentValue is not an Int");
}

double DocumentValue::asDouble() const
{
    if (const double* value = std::get_if<double>(&data_))
    {
        return *value;
    }

    if (const std::int64_t* value = std::get_if<std::int64_t>(&data_))
    {
        return static_cast<double>(*value);
    }

    throw errors::serialization::DeserializationFailed("DocumentValue is not a number");
}

const std::string& DocumentValue::asString() const
{
    if (const std::string* value = std::get_if<std::string>(&data_))
    {
        return *value;
    }

    throw errors::serialization::DeserializationFailed("DocumentValue is not a String");
}

const DocumentValue::List& DocumentValue::asList() const
{
    if (const List* value = std::get_if<List>(&data_))
    {
        return *value;
    }

    throw errors::serialization::DeserializationFailed("DocumentValue is not a List");
}

DocumentValue::List& DocumentValue::asList()
{
    if (List* value = std::get_if<List>(&data_))
    {
        return *value;
    }

    throw errors::serialization::DeserializationFailed("DocumentValue is not a List");
}

const DocumentValue::Map& DocumentValue::asMap() const
{
    if (const Map* value = std::get_if<Map>(&data_))
    {
        return *value;
    }

    throw errors::serialization::DeserializationFailed("DocumentValue is not a Map");
}

DocumentValue::Map& DocumentValue::asMap()
{
    if (Map* value = std::get_if<Map>(&data_))
    {
        return *value;
    }

    throw errors::serialization::DeserializationFailed("DocumentValue is not a Map");
}

bool DocumentValue::contains(const std::string& key) const noexcept
{
    if (const Map* map = std::get_if<Map>(&data_))
    {
        return map->find(key) != map->end();
    }

    return false;
}

const DocumentValue* DocumentValue::find(const std::string& key) const noexcept
{
    if (const Map* map = std::get_if<Map>(&data_))
    {
        const auto it = map->find(key);
        if (it != map->end())
        {
            return &it->second;
        }
    }

    return nullptr;
}

DocumentValue* DocumentValue::find(const std::string& key) noexcept
{
    if (Map* map = std::get_if<Map>(&data_))
    {
        const auto it = map->find(key);
        if (it != map->end())
        {
            return &it->second;
        }
    }

    return nullptr;
}

const DocumentValue& DocumentValue::at(const std::string& key) const
{
    if (const DocumentValue* value = find(key))
    {
        return *value;
    }

    throw errors::serialization::DeserializationFailed("Missing key '" + key + "'");
}

DocumentValue& DocumentValue::operator[](const std::string& key)
{
    if (!isMap())
    {
        data_ = Map{};
    }

    return std::get<Map>(data_)[key];
}

DocumentValue& DocumentValue::operator[](const char* key)
{
    return operator[](std::string(key != nullptr ? key : ""));
}

std::size_t DocumentValue::size() const noexcept
{
    if (const List* list = std::get_if<List>(&data_))
    {
        return list->size();
    }

    if (const Map* map = std::get_if<Map>(&data_))
    {
        return map->size();
    }

    if (const std::string* text = std::get_if<std::string>(&data_))
    {
        return text->size();
    }

    return 0;
}

bool DocumentValue::empty() const noexcept
{
    if (const List* list = std::get_if<List>(&data_))
    {
        return list->empty();
    }

    if (const Map* map = std::get_if<Map>(&data_))
    {
        return map->empty();
    }

    if (const std::string* text = std::get_if<std::string>(&data_))
    {
        return text->empty();
    }

    return isNull();
}

bool DocumentValue::operator==(const DocumentValue& other) const noexcept
{
    return data_ == other.data_;
}

bool DocumentValue::operator!=(const DocumentValue& other) const noexcept
{
    return !(*this == other);
}

const DocumentValue* child(const DocumentValue& parent, const std::string& key) noexcept
{
    return parent.find(key);
}

std::string stringOr(const DocumentValue& parent, const std::string& key, const std::string& fallback)
{
    const DocumentValue* node = parent.find(key);
    if (node == nullptr || node->isNull())
    {
        return fallback;
    }

    return node->asString();
}

std::int64_t intOr(const DocumentValue& parent, const std::string& key, std::int64_t fallback)
{
    const DocumentValue* node = parent.find(key);
    if (node == nullptr || node->isNull())
    {
        return fallback;
    }

    return node->asInt();
}

}  // namespace app::infrastructure::documents
