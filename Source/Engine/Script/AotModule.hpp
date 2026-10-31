// FemboyEngine2D - AOT script plugin loader and host bridge.
#pragma once

#include "Engine/Script/AotAbi.hpp"
#include "Engine/Script/EcsBridge.hpp"
#include "Engine/Script/Vm.hpp"

#include <cstdint>
#include <string>

namespace Fby::Script
{

// Validates ABI version and component LayoutHashes. Does not call into the
[[nodiscard]] bool ValidateAotModule(const Aot::Module& InModule, const EcsBridge& InBridge,
                                     std::string* InError = nullptr);

// Runs one event of an already-loaded module. Returns false when the event is
[[nodiscard]] bool RunAotModule(const Aot::Module& InModule, ScriptEvent InEvent, Entity InSelf, VmContext& InContext,
                                const EcsBridge& InBridge, std::string* InError = nullptr);

class AotModule
{
public:
    AotModule() = default;
    ~AotModule();

    AotModule(const AotModule&) = delete;
    AotModule& operator=(const AotModule&) = delete;

    [[nodiscard]] bool Load(const char* InPath, const EcsBridge& InBridge, std::string* InError = nullptr);
    void Unload() noexcept;

    [[nodiscard]] bool Loaded() const noexcept
    {
        return MModule != nullptr;
    }
    [[nodiscard]] std::uint64_t GraphHash() const noexcept
    {
        return MModule != nullptr ? MModule->graphHash : 0;
    }
    [[nodiscard]] bool HasEvent(ScriptEvent InEvent) const noexcept;
    [[nodiscard]] bool Run(ScriptEvent InEvent, Entity InSelf, VmContext& InContext,
                           std::string* InError = nullptr) const;

private:
    const Aot::Module* ValidateLoaded(const EcsBridge& InBridge, std::string* InError) noexcept;

    void* MHandle = nullptr;
    const Aot::Module* MModule = nullptr;
    // Borrowed; outlives the module in every supported use
    const EcsBridge* MBridge = nullptr;
};
}
