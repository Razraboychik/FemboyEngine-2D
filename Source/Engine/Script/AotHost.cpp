// FemboyEngine2D - AOT manifest and nativized program registry implementation.
#include "Engine/Script/AotHost.hpp"

#include "Engine/Script/Bytecode.hpp"

#include <algorithm>
#include <charconv>

namespace Fby::Script
{
namespace
{
[[nodiscard]] bool ParseHex64(std::string_view InText, std::uint64_t* OutValue) noexcept
{
    if (OutValue == nullptr || InText.empty() || InText.size() > 16)
    {
        return false;
    }
    std::uint64_t ScopedValue = 0;
    const auto ScopedResult = std::from_chars(InText.data(), InText.data() + InText.size(), ScopedValue, 16);
    if (ScopedResult.ec != std::errc{} || ScopedResult.ptr != InText.data() + InText.size())
    {
        return false;
    }
    *OutValue = ScopedValue;
    return true;
}

[[nodiscard]] std::string FormatHash(std::uint64_t InHash)
{
    std::array<char, 17> ScopedBuffer{};
    const auto ScopedResult = std::to_chars(ScopedBuffer.data(), ScopedBuffer.data() + 16, InHash, 16);
    std::string ScopedText(ScopedBuffer.data(), ScopedResult.ptr);
    if (ScopedText.size() < 16)
    {
        ScopedText.insert(ScopedText.begin(), 16 - ScopedText.size(), '0');
    }
    return ScopedText;
}

[[nodiscard]] std::vector<std::string_view> SplitLine(std::string_view InLine, char InDelimiter) noexcept
{
    std::vector<std::string_view> ScopedFields;
    std::size_t ScopedStart = 0;
    while (ScopedStart <= InLine.size())
    {
        const std::size_t ScopedEnd = InLine.find(InDelimiter, ScopedStart);
        const std::size_t ScopedLength =
            (ScopedEnd == std::string_view::npos ? InLine.size() : ScopedEnd) - ScopedStart;
        ScopedFields.push_back(InLine.substr(ScopedStart, ScopedLength));
        if (ScopedEnd == std::string_view::npos)
        {
            break;
        }
        ScopedStart = ScopedEnd + 1;
    }
    return ScopedFields;
}
}

const AotHost::Slot* AotHost::Find(std::uint64_t InHash) const noexcept
{
    if (InHash == 0)
    {
        return nullptr;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MSlots[ScopedI].Ref.Hash == InHash)
        {
            return &MSlots[ScopedI];
        }
    }
    return nullptr;
}

AotHost::Slot* AotHost::FindMutable(std::uint64_t InHash) noexcept
{
    if (InHash == 0)
    {
        return nullptr;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MSlots[ScopedI].Ref.Hash == InHash)
        {
            return &MSlots[ScopedI];
        }
    }
    return nullptr;
}

const AotProgramRef* AotHost::ProgramAt(std::uint32_t InIndex) const noexcept
{
    if (InIndex >= MCount)
    {
        return nullptr;
    }
    return &MSlots[InIndex].Ref;
}

std::uint64_t AotHost::ProgramHashAt(std::uint32_t InIndex) const noexcept
{
    return InIndex < MCount ? MSlots[InIndex].Ref.Hash : 0;
}

bool AotHost::HasProgram(std::uint64_t InHash) const noexcept
{
    return Find(InHash) != nullptr;
}

bool AotHost::RegisterModule(std::uint64_t InHash, const Aot::Module& InModule, const EcsBridge& InBridge,
                             std::string_view InName)
{
    if (InHash == 0 || Find(InHash) != nullptr || MCount >= kMaxAotPrograms)
    {
        return false;
    }
    Slot& ScopedSlot = MSlots[MCount++];
    ScopedSlot.Ref.Hash = InHash;
    ScopedSlot.Ref.Name = std::string(InName);
    ScopedSlot.Borrowed = &InModule;
    ScopedSlot.Bridge = &InBridge;
    ++MLoadedCount;
    return true;
}

bool AotHost::Run(std::uint64_t InProgramHash, ScriptEvent InEvent, Entity InSelf, VmContext& InContext,
                  std::string* InError) const
{
    const Slot* ScopedSlot = Find(InProgramHash);
    if (ScopedSlot == nullptr || ScopedSlot->Bridge == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "program is not nativized";
        }
        return false;
    }
    if (ScopedSlot->Owned != nullptr)
    {
        return ScopedSlot->Owned->Run(InEvent, InSelf, InContext, InError);
    }
    if (ScopedSlot->Borrowed != nullptr)
    {
        return RunAotModule(*ScopedSlot->Borrowed, InEvent, InSelf, InContext, *ScopedSlot->Bridge, InError);
    }
    if (InError != nullptr)
    {
        *InError = "program module is not loaded";
    }
    return false;
}

bool AotHost::ValidateRequired(std::span<const std::uint64_t> InRequiredHashes, std::string* InError) const
{
    std::string ScopedMissing;
    for (const std::uint64_t ScopedHash : InRequiredHashes)
    {
        if (ScopedHash == 0)
        {
            continue;
        }
        if (Find(ScopedHash) == nullptr)
        {
            if (!ScopedMissing.empty())
            {
                ScopedMissing += ", ";
            }
            ScopedMissing += FormatHash(ScopedHash);
        }
    }
    if (ScopedMissing.empty())
    {
        return true;
    }
    if (InError != nullptr)
    {
        *InError = "programs without an AOT module: " + ScopedMissing;
    }
    return false;
}

void AotHost::Unload() noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MSlots[ScopedI].Owned != nullptr)
        {
            MSlots[ScopedI].Owned->Unload();
        }
    }
    for (Slot& ScopedSlot : MSlots)
    {
        ScopedSlot = {};
    }
    MCount = 0;
    MLoadedCount = 0;
}

bool AotHost::LoadManifest(std::string_view InManifestText, const std::filesystem::path& InBaseDirectory,
                           const EcsBridge& InBridge, std::string* InError)
{
    std::vector<AotProgramRef> ScopedPrograms;
    if (!ParseManifest(InManifestText, &ScopedPrograms, InError))
    {
        return false;
    }
    Unload();
    for (const AotProgramRef& ScopedRef : ScopedPrograms)
    {
        if (MCount >= kMaxAotPrograms)
        {
            if (InError != nullptr)
            {
                *InError = "too many AOT programs in the manifest";
            }
            Unload();
            return false;
        }
        auto ScopedModule = std::make_unique<AotModule>();
        const std::filesystem::path ScopedPath = InBaseDirectory / ScopedRef.LibraryPath;
        std::string ScopedLoadError;
        if (!ScopedModule->Load(ScopedPath.string().c_str(), InBridge, &ScopedLoadError))
        {
            if (InError != nullptr)
            {
                *InError = "AOT module '" + ScopedRef.LibraryPath + "': " + ScopedLoadError;
            }
            Unload();
            return false;
        }
        Slot& ScopedSlot = MSlots[MCount++];
        ScopedSlot.Ref = ScopedRef;
        ScopedSlot.Ref.LibraryPath = ScopedPath.string();
        ScopedSlot.Owned = std::move(ScopedModule);
        ScopedSlot.Bridge = &InBridge;
        ++MLoadedCount;
    }
    return true;
}

std::string AotHost::BuildManifest(std::span<const AotProgramRef> InPrograms)
{
    std::string ScopedText(kAotManifestMagic);
    ScopedText += '\n';
    for (const AotProgramRef& ScopedRef : InPrograms)
    {
        ScopedText += "program\t";
        ScopedText += FormatHash(ScopedRef.Hash);
        ScopedText += '\t';
        ScopedText += ScopedRef.LibraryPath;
        ScopedText += '\t';
        ScopedText += ScopedRef.Name;
        ScopedText += '\n';
    }
    return ScopedText;
}

bool AotHost::ParseManifest(std::string_view InManifestText, std::vector<AotProgramRef>* OutPrograms,
                            std::string* InError)
{
    if (OutPrograms == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "manifest: null output";
        }
        return false;
    }
    std::vector<AotProgramRef> ScopedPrograms;
    std::size_t ScopedPosition = 0;
    bool ScopedFirst = true;
    while (ScopedPosition <= InManifestText.size())
    {
        const std::size_t ScopedEnd = InManifestText.find('\n', ScopedPosition);
        std::string_view ScopedLine =
            InManifestText.substr(ScopedPosition, (ScopedEnd == std::string_view::npos ? InManifestText.size()
                                                                                        : ScopedEnd) -
                                                     ScopedPosition);
        if (!ScopedLine.empty() && ScopedLine.back() == '\r')
        {
            ScopedLine.remove_suffix(1);
        }
        ScopedPosition = ScopedEnd == std::string_view::npos ? InManifestText.size() + 1 : ScopedEnd + 1;
        if (ScopedFirst)
        {
            ScopedFirst = false;
            if (ScopedLine != kAotManifestMagic)
            {
                if (InError != nullptr)
                {
                    *InError = "manifest: bad magic (expected FBYAOT1)";
                }
                return false;
            }
            continue;
        }
        if (ScopedLine.empty())
        {
            continue;
        }
        const std::vector<std::string_view> ScopedFields = SplitLine(ScopedLine, '\t');
        if (ScopedFields.size() < 3 || ScopedFields[0] != "program")
        {
            if (InError != nullptr)
            {
                *InError = "manifest: malformed program line";
            }
            return false;
        }
        AotProgramRef ScopedRef;
        if (!ParseHex64(ScopedFields[1], &ScopedRef.Hash) || ScopedRef.Hash == 0)
        {
            if (InError != nullptr)
            {
                *InError = "manifest: invalid program hash";
            }
            return false;
        }
        ScopedRef.LibraryPath = std::string(ScopedFields[2]);
        ScopedRef.Name = ScopedFields.size() > 3 ? std::string(ScopedFields[3]) : std::string();
        if (ScopedRef.LibraryPath.empty())
        {
            if (InError != nullptr)
            {
                *InError = "manifest: empty library path";
            }
            return false;
        }
        if (ScopedPrograms.size() >= kMaxAotPrograms)
        {
            if (InError != nullptr)
            {
                *InError = "manifest: too many programs";
            }
            return false;
        }
        ScopedPrograms.push_back(std::move(ScopedRef));
    }
    if (ScopedFirst)
    {
        if (InError != nullptr)
        {
            *InError = "manifest: empty file";
        }
        return false;
    }
    *OutPrograms = std::move(ScopedPrograms);
    return true;
}
}
