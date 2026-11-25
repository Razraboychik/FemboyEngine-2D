#include "Engine/Data/StructValueToml.hpp"

#include <toml.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstring>

namespace Fby::Data
{
namespace
{
void SetError(std::string* InError, std::string_view InMessage)
{
    if (InError != nullptr)
    {
        InError->assign(InMessage);
    }
}

[[nodiscard]] bool ParseU64(std::string_view InText, std::uint64_t* OutValue) noexcept
{
    std::uint64_t ScopedValue = 0;
    const auto ScopedResult = std::from_chars(InText.data(), InText.data() + InText.size(), ScopedValue);
    if (ScopedResult.ec != std::errc{} || ScopedResult.ptr != InText.data() + InText.size())
    {
        return false;
    }
    *OutValue = ScopedValue;
    return true;
}

[[nodiscard]] bool WriteField(std::string_view InName, Reflect::FieldKind InKind, const std::byte* InField,
                              std::uint32_t InSize, toml::table& OutTable, std::string* InError)
{
    switch (InKind)
    {
        case Reflect::FieldKind::Float:
        {
            if (InSize != sizeof(float))
            {
                break;
            }
            float ScopedValue = 0.0f;
            std::memcpy(&ScopedValue, InField, sizeof(ScopedValue));
            OutTable.insert(InName, static_cast<double>(ScopedValue));
            return true;
        }
        case Reflect::FieldKind::Int32:
        {
            if (InSize != sizeof(std::int32_t))
            {
                break;
            }
            std::int32_t ScopedValue = 0;
            std::memcpy(&ScopedValue, InField, sizeof(ScopedValue));
            OutTable.insert(InName, static_cast<std::int64_t>(ScopedValue));
            return true;
        }
        case Reflect::FieldKind::UInt32:
        {
            if (InSize != sizeof(std::uint32_t))
            {
                break;
            }
            std::uint32_t ScopedValue = 0;
            std::memcpy(&ScopedValue, InField, sizeof(ScopedValue));
            OutTable.insert(InName, static_cast<std::int64_t>(ScopedValue));
            return true;
        }
        case Reflect::FieldKind::Bool:
        {
            if (InSize != sizeof(bool))
            {
                break;
            }
            bool ScopedValue = false;
            std::memcpy(&ScopedValue, InField, sizeof(ScopedValue));
            OutTable.insert(InName, ScopedValue);
            return true;
        }
        case Reflect::FieldKind::Entity:
        case Reflect::FieldKind::Color32:
        {
            if (InSize != sizeof(std::uint32_t))
            {
                break;
            }
            std::uint32_t ScopedValue = 0;
            std::memcpy(&ScopedValue, InField, sizeof(ScopedValue));
            OutTable.insert(InName, static_cast<std::int64_t>(ScopedValue));
            return true;
        }
        case Reflect::FieldKind::UInt64:
        {
            if (InSize != sizeof(std::uint64_t))
            {
                break;
            }
            std::uint64_t ScopedValue = 0;
            std::memcpy(&ScopedValue, InField, sizeof(ScopedValue));
            OutTable.insert(InName, std::to_string(ScopedValue));
            return true;
        }
        case Reflect::FieldKind::Vector2D:
        {
            if (InSize != sizeof(Vector2D))
            {
                break;
            }
            Vector2D ScopedValue{};
            std::memcpy(&ScopedValue, InField, sizeof(ScopedValue));
            toml::array ScopedPair;
            ScopedPair.push_back(static_cast<double>(ScopedValue.X));
            ScopedPair.push_back(static_cast<double>(ScopedValue.Y));
            OutTable.insert(InName, std::move(ScopedPair));
            return true;
        }
        case Reflect::FieldKind::String:
        {
            if (InSize == 0)
            {
                break;
            }
            std::size_t ScopedLength = 0;
            while (ScopedLength < InSize && InField[ScopedLength] != std::byte{0})
            {
                ++ScopedLength;
            }
            OutTable.insert(InName,
                            std::string(reinterpret_cast<const char*>(InField), ScopedLength));
            return true;
        }
        default:
            SetError(InError, "struct payload field kind is not supported by TOML");
            return false;
    }
    SetError(InError, "struct payload field size does not match its kind");
    return false;
}

[[nodiscard]] bool ReadField(const toml::node& InNode, Reflect::FieldKind InKind, std::byte* OutField,
                             std::uint32_t InSize, std::string* InError)
{
    switch (InKind)
    {
        case Reflect::FieldKind::Float:
        {
            const auto ScopedValue = InNode.value<double>();
            if (!ScopedValue || InSize != sizeof(float))
            {
                break;
            }
            const float ScopedFloat = static_cast<float>(*ScopedValue);
            if (!std::isfinite(ScopedFloat))
            {
                SetError(InError, "struct payload float is not finite");
                return false;
            }
            std::memcpy(OutField, &ScopedFloat, sizeof(ScopedFloat));
            return true;
        }
        case Reflect::FieldKind::Int32:
        {
            const auto ScopedValue = InNode.value<std::int64_t>();
            if (!ScopedValue || InSize != sizeof(std::int32_t) || *ScopedValue < INT32_MIN || *ScopedValue > INT32_MAX)
            {
                break;
            }
            const std::int32_t ScopedInt = static_cast<std::int32_t>(*ScopedValue);
            std::memcpy(OutField, &ScopedInt, sizeof(ScopedInt));
            return true;
        }
        case Reflect::FieldKind::UInt32:
        case Reflect::FieldKind::Entity:
        case Reflect::FieldKind::Color32:
        {
            const auto ScopedValue = InNode.value<std::int64_t>();
            if (!ScopedValue || InSize != sizeof(std::uint32_t) || *ScopedValue < 0 || *ScopedValue > UINT32_MAX)
            {
                break;
            }
            const std::uint32_t ScopedUInt = static_cast<std::uint32_t>(*ScopedValue);
            std::memcpy(OutField, &ScopedUInt, sizeof(ScopedUInt));
            return true;
        }
        case Reflect::FieldKind::Bool:
        {
            const auto ScopedValue = InNode.value<bool>();
            if (!ScopedValue || InSize != sizeof(bool))
            {
                break;
            }
            std::memcpy(OutField, &*ScopedValue, sizeof(bool));
            return true;
        }
        case Reflect::FieldKind::UInt64:
        {
            std::uint64_t ScopedValue = 0;
            if (InSize != sizeof(std::uint64_t))
            {
                break;
            }
            if (const auto ScopedString = InNode.value<std::string>(); ScopedString.has_value())
            {
                if (!ParseU64(*ScopedString, &ScopedValue))
                {
                    break;
                }
            }
            else if (const auto ScopedInt = InNode.value<std::int64_t>(); ScopedInt.has_value() && *ScopedInt >= 0)
            {
                ScopedValue = static_cast<std::uint64_t>(*ScopedInt);
            }
            else
            {
                break;
            }
            std::memcpy(OutField, &ScopedValue, sizeof(ScopedValue));
            return true;
        }
        case Reflect::FieldKind::Vector2D:
        {
            const toml::array* ScopedArray = InNode.as_array();
            if (ScopedArray == nullptr || ScopedArray->size() != 2 || InSize != sizeof(Vector2D))
            {
                break;
            }
            const auto ScopedX = (*ScopedArray)[0].value<double>();
            const auto ScopedY = (*ScopedArray)[1].value<double>();
            if (!ScopedX || !ScopedY || !std::isfinite(*ScopedX) || !std::isfinite(*ScopedY))
            {
                break;
            }
            Vector2D ScopedValue{static_cast<float>(*ScopedX), static_cast<float>(*ScopedY)};
            std::memcpy(OutField, &ScopedValue, sizeof(ScopedValue));
            return true;
        }
        case Reflect::FieldKind::String:
        {
            const auto ScopedValue = InNode.value<std::string>();
            if (!ScopedValue || ScopedValue->size() >= InSize)
            {
                break;
            }
            std::memset(OutField, 0, InSize);
            std::memcpy(OutField, ScopedValue->data(), ScopedValue->size());
            return true;
        }
        default:
            SetError(InError, "struct payload field kind is not supported by TOML");
            return false;
    }
    SetError(InError, "struct payload field value has the wrong type or size");
    return false;
}
}

bool EncodeStructPayloadToml(const StructPayload& InPayload, const StructTypeRegistry& InRegistry,
                             toml::array& OutValues, std::string* InError)
{
    if (InPayload.Count > kStructPayloadMaxValues)
    {
        SetError(InError, "struct payload count is out of range");
        return false;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InPayload.Count; ++ScopedIndex)
    {
        const StructValue& ScopedValue = InPayload.Values[ScopedIndex];
        if (ScopedValue.Empty())
        {
            SetError(InError, "struct payload contains an empty value");
            return false;
        }
        const StructType* ScopedType = InRegistry.Find(ScopedValue.Type());
        if (ScopedType == nullptr)
        {
            SetError(InError, "struct payload type is not registered");
            return false;
        }
        if (ScopedType->SchemaHash != ScopedValue.SchemaHash() || ScopedType->Size != ScopedValue.Size())
        {
            SetError(InError, "struct payload schema does not match the registered type");
            return false;
        }
        toml::table ScopedTable;
        ScopedTable.insert("type", std::string(ScopedType->Name));
        ScopedTable.insert("schema", std::to_string(ScopedType->SchemaHash));
        toml::table ScopedFields;
        for (const StructField& ScopedField : ScopedType->Fields)
        {
            if (ScopedField.Offset > ScopedValue.Size() || ScopedField.Size > ScopedValue.Size() - ScopedField.Offset)
            {
                SetError(InError, "struct payload field is outside the value");
                return false;
            }
            if (!WriteField(ScopedField.Name, ScopedField.Kind, ScopedValue.Bytes().data() + ScopedField.Offset,
                            ScopedField.Size, ScopedFields, InError))
            {
                return false;
            }
        }
        ScopedTable.insert("fields", std::move(ScopedFields));
        OutValues.push_back(std::move(ScopedTable));
    }
    return true;
}

bool DecodeStructPayloadToml(const toml::array& InValues, const StructTypeRegistry& InRegistry,
                             StructPayload* OutPayload, std::string* InError)
{
    if (OutPayload == nullptr || InValues.size() > kStructPayloadMaxValues)
    {
        SetError(InError, "struct payload TOML array is too large");
        return false;
    }
    StructPayload ScopedPayload;
    for (const toml::node& ScopedNode : InValues)
    {
        const toml::table* ScopedTable = ScopedNode.as_table();
        if (ScopedTable == nullptr)
        {
            SetError(InError, "struct payload entry must be a table");
            return false;
        }
        const auto ScopedTypeName = (*ScopedTable)["type"].value<std::string>();
        const auto ScopedSchemaText = (*ScopedTable)["schema"].value<std::string>();
        if (!ScopedTypeName || !ScopedSchemaText)
        {
            SetError(InError, "struct payload entry needs type and schema");
            return false;
        }
        std::uint64_t ScopedSchema = 0;
        if (!ParseU64(*ScopedSchemaText, &ScopedSchema))
        {
            SetError(InError, "struct payload schema is not a number");
            return false;
        }
        const StructType* ScopedType = InRegistry.Find(*ScopedTypeName);
        if (ScopedType == nullptr || ScopedType->SchemaHash != ScopedSchema)
        {
            SetError(InError, "struct payload type or schema is not registered");
            return false;
        }
        const toml::table* ScopedFields = (*ScopedTable)["fields"].as_table();
        if (ScopedFields == nullptr)
        {
            SetError(InError, "struct payload entry needs a fields table");
            return false;
        }
        std::array<std::byte, kStructValueBytes> ScopedBytes{};
        for (const StructField& ScopedField : ScopedType->Fields)
        {
            const toml::node* ScopedFieldNode = (*ScopedFields)[ScopedField.Name].node();
            if (ScopedFieldNode == nullptr)
            {
                SetError(InError, "struct payload field is missing");
                return false;
            }
            if (!ReadField(*ScopedFieldNode, ScopedField.Kind, ScopedBytes.data() + ScopedField.Offset,
                           ScopedField.Size, InError))
            {
                return false;
            }
        }
        if (!ScopedPayload.AddRaw(ScopedType->Id, ScopedType->SchemaHash,
                                  std::span<const std::byte>(ScopedBytes.data(), ScopedType->Size)))
        {
            SetError(InError, "struct payload is full or the value is invalid");
            return false;
        }
    }
    *OutPayload = ScopedPayload;
    return true;
}
}
