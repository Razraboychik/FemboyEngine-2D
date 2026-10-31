// FemboyEngine2D - runtime localization.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Loc
{
struct LocalizationPack;
struct LocaleTable;
struct StringEntry;
}

namespace Fby
{

enum class LocalizationGender : std::uint8_t
{
    Neutral = 0,
    Male = 1,
    Female = 2,
};

class LocalizationTable
{
public:
    LocalizationTable() = default;

    // Verifies the buffer and keeps a view of it. The caller guarantees the
    [[nodiscard]] static LocalizationTable Open(std::span<const std::byte> InBytes, std::string* InError = nullptr);

    [[nodiscard]] bool IsValid() const noexcept
    {
        return MPack != nullptr;
    }
    [[nodiscard]] std::uint32_t Version() const noexcept;
    [[nodiscard]] std::size_t LocaleCount() const noexcept;
    [[nodiscard]] std::string_view LocaleCode(std::size_t InIndex) const noexcept;

    // Selects the active language. Returns false for an unknown code.
    bool SelectLocale(std::string_view InCode) noexcept;
    // Picks the pack's default locale; called by Open().
    void SelectDefaultLocale() noexcept;

    // The hot path: binary search by id, zero allocation. Returns an empty view
    [[nodiscard]] std::string_view Find(std::uint32_t InId) const noexcept;
    // Falls back to the default locale when the active one does not have the id.
    [[nodiscard]] std::string_view FindWithFallback(std::uint32_t InId) const noexcept;

    // Variant convention for tables that only store uint32 string ids:
    //   base + 0..5   -> other, one, few, many, two, zero
    //   base + 8..10  -> neutral, male, female
    //   base + 16..33 -> gender * 6 + plural category
    // The resolver first tries the combined id, then plural-only, gender-only,
    // and finally the base id. This keeps the FlatBuffers schema stable.
    [[nodiscard]] std::string_view FindVariant(std::uint32_t InBaseId, std::int64_t InCount,
                                               LocalizationGender InGender) const noexcept;
    [[nodiscard]] std::string_view ActiveLocale() const noexcept;

private:
    [[nodiscard]] const Fby::Loc::LocaleTable* LocaleAt(std::size_t InIndex) const noexcept;

    const Fby::Loc::LocalizationPack* MPack = nullptr;
    const Fby::Loc::LocaleTable* MActive = nullptr;
    const Fby::Loc::LocaleTable* MFallback = nullptr;
};
}
