// FemboyEngine2D - developer-authored settings schema and value store.
//
// The registry is a constructor, not a fixed list of engine settings. A game
// registers categories, typed descriptors and apply callbacks once, freezes the
// schema, then loads project defaults and per-install user overrides. The
// registry never decides which categories, colours or defaults a game needs.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Fby::Settings
{

enum class Scope : std::uint8_t
{
    Project = 0,
    User = 1,
    Session = 2,
};

enum class Type : std::uint8_t
{
    Bool = 0,
    Int = 1,
    Float = 2,
    Color = 3,
    String = 4,
    Enum = 5,
};

struct Color
{
    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
    float A = 1.0f;

    [[nodiscard]] friend bool operator==(const Color&, const Color&) noexcept = default;
};

struct Value
{
    Type Kind = Type::Bool;
    bool BoolValue = false;
    std::int64_t IntValue = 0;
    double FloatValue = 0.0;
    Color ColorValue{};
    std::string StringValue;
    std::string EnumValue;

    [[nodiscard]] static Value MakeBool(bool InValue) noexcept
    {
        Value Scoped;
        Scoped.Kind = Type::Bool;
        Scoped.BoolValue = InValue;
        return Scoped;
    }
    [[nodiscard]] static Value MakeInt(std::int64_t InValue) noexcept
    {
        Value Scoped;
        Scoped.Kind = Type::Int;
        Scoped.IntValue = InValue;
        return Scoped;
    }
    [[nodiscard]] static Value MakeFloat(double InValue) noexcept
    {
        Value Scoped;
        Scoped.Kind = Type::Float;
        Scoped.FloatValue = InValue;
        return Scoped;
    }
    [[nodiscard]] static Value MakeColor(const Color& InValue) noexcept
    {
        Value Scoped;
        Scoped.Kind = Type::Color;
        Scoped.ColorValue = InValue;
        return Scoped;
    }
    [[nodiscard]] static Value MakeString(std::string InValue)
    {
        Value Scoped;
        Scoped.Kind = Type::String;
        Scoped.StringValue = std::move(InValue);
        return Scoped;
    }
    [[nodiscard]] static Value MakeEnum(std::string InValue)
    {
        Value Scoped;
        Scoped.Kind = Type::Enum;
        Scoped.EnumValue = std::move(InValue);
        return Scoped;
    }

    [[nodiscard]] friend bool operator==(const Value&, const Value&) noexcept = default;
};

enum Flags : std::uint32_t
{
    FlagNone = 0,
    FlagUserEditable = 1u << 0,
    FlagRequiresRestart = 1u << 1,
    FlagDangerous = 1u << 2,
    FlagRequiresConfirmation = 1u << 3,
    FlagAdvanced = 1u << 4,
    FlagHidden = 1u << 5,
    FlagServerAuthoritative = 1u << 6,
};

struct Descriptor
{
    std::string Id;
    std::string Category;
    std::string Label;
    std::string LocalizationKey;
    std::string Tooltip;
    std::string Unit;
    std::string ConfirmationText;
    std::string RecoveryHotkey;
    Type Kind = Type::Bool;
    Scope DefaultScope = Scope::Project;
    Value DefaultValue{};
    Value SafeValue{};
    double Minimum = -std::numeric_limits<double>::infinity();
    double Maximum = std::numeric_limits<double>::infinity();
    std::uint32_t Flags = FlagNone;
    std::vector<std::string> EnumValues;
    std::function<void(const Value&)> Apply;

    [[nodiscard]] bool IsUserEditable() const noexcept
    {
        return (Flags & FlagUserEditable) != 0u;
    }
    [[nodiscard]] bool IsDangerous() const noexcept
    {
        return (Flags & FlagDangerous) != 0u;
    }
};

class Registry
{
public:
    // Immutable hot-path view: descriptor order and values are fixed at creation.
    // Runtime systems read by integer index instead of hashing a string per frame.
    class Snapshot
    {
    public:
        [[nodiscard]] std::size_t Size() const noexcept
        {
            return MValues.size();
        }
        [[nodiscard]] const Value& At(std::size_t InIndex) const noexcept
        {
            static const Value ScopedEmpty{};
            return InIndex < MValues.size() ? MValues[InIndex] : ScopedEmpty;
        }
        [[nodiscard]] bool Bool(std::size_t InIndex, bool InFallback = false) const noexcept
        {
            const Value& Scoped = At(InIndex);
            return Scoped.Kind == Type::Bool ? Scoped.BoolValue : InFallback;
        }
        [[nodiscard]] double Float(std::size_t InIndex, double InFallback = 0.0) const noexcept
        {
            const Value& Scoped = At(InIndex);
            return Scoped.Kind == Type::Float ? Scoped.FloatValue : InFallback;
        }
        [[nodiscard]] const Color& ColorValue(std::size_t InIndex) const noexcept
        {
            static const Color ScopedEmpty{};
            const Value& Scoped = At(InIndex);
            return Scoped.Kind == Type::Color ? Scoped.ColorValue : ScopedEmpty;
        }

    private:
        friend class Registry;
        explicit Snapshot(std::vector<Value> InValues) : MValues(std::move(InValues))
        {
        }
        std::vector<Value> MValues;
    };

    struct Builder
    {
        Registry* Owner = nullptr;
        std::size_t Index = 0;

        Builder& Category(std::string InCategory)
        {
            Owner->MDescriptors[Index].Category = std::move(InCategory);
            return *this;
        }
        Builder& Label(std::string InLabel)
        {
            Owner->MDescriptors[Index].Label = std::move(InLabel);
            return *this;
        }
        Builder& LocalizationKey(std::string InKey)
        {
            Owner->MDescriptors[Index].LocalizationKey = std::move(InKey);
            return *this;
        }
        Builder& Tooltip(std::string InTooltip)
        {
            Owner->MDescriptors[Index].Tooltip = std::move(InTooltip);
            return *this;
        }
        Builder& Unit(std::string InUnit)
        {
            Owner->MDescriptors[Index].Unit = std::move(InUnit);
            return *this;
        }
        Builder& Default(Value InValue)
        {
            Owner->MDescriptors[Index].DefaultValue = std::move(InValue);
            return *this;
        }
        Builder& Safe(Value InValue)
        {
            Owner->MDescriptors[Index].SafeValue = std::move(InValue);
            return *this;
        }
        Builder& Scope(Scope InScope)
        {
            Owner->MDescriptors[Index].DefaultScope = InScope;
            if (InScope == Settings::Scope::User)
            {
                Owner->MDescriptors[Index].Flags |= FlagUserEditable;
            }
            return *this;
        }
        Builder& Range(double InMinimum, double InMaximum)
        {
            Owner->MDescriptors[Index].Minimum = InMinimum;
            Owner->MDescriptors[Index].Maximum = InMaximum;
            return *this;
        }
        Builder& Enum(std::span<const std::string_view> InValues)
        {
            auto& Scoped = Owner->MDescriptors[Index].EnumValues;
            Scoped.clear();
            for (const std::string_view ScopedValue : InValues)
            {
                Scoped.emplace_back(ScopedValue);
            }
            return *this;
        }
        Builder& Flags(std::uint32_t InFlags)
        {
            Owner->MDescriptors[Index].Flags |= InFlags;
            return *this;
        }
        Builder& UserEditable()
        {
            Owner->MDescriptors[Index].Flags |= FlagUserEditable;
            return *this;
        }
        Builder& Advanced()
        {
            Owner->MDescriptors[Index].Flags |= FlagAdvanced;
            return *this;
        }
        Builder& Dangerous(std::string InConfirmationText, std::string InRecoveryHotkey)
        {
            Owner->MDescriptors[Index].Flags |= FlagDangerous | FlagRequiresConfirmation;
            Owner->MDescriptors[Index].ConfirmationText = std::move(InConfirmationText);
            Owner->MDescriptors[Index].RecoveryHotkey = std::move(InRecoveryHotkey);
            return *this;
        }
        Builder& Apply(std::function<void(const Value&)> InApply)
        {
            Owner->MDescriptors[Index].Apply = std::move(InApply);
            return *this;
        }
    };

    Builder Bool(std::string InId)
    {
        return Add(std::move(InId), Type::Bool, Value::MakeBool(false));
    }
    Builder Int(std::string InId)
    {
        return Add(std::move(InId), Type::Int, Value::MakeInt(0));
    }
    Builder Float(std::string InId)
    {
        return Add(std::move(InId), Type::Float, Value::MakeFloat(0.0));
    }
    Builder ColorSetting(std::string InId)
    {
        return Add(std::move(InId), Type::Color, Value::MakeColor(Color{}));
    }
    Builder ColorRole(std::string InId)
    {
        Builder Scoped = Add(std::move(InId), Type::Color, Value::MakeColor(Color{}));
        Scoped.Category("Theme");
        return Scoped;
    }
    Builder String(std::string InId)
    {
        return Add(std::move(InId), Type::String, Value::MakeString({}));
    }
    Builder Enum(std::string InId)
    {
        return Add(std::move(InId), Type::Enum, Value::MakeEnum({}));
    }

    [[nodiscard]] bool Freeze()
    {
        if (MFrozen)
        {
            return false;
        }
        for (std::size_t ScopedIndex = 0; ScopedIndex < MDescriptors.size(); ++ScopedIndex)
        {
            const Descriptor& Scoped = MDescriptors[ScopedIndex];
            if (Scoped.Id.empty() || MIndex.contains(Scoped.Id))
            {
                return false;
            }
            MIndex.emplace(Scoped.Id, ScopedIndex);
        }
        MFrozen = true;
        return true;
    }

    [[nodiscard]] const Descriptor* Find(std::string_view InId) const noexcept
    {
        const auto Scoped = MIndex.find(std::string(InId));
        return Scoped == MIndex.end() ? nullptr : &MDescriptors[Scoped->second];
    }

    [[nodiscard]] std::span<const Descriptor> Descriptors() const noexcept
    {
        return MDescriptors;
    }

    [[nodiscard]] std::size_t IndexOf(std::string_view InId) const noexcept
    {
        const auto Scoped = MIndex.find(std::string(InId));
        return Scoped == MIndex.end() ? static_cast<std::size_t>(-1) : Scoped->second;
    }

    [[nodiscard]] Snapshot MakeSnapshot() const
    {
        std::vector<Value> ScopedValues;
        ScopedValues.reserve(MDescriptors.size());
        for (const Descriptor& Scoped : MDescriptors)
        {
            ScopedValues.push_back(Effective(Scoped.Id));
        }
        return Snapshot(std::move(ScopedValues));
    }

    [[nodiscard]] bool SetProject(std::string_view InId, Value InValue, std::string* InError = nullptr)
    {
        return Set(InId, Scope::Project, std::move(InValue), false, InError);
    }
    [[nodiscard]] bool SetUser(std::string_view InId, Value InValue, bool InConfirmed = false,
                               std::string* InError = nullptr)
    {
        return Set(InId, Scope::User, std::move(InValue), InConfirmed, InError);
    }
    [[nodiscard]] bool SetSession(std::string_view InId, Value InValue, bool InConfirmed = false,
                                  std::string* InError = nullptr)
    {
        return Set(InId, Scope::Session, std::move(InValue), InConfirmed, InError);
    }

    [[nodiscard]] Value Effective(std::string_view InId) const
    {
        const Descriptor* Scoped = Find(InId);
        if (Scoped == nullptr)
        {
            return {};
        }
        const std::string ScopedKey(InId);
        if (const auto Scoped = MSession.find(ScopedKey); Scoped != MSession.end())
        {
            return Scoped->second;
        }
        if (Scoped->IsUserEditable())
        {
            if (const auto Scoped = MUser.find(ScopedKey); Scoped != MUser.end())
            {
                return Scoped->second;
            }
        }
        if (const auto Scoped = MProject.find(ScopedKey); Scoped != MProject.end())
        {
            return Scoped->second;
        }
        return Scoped->DefaultValue;
    }

    [[nodiscard]] bool IsOverridden(std::string_view InId, Scope InScope) const
    {
        const std::string ScopedKey(InId);
        if (InScope == Scope::User)
        {
            return MUser.contains(ScopedKey);
        }
        if (InScope == Scope::Session)
        {
            return MSession.contains(ScopedKey);
        }
        return MProject.contains(ScopedKey);
    }

    template <typename Function> void ForEachOverride(Scope InScope, Function&& InFunction) const
    {
        const std::map<std::string, Value>* ScopedValues = &MProject;
        if (InScope == Scope::User)
        {
            ScopedValues = &MUser;
        }
        else if (InScope == Scope::Session)
        {
            ScopedValues = &MSession;
        }
        for (const auto& [ScopedId, ScopedValue] : *ScopedValues)
        {
            const Descriptor* ScopedDescriptor = Find(ScopedId);
            if (ScopedDescriptor != nullptr)
            {
                InFunction(*ScopedDescriptor, ScopedValue);
            }
        }
    }

    [[nodiscard]] bool Apply(std::string_view InId, std::string* InError = nullptr)
    {
        const Descriptor* Scoped = Find(InId);
        if (Scoped == nullptr)
        {
            SetError(InError, "unknown setting");
            return false;
        }
        if (!Scoped->Apply)
        {
            return true;
        }
        Scoped->Apply(Effective(InId));
        return true;
    }

    [[nodiscard]] bool ApplyAll(std::string* InError = nullptr)
    {
        for (const Descriptor& Scoped : MDescriptors)
        {
            if (!Apply(Scoped.Id, InError))
            {
                return false;
            }
        }
        return true;
    }

    // Safe mode restores every dangerous setting to its declared safe value and
    // drops user/session overrides that could lock the player out.
    void RestoreSafeMode()
    {
        for (const Descriptor& Scoped : MDescriptors)
        {
            if (!Scoped.IsDangerous())
            {
                continue;
            }
            MUser.erase(Scoped.Id);
            MSession.erase(Scoped.Id);
            if (Scoped.SafeValue.Kind == Scoped.Kind)
            {
                MProject[Scoped.Id] = Validate(Scoped, Scoped.SafeValue);
            }
        }
    }

    [[nodiscard]] bool IsFrozen() const noexcept
    {
        return MFrozen;
    }

private:
    Builder Add(std::string InId, Type InKind, Value InDefault)
    {
        Descriptor Scoped;
        Scoped.Id = std::move(InId);
        Scoped.Kind = InKind;
        Scoped.DefaultValue = std::move(InDefault);
        Scoped.SafeValue = Scoped.DefaultValue;
        MDescriptors.push_back(std::move(Scoped));
        return Builder{this, MDescriptors.size() - 1u};
    }

    static void SetError(std::string* InError, std::string_view InMessage)
    {
        if (InError != nullptr)
        {
            *InError = std::string(InMessage);
        }
    }

    [[nodiscard]] Value Validate(const Descriptor& InDescriptor, Value InValue) const
    {
        if (InValue.Kind != InDescriptor.Kind)
        {
            return InDescriptor.DefaultValue;
        }
        switch (InValue.Kind)
        {
            case Type::Float:
                if (!std::isfinite(InValue.FloatValue))
                {
                    return InDescriptor.DefaultValue;
                }
                InValue.FloatValue = std::clamp(InValue.FloatValue, InDescriptor.Minimum, InDescriptor.Maximum);
                break;
            case Type::Int:
                InValue.IntValue = static_cast<std::int64_t>(
                    std::clamp(static_cast<double>(InValue.IntValue), InDescriptor.Minimum, InDescriptor.Maximum));
                break;
            case Type::Color:
                InValue.ColorValue.R = std::clamp(InValue.ColorValue.R, 0.0f, 1.0f);
                InValue.ColorValue.G = std::clamp(InValue.ColorValue.G, 0.0f, 1.0f);
                InValue.ColorValue.B = std::clamp(InValue.ColorValue.B, 0.0f, 1.0f);
                InValue.ColorValue.A = std::clamp(InValue.ColorValue.A, 0.0f, 1.0f);
                break;
            case Type::String:
                if (InValue.StringValue.size() > 1024u)
                {
                    InValue.StringValue.resize(1024u);
                }
                break;
            case Type::Enum:
                if (!InDescriptor.EnumValues.empty() &&
                    std::find(InDescriptor.EnumValues.begin(), InDescriptor.EnumValues.end(), InValue.EnumValue) ==
                        InDescriptor.EnumValues.end())
                {
                    return InDescriptor.DefaultValue;
                }
                break;
            case Type::Bool:
                break;
        }
        return InValue;
    }

    [[nodiscard]] bool Set(std::string_view InId, Scope InScope, Value InValue, bool InConfirmed,
                           std::string* InError)
    {
        const Descriptor* Scoped = Find(InId);
        if (Scoped == nullptr)
        {
            SetError(InError, "unknown setting");
            return false;
        }
        if (InScope == Scope::User && !Scoped->IsUserEditable())
        {
            SetError(InError, "setting is not user-editable");
            return false;
        }
        if (Scoped->IsDangerous() && !InConfirmed)
        {
            SetError(InError, "dangerous setting requires confirmation");
            return false;
        }
        const Value ScopedValue = Validate(*Scoped, std::move(InValue));
        switch (InScope)
        {
            case Scope::Project:
                MProject[std::string(InId)] = ScopedValue;
                break;
            case Scope::User:
                MUser[std::string(InId)] = ScopedValue;
                break;
            case Scope::Session:
                MSession[std::string(InId)] = ScopedValue;
                break;
        }
        if (Scoped->Apply && !Scoped->IsDangerous())
        {
            Scoped->Apply(ScopedValue);
            ++MApplyCount;
        }
        return true;
    }

    std::vector<Descriptor> MDescriptors;
    std::unordered_map<std::string, std::size_t> MIndex;
    std::map<std::string, Value> MProject;
    std::map<std::string, Value> MUser;
    std::map<std::string, Value> MSession;
    std::uint32_t MApplyCount = 0;
    bool MFrozen = false;
};
}