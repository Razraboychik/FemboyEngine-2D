#include "Engine/ECS/Scene.hpp"

#include "Engine/ECS/SceneField.hpp"

#include <toml.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <string>
#include <unordered_set>
#include <utility>

namespace Fby::Scene
{
namespace
{
using namespace Detail;

[[nodiscard]] std::string FieldPath(std::size_t InEntityIndex, std::string_view InComponent, std::string_view InField)
{
    return std::format("entity[{}].{}.{}", InEntityIndex, InComponent, InField);
}

void AppendFieldToml(std::string& Out, const SceneComponent& InComponent, const Reflect::FieldDesc& InField)
{
    Out += "  ";
    Out += InField.name;
    Out += " = ";
    switch (InField.kind)
    {
        case Reflect::FieldKind::Float:
            Out += FormatFloat(ReadFloat(InComponent.data.data(), InField));
            break;
        case Reflect::FieldKind::Int32:
            Out += std::to_string(ReadSigned(InComponent.data.data(), InField));
            break;
        case Reflect::FieldKind::UInt32:
            Out += std::to_string(ReadUnsigned(InComponent.data.data(), InField));
            break;
        case Reflect::FieldKind::Bool:
            Out += ReadUnsigned(InComponent.data.data(), InField) != 0 ? "true" : "false";
            break;
        case Reflect::FieldKind::Vector2D:
        {
            const Vector2D ScopedValue = ReadVec2(InComponent.data.data(), InField);
            Out += "[";
            Out += FormatFloat(ScopedValue.X);
            Out += ", ";
            Out += FormatFloat(ScopedValue.Y);
            Out += "]";
            break;
        }
        case Reflect::FieldKind::Color32:
            Out += FormatColor(ReadColor(InComponent.data.data(), InField));
            break;
        case Reflect::FieldKind::Entity:
        {
            const std::uint32_t ScopedIndex = ReadEntityIndex(InComponent.data.data(), InField);
            Out += ScopedIndex == kInvalidEntityIndex ? "-1" : std::to_string(ScopedIndex);
            break;
        }
        case Reflect::FieldKind::String:
            Out += '"';
            Out += EscapeTomlString(ReadString(InComponent.data.data(), InField));
            Out += '"';
            break;
        case Reflect::FieldKind::UInt64:
            Out += '"';
            Out += std::format("0x{:016X}", ReadU64(InComponent.data.data(), InField));
            Out += '"';
            break;
        case Reflect::FieldKind::Int128:
        {
            BigNum::Int128 ScopedValue;
            if (ReadInt128(InComponent.data.data(), InField, &ScopedValue))
            {
                Out += '"';
                Out += ScopedValue.ToString();
                Out += '"';
            }
            else
            {
                Out += '"';
                Out += '0';
                Out += '"';
            }
            break;
        }
        case Reflect::FieldKind::Decimal:
        {
            BigNum::Decimal128 ScopedValue;
            if (ReadDecimal(InComponent.data.data(), InField, &ScopedValue))
            {
                Out += '"';
                Out += ScopedValue.ToString();
                Out += '"';
            }
            else
            {
                Out += '"';
                Out += '0';
                Out += '"';
            }
            break;
        }
    }
    Out += '\n';
}

[[nodiscard]] bool ParseField(const toml::node& InNode, const Reflect::FieldDesc& InField, void* InComponent,
                              std::string* InError)
{
    switch (InField.kind)
    {
        case Reflect::FieldKind::Float:
        {
            const auto ScopedValue = InNode.value<double>();
            if (!ScopedValue)
            {
                *InError = "expected a float";
                return false;
            }
            if (!WriteFloat(InComponent, InField, static_cast<float>(*ScopedValue)))
            {
                *InError = "invalid float field size";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::Int32:
        {
            const auto ScopedValue = InNode.value<std::int64_t>();
            if (!ScopedValue)
            {
                *InError = "expected an integer";
                return false;
            }
            if (!WriteSigned(InComponent, InField, *ScopedValue))
            {
                *InError = "value is outside the field's range";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::UInt32:
        {
            const auto ScopedValue = InNode.value<std::int64_t>();
            if (!ScopedValue || *ScopedValue < 0)
            {
                *InError = "expected a non-negative integer";
                return false;
            }
            if (!WriteUnsigned(InComponent, InField, static_cast<std::uint64_t>(*ScopedValue)))
            {
                *InError = "value is outside the field's range";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::Bool:
        {
            const auto ScopedValue = InNode.value<bool>();
            if (!ScopedValue)
            {
                *InError = "expected a boolean";
                return false;
            }
            if (!WriteUnsigned(InComponent, InField, *ScopedValue ? 1 : 0))
            {
                *InError = "invalid bool field size";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::Vector2D:
        {
            const toml::array* ScopedArray = InNode.as_array();
            if (ScopedArray == nullptr || ScopedArray->size() != 2)
            {
                *InError = "expected a [x, y] array";
                return false;
            }
            const auto ScopedX = ScopedArray->get(0)->value<double>();
            const auto ScopedY = ScopedArray->get(1)->value<double>();
            if (!ScopedX || !ScopedY)
            {
                *InError = "expected two floats in [x, y]";
                return false;
            }
            if (!WriteVec2(InComponent, InField, Vector2D{static_cast<float>(*ScopedX), static_cast<float>(*ScopedY)}))
            {
                *InError = "invalid Vec2 field size";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::Color32:
        {
            if (InNode.is_string())
            {
                const auto ScopedText = InNode.value<std::string>();
                Color32 ScopedColor;
                if (!ScopedText || !ParseColorString(*ScopedText, ScopedColor))
                {
                    *InError = "expected a color string \"#RRGGBBAA\"";
                    return false;
                }
                if (!WriteColor(InComponent, InField, ScopedColor))
                {
                    *InError = "invalid Color32 field size";
                    return false;
                }
                return true;
            }
            const auto ScopedValue = InNode.value<std::int64_t>();
            if (!ScopedValue || *ScopedValue < 0 || *ScopedValue > 0xFFFFFFFFll)
            {
                *InError = "expected a color string or a 32-bit integer";
                return false;
            }
            if (!WriteColor(InComponent, InField, Color32{static_cast<std::uint32_t>(*ScopedValue)}))
            {
                *InError = "invalid Color32 field size";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::Entity:
        {
            const auto ScopedValue = InNode.value<std::int64_t>();
            if (!ScopedValue || *ScopedValue < -1 || *ScopedValue > static_cast<std::int64_t>(kInvalidEntityIndex) - 1)
            {
                *InError = "expected an entity index (or -1)";
                return false;
            }
            const std::uint32_t ScopedIndex =
                *ScopedValue < 0 ? kInvalidEntityIndex : static_cast<std::uint32_t>(*ScopedValue);
            if (!WriteEntityIndex(InComponent, InField, ScopedIndex))
            {
                *InError = "invalid Entity field size";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::String:
        {
            const auto ScopedValue = InNode.value<std::string>();
            if (!ScopedValue)
            {
                *InError = "expected a string";
                return false;
            }
            if (!WriteString(InComponent, InField, *ScopedValue))
            {
                *InError = "string is too long for its fixed-capacity field";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::Int128:
        {
            const auto ScopedText = InNode.value<std::string>();
            if (!ScopedText)
            {
                *InError = "expected a 128-bit integer string";
                return false;
            }
            BigNum::Int128 ScopedValue;
            if (BigNum::Int128::FromString(*ScopedText, &ScopedValue) != BigNum::Status::Ok)
            {
                *InError = "invalid 128-bit integer or value is out of range";
                return false;
            }
            if (!WriteInt128(InComponent, InField, ScopedValue))
            {
                *InError = "invalid Int128 field size";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::Decimal:
        {
            const auto ScopedText = InNode.value<std::string>();
            if (!ScopedText)
            {
                *InError = "expected a decimal string";
                return false;
            }
            BigNum::Decimal128 ScopedValue;
            if (BigNum::Decimal128::FromString(*ScopedText, &ScopedValue) != BigNum::Status::Ok)
            {
                *InError = "invalid decimal or value is out of range";
                return false;
            }
            if (!WriteDecimal(InComponent, InField, ScopedValue))
            {
                *InError = "invalid Decimal field size";
                return false;
            }
            return true;
        }
        case Reflect::FieldKind::UInt64:
        {
            std::uint64_t ScopedValue = 0;
            if (InNode.is_string())
            {
                const auto ScopedText = InNode.value<std::string>();
                if (!ScopedText)
                {
                    *InError = "expected a uint64 string";
                    return false;
                }
                std::string_view ScopedView(*ScopedText);
                int ScopedBase = 10;
                if (ScopedView.starts_with("0x") || ScopedView.starts_with("0X"))
                {
                    ScopedView.remove_prefix(2);
                    ScopedBase = 16;
                }
                const auto ScopedResult =
                    std::from_chars(ScopedView.data(), ScopedView.data() + ScopedView.size(), ScopedValue, ScopedBase);
                if (ScopedResult.ec != std::errc{} || ScopedResult.ptr != ScopedView.data() + ScopedView.size())
                {
                    *InError = "expected a decimal or 0x-hexadecimal uint64";
                    return false;
                }
            }
            else
            {
                const auto ScopedInteger = InNode.value<std::int64_t>();
                if (!ScopedInteger || *ScopedInteger < 0)
                {
                    *InError = "expected a non-negative uint64";
                    return false;
                }
                ScopedValue = static_cast<std::uint64_t>(*ScopedInteger);
            }
            if (!WriteU64(InComponent, InField, ScopedValue))
            {
                *InError = "invalid UInt64 field size";
                return false;
            }
            return true;
        }
    }
    *InError = "unknown field kind";
    return false;
}

[[nodiscard]] bool ValidateEntityIndices(const SceneData& InScene, std::string* InError)
{
    for (std::size_t ScopedEntityIndex = 0; ScopedEntityIndex < InScene.entities.size(); ++ScopedEntityIndex)
    {
        for (const SceneComponent& ScopedComponent : InScene.entities[ScopedEntityIndex].components)
        {
            if (ScopedComponent.type == nullptr)
            {
                continue;
            }
            for (const Reflect::FieldDesc& ScopedField : ScopedComponent.type->fields)
            {
                if (ScopedField.kind != Reflect::FieldKind::Entity)
                {
                    continue;
                }
                const std::uint32_t ScopedIndex = ReadEntityIndex(ScopedComponent.data.data(), ScopedField);
                if (ScopedIndex != kInvalidEntityIndex && ScopedIndex >= InScene.entities.size())
                {
                    *InError = FieldPath(ScopedEntityIndex, ScopedComponent.type->name, ScopedField.name) +
                               ": entity index " + std::to_string(ScopedIndex) + " is outside the scene (" +
                               std::to_string(InScene.entities.size()) + " entities)";
                    return false;
                }
            }
        }
    }
    return true;
}
}

std::string EncodeSceneToml(const SceneData& InScene)
{
    std::string ScopedOut;
    ScopedOut += "# FemboyEngine2D scene/prefab\n";
    ScopedOut += "# Generated by Fby::Scene - safe to edit by hand. Entity ids are local to this file.\n\n";
    ScopedOut += "version = ";
    ScopedOut += std::to_string(InScene.meta.version);
    ScopedOut += '\n';
    ScopedOut += "name = \"";
    ScopedOut += EscapeTomlString(InScene.meta.name);
    ScopedOut += "\"\n";
    if (!InScene.meta.author.empty())
    {
        ScopedOut += "author = \"";
        ScopedOut += EscapeTomlString(InScene.meta.author);
        ScopedOut += "\"\n";
    }

    for (std::size_t ScopedI = 0; ScopedI < InScene.entities.size(); ++ScopedI)
    {
        const SceneEntity& ScopedEntity = InScene.entities[ScopedI];
        ScopedOut += "\n[[entity]]\n";
        ScopedOut += "id = ";
        ScopedOut += std::to_string(ScopedI);
        ScopedOut += '\n';
        if (!ScopedEntity.name.empty())
        {
            ScopedOut += "name = \"";
            ScopedOut += EscapeTomlString(ScopedEntity.name);
            ScopedOut += "\"\n";
        }
        for (const SceneComponent& ScopedComponent : ScopedEntity.components)
        {
            if (ScopedComponent.type == nullptr)
            {
                continue;
            }
            ScopedOut += "\n[[entity.components]]\n";
            ScopedOut += "type = \"";
            ScopedOut += EscapeTomlString(ScopedComponent.type->name);
            ScopedOut += "\"\n";
            for (const Reflect::FieldDesc& ScopedField : ScopedComponent.type->fields)
            {
                AppendFieldToml(ScopedOut, ScopedComponent, ScopedField);
            }
        }
    }
    return ScopedOut;
}

std::expected<SceneData, std::string> DecodeSceneToml(std::string_view InToml, const ComponentRegistry& InComponents)
{
    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(InToml);
    }
    catch (const toml::parse_error& ScopedParseError)
    {
        return std::unexpected(std::string("TOML parse error: ") + std::string(ScopedParseError.description()));
    }

    SceneData ScopedScene;
    if (const auto ScopedVersion = ScopedRoot["version"].value<std::int64_t>())
    {
        if (*ScopedVersion <= 0 || *ScopedVersion > static_cast<std::int64_t>(kSceneVersion))
        {
            return std::unexpected("unsupported scene version " + std::to_string(*ScopedVersion) + " (engine writes " +
                                   std::to_string(kSceneVersion) + ")");
        }
        ScopedScene.meta.version = static_cast<std::uint32_t>(*ScopedVersion);
    }
    if (const auto ScopedName = ScopedRoot["name"].value<std::string>())
    {
        ScopedScene.meta.name = *ScopedName;
    }
    if (const auto ScopedAuthor = ScopedRoot["author"].value<std::string>())
    {
        ScopedScene.meta.author = *ScopedAuthor;
    }

    const toml::array* ScopedEntities = ScopedRoot["entity"].as_array();
    if (ScopedEntities == nullptr)
    {
        // An empty scene is valid
        return ScopedScene;
    }

    ScopedScene.entities.reserve(ScopedEntities->size());
    for (std::size_t ScopedEntityIndex = 0; ScopedEntityIndex < ScopedEntities->size(); ++ScopedEntityIndex)
    {
        const toml::table* ScopedEntityTable = ScopedEntities->get(ScopedEntityIndex)->as_table();
        if (ScopedEntityTable == nullptr)
        {
            return std::unexpected("entity[" + std::to_string(ScopedEntityIndex) + "] must be a table");
        }

        SceneEntity ScopedEntity;
        if (const auto ScopedName = (*ScopedEntityTable)["name"].value<std::string>())
        {
            ScopedEntity.name = *ScopedName;
        }

        const toml::array* ScopedComponentArray = (*ScopedEntityTable)["components"].as_array();
        if (ScopedComponentArray != nullptr)
        {
            std::unordered_set<std::string> ScopedSeen;
            for (std::size_t ScopedComponentIndex = 0; ScopedComponentIndex < ScopedComponentArray->size();
                 ++ScopedComponentIndex)
            {
                const toml::table* ScopedComponentTable = ScopedComponentArray->get(ScopedComponentIndex)->as_table();
                if (ScopedComponentTable == nullptr)
                {
                    return std::unexpected("entity[" + std::to_string(ScopedEntityIndex) + "].components[" +
                                           std::to_string(ScopedComponentIndex) + "] must be a table");
                }
                const auto ScopedType = (*ScopedComponentTable)["type"].value<std::string>();
                if (!ScopedType)
                {
                    return std::unexpected("entity[" + std::to_string(ScopedEntityIndex) + "].components[" +
                                           std::to_string(ScopedComponentIndex) + "] is missing 'type'");
                }
                if (!ScopedSeen.insert(*ScopedType).second)
                {
                    return std::unexpected("entity[" + std::to_string(ScopedEntityIndex) + "] has two '" + *ScopedType +
                                           "' components");
                }

                const ComponentOps* ScopedOps = InComponents.Find(*ScopedType);
                if (ScopedOps == nullptr)
                {
                    return std::unexpected("unknown component '" + *ScopedType + "' on entity[" +
                                           std::to_string(ScopedEntityIndex) + "]");
                }

                SceneComponent ScopedComponent;
                ScopedComponent.type = ScopedOps;
                ScopedComponent.data.assign(ScopedOps->size, std::byte{0});

                for (const Reflect::FieldDesc& ScopedField : ScopedOps->fields)
                {
                    const toml::node* ScopedNode = ScopedComponentTable->get(ScopedField.name);
                    if (ScopedNode == nullptr)
                    {
                        return std::unexpected(FieldPath(ScopedEntityIndex, ScopedOps->name, ScopedField.name) +
                                               ": field is missing");
                    }
                    std::string ScopedFieldError;
                    if (!ParseField(*ScopedNode, ScopedField, ScopedComponent.data.data(), &ScopedFieldError))
                    {
                        return std::unexpected(FieldPath(ScopedEntityIndex, ScopedOps->name, ScopedField.name) + ": " +
                                               ScopedFieldError);
                    }
                }
                ScopedEntity.components.push_back(std::move(ScopedComponent));
            }
        }
        ScopedScene.entities.push_back(std::move(ScopedEntity));
    }

    std::string ScopedValidationError;
    if (!ValidateEntityIndices(ScopedScene, &ScopedValidationError))
    {
        return std::unexpected(std::move(ScopedValidationError));
    }
    return ScopedScene;
}
}
