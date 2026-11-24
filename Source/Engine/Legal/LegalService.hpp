// FemboyEngine2D - project legal documents and first-run acceptance.
#pragma once

#include "Engine/Assets/VFS.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Legal
{

enum class DeclineAction : std::uint8_t
{
    Quit,
    Continue,
    Custom
};

struct LegalDocument
{
    std::string Language;
    std::string Title;
    std::string Path;
    std::string Text;
};

struct LegalManifest
{
    std::string Id;
    std::string Version;
    std::string Updated;
    bool Required = true;
    DeclineAction OnDecline = DeclineAction::Quit;
    std::vector<LegalDocument> Documents;
    std::uint64_t ContentHash = 0;
};

struct LegalAcceptance
{
    std::string Id;
    std::string Version;
    std::uint64_t ContentHash = 0;
    std::string AcceptedAt;
    std::string Language;
};

// Engine-level, UI-agnostic legal layer. The game supplies Legal/legal.toml and
// localized documents; the engine loads, hashes, stores and compares acceptance.
class LegalService
{
public:
    bool LoadFromVfs(VirtualFileSystem& InVfs, std::string_view InDirectory = "Legal",
                     std::string* InError = nullptr);
    [[nodiscard]] const LegalManifest& Manifest() const noexcept
    {
        return MManifest;
    }
    [[nodiscard]] const LegalDocument* DocumentFor(std::string_view InLanguage) const;
    [[nodiscard]] std::string_view Text(std::string_view InLanguage) const;
    [[nodiscard]] bool NeedsAcceptance(const LegalAcceptance& InAcceptance) const;
    [[nodiscard]] LegalAcceptance Accept(std::string_view InTimestamp, std::string_view InLanguage) const;
    [[nodiscard]] DeclineAction OnDecline() const noexcept
    {
        return MManifest.OnDecline;
    }

    [[nodiscard]] static bool SaveAcceptance(std::string_view InPath, const LegalAcceptance& InAcceptance,
                                             std::string* InError = nullptr);
    [[nodiscard]] static bool LoadAcceptance(std::string_view InPath, LegalAcceptance* OutAcceptance,
                                             std::string* InError = nullptr);

private:
    LegalManifest MManifest;
};

}