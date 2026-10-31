// FemboyEngine2D - nativized program registry: AOT manifest, module loading and
// the cook-time gate that guarantees a build ships no interpreter fallback.
#pragma once

#include "Engine/Script/AotModule.hpp"
#include "Engine/Script/EcsBridge.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Script
{

// One entry of the AOT manifest: the stable program hash (see HashProgramBytes)
// and the native module that implements it.
struct AotProgramRef
{
    std::uint64_t Hash = 0;
    std::string LibraryPath{};
    std::string Name{};
};

inline constexpr std::uint32_t kMaxAotPrograms = 32;
inline constexpr std::string_view kAotManifestMagic = "FBYAOT1";

class AotHost
{
public:
    AotHost() = default;
    ~AotHost() = default;

    AotHost(const AotHost&) = delete;
    AotHost& operator=(const AotHost&) = delete;

    [[nodiscard]] std::uint32_t ProgramCount() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] const AotProgramRef* ProgramAt(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] std::uint64_t ProgramHashAt(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] bool HasProgram(std::uint64_t InHash) const noexcept;
    [[nodiscard]] std::uint32_t LoadedModuleCount() const noexcept
    {
        return MLoadedCount;
    }

    // Parses the manifest and loads every module it names. Paths are resolved
    // relative to `InBaseDirectory`; each module is validated against the bridge's
    // component LayoutHashes before it is accepted.
    [[nodiscard]] bool LoadManifest(std::string_view InManifestText, const std::filesystem::path& InBaseDirectory,
                                    const EcsBridge& InBridge, std::string* InError = nullptr);

    // Test/tool seam: registers an already-loaded module. The module must outlive
    // the host (it is borrowed, not copied).
    [[nodiscard]] bool RegisterModule(std::uint64_t InHash, const Aot::Module& InModule, const EcsBridge& InBridge,
                                      std::string_view InName = {});

    // Runs one event through the program's native module.
    [[nodiscard]] bool Run(std::uint64_t InProgramHash, ScriptEvent InEvent, Entity InSelf, VmContext& InContext,
                           std::string* InError = nullptr) const;

    // Cook gate: every required program must be nativized. Missing hashes are
    // reported together, so a cook fails once instead of after a partial deploy.
    [[nodiscard]] bool ValidateRequired(std::span<const std::uint64_t> InRequiredHashes,
                                        std::string* InError = nullptr) const;

    [[nodiscard]] BigNumberPool& BigNumbers() const noexcept
    {
        return MBigNumbers;
    }
    [[nodiscard]] DecimalPool& Decimals() const noexcept
    {
        return MDecimals;
    }
    void ResetTransientNumbers() const noexcept
    {
        MBigNumbers.Clear();
        MDecimals.Clear();
    }

    void Unload() noexcept;

    static std::string BuildManifest(std::span<const AotProgramRef> InPrograms);
    static bool ParseManifest(std::string_view InManifestText, std::vector<AotProgramRef>* OutPrograms,
                              std::string* InError = nullptr);

private:
    struct Slot
    {
        AotProgramRef Ref{};
        std::unique_ptr<AotModule> Owned{};
        const Aot::Module* Borrowed = nullptr;
        const EcsBridge* Bridge = nullptr;
    };

    [[nodiscard]] const Slot* Find(std::uint64_t InHash) const noexcept;
    [[nodiscard]] Slot* FindMutable(std::uint64_t InHash) noexcept;

    std::array<Slot, kMaxAotPrograms> MSlots{};
    // Nativized scripts use the same token pool layout as the interpreter.
    mutable BigNumberPool MBigNumbers{};
    mutable DecimalPool MDecimals{};
    std::uint32_t MCount = 0;
    std::uint32_t MLoadedCount = 0;
};
}
