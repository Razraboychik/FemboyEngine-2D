// FemboyEngine2D - AOT plugin ABI for FemboyScript 2.0.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Fby::Script::Aot
{

// "FAOT"
inline constexpr std::uint32_t kMagic = 0x544F4146u;
// Bumped when HostApi gains callbacks. Generated plugins and the loader must
// agree, so a stale .dll is rejected with "ABI version mismatch" instead of
// calling through a short function table.
inline constexpr std::uint16_t kAbiVersion = 5;
inline constexpr std::uint32_t kMaxModuleComponents = 32;

// Fault codes mirror VmFault::Code so the VM and an AOT plugin report the same
// number for the same failure (AotModule static_asserts the mapping).
namespace FaultCode
{
inline constexpr std::uint32_t MissingComponent = 2;
inline constexpr std::uint32_t BadField = 5;
inline constexpr std::uint32_t DivisionByZero = 8;
}

struct Vector2D
{
    float X = 0.0f;
    float Y = 0.0f;
};

// Rust-style POD helpers: the generated code does not reinterpret an unaligned
template <typename T> [[nodiscard]] inline T Load(const std::byte* InBase, std::uint32_t InOffset) noexcept
{
    T ScopedValue{};
    if (InBase != nullptr)
    {
        std::memcpy(&ScopedValue, InBase + InOffset, sizeof(T));
    }
    return ScopedValue;
}

template <typename T> inline void Store(std::byte* InBase, std::uint32_t InOffset, T InValue) noexcept
{
    if (InBase != nullptr)
    {
        std::memcpy(InBase + InOffset, &InValue, sizeof(T));
    }
}

// Host services the generated code may call. Function pointers, not an
struct HostApi
{
    std::uint32_t version = kAbiVersion;
    std::uint32_t reserved = 0;
    // Opaque pointer the host passes back on every call (the EcsBridge). Not a
    void* componentUser = nullptr;
    void* (*component)(void* InComponentUser, void* InRegistry, std::uint32_t InEntity,
                       std::uint64_t InLayoutHash) noexcept = nullptr;
    // Returns 1 when the entity was queued for destruction, 0 otherwise: the VM
    // writes the same value into the native result register.
    std::uint32_t (*despawn)(void* InUser, std::uint32_t InEntity) noexcept = nullptr;
    std::uint32_t (*spawn)(void* InUser, std::uint32_t InPrefabId) noexcept = nullptr;
    float (*randomFloat)(void* InUser) noexcept = nullptr;
    void (*logFloat)(void* InUser, float InValue) noexcept = nullptr;
    void (*logU32)(void* InUser, std::uint32_t InValue) noexcept = nullptr;
    void (*logVec2)(void* InUser, float InX, float InY) noexcept = nullptr;
    // Signed logging so a plugin and the interpreter print the same value.
    void (*logI32)(void* InUser, std::int32_t InValue) noexcept = nullptr;
    // Big integer tokens are float-register payloads; the host owns the pool.
    std::uint32_t (*bigIntFromInt)(void* InUser, std::int32_t InValue) noexcept = nullptr;
    std::uint32_t (*bigIntAdd)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::uint32_t (*bigIntSub)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::uint32_t (*bigIntMul)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::uint32_t (*bigIntDiv)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::uint32_t (*bigIntMod)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::int32_t (*bigIntCmp)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::int64_t (*bigIntToInt)(void* InUser, std::uint32_t InHandle) noexcept = nullptr;
    void (*bigIntLog)(void* InUser, std::uint32_t InHandle) noexcept = nullptr;
    void (*bigIntRelease)(void* InUser, std::uint32_t InHandle) noexcept = nullptr;
    std::uint32_t (*decimalFromInt)(void* InUser, std::int32_t InValue) noexcept = nullptr;
    std::uint32_t (*decimalAdd)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::uint32_t (*decimalSub)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::uint32_t (*decimalMul)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::uint32_t (*decimalDiv)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::uint32_t (*decimalDivRounded)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight,
                                       std::int32_t InRounding) noexcept = nullptr;
    std::int32_t (*decimalCmp)(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept = nullptr;
    std::int64_t (*decimalToInt)(void* InUser, std::uint32_t InHandle) noexcept = nullptr;
    void (*decimalLog)(void* InUser, std::uint32_t InHandle) noexcept = nullptr;
    void (*decimalRelease)(void* InUser, std::uint32_t InHandle) noexcept = nullptr;
    // Raised by generated code when the interpreter would report a VmFault.
    void (*fault)(void* InUser, std::uint32_t InCode, std::uint32_t InPc) noexcept = nullptr;
};

struct Context
{
    const HostApi* host = nullptr;
    void* registry = nullptr;
    void* user = nullptr;
    float dt = 0.0f;
    std::uint64_t frameIndex = 0;
    std::uint32_t self = 0;
    std::uint32_t reserved = 0;

    [[nodiscard]] std::byte* Component(std::uint64_t InLayoutHash) const noexcept
    {
        if (host == nullptr || host->component == nullptr)
        {
            return nullptr;
        }
        return static_cast<std::byte*>(host->component(host->componentUser, registry, self, InLayoutHash));
    }

    [[nodiscard]] std::uint32_t Despawn(std::uint32_t InEntity) const noexcept
    {
        return (host != nullptr && host->despawn != nullptr) ? host->despawn(user, InEntity) : 0u;
    }

    [[nodiscard]] std::uint32_t Spawn(std::uint32_t InPrefabId) const noexcept
    {
        return (host != nullptr && host->spawn != nullptr) ? host->spawn(user, InPrefabId) : 0xFFFFFFFFu;
    }

    [[nodiscard]] float RandomFloat() const noexcept
    {
        return (host != nullptr && host->randomFloat != nullptr) ? host->randomFloat(user) : 0.0f;
    }

    void LogFloat(float InValue) const noexcept
    {
        if (host != nullptr && host->logFloat != nullptr)
        {
            host->logFloat(user, InValue);
        }
    }

    void LogU32(std::uint32_t InValue) const noexcept
    {
        if (host != nullptr && host->logU32 != nullptr)
        {
            host->logU32(user, InValue);
        }
    }

    void LogI32(std::int32_t InValue) const noexcept
    {
        if (host != nullptr && host->logI32 != nullptr)
        {
            host->logI32(user, InValue);
            return;
        }
        LogU32(static_cast<std::uint32_t>(InValue));
    }

    // Reports a fault the VM would surface as VmFault. Generated code calls this
    // before returning so a plugin fault is observable instead of silent.
    void Fault(std::uint32_t InCode, std::uint32_t InPc) const noexcept
    {
        if (host != nullptr && host->fault != nullptr)
        {
            host->fault(user, InCode, InPc);
        }
    }

    void LogVec2(float InX, float InY) const noexcept
    {
        if (host != nullptr && host->logVec2 != nullptr)
        {
            host->logVec2(user, InX, InY);
        }
    }

    [[nodiscard]] std::uint32_t BigIntFromInt(std::int32_t InValue) const noexcept
    {
        return (host != nullptr && host->bigIntFromInt != nullptr) ? host->bigIntFromInt(user, InValue) : 0u;
    }

    [[nodiscard]] std::uint32_t BigIntAdd(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->bigIntAdd != nullptr) ? host->bigIntAdd(user, InLeft, InRight) : 0u;
    }

    [[nodiscard]] std::uint32_t BigIntSub(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->bigIntSub != nullptr) ? host->bigIntSub(user, InLeft, InRight) : 0u;
    }

    [[nodiscard]] std::uint32_t BigIntMul(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->bigIntMul != nullptr) ? host->bigIntMul(user, InLeft, InRight) : 0u;
    }

    [[nodiscard]] std::uint32_t BigIntDiv(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->bigIntDiv != nullptr) ? host->bigIntDiv(user, InLeft, InRight) : 0u;
    }

    [[nodiscard]] std::uint32_t BigIntMod(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->bigIntMod != nullptr) ? host->bigIntMod(user, InLeft, InRight) : 0u;
    }

    [[nodiscard]] std::int32_t BigIntCmp(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->bigIntCmp != nullptr) ? host->bigIntCmp(user, InLeft, InRight) : 0;
    }

    [[nodiscard]] std::int64_t BigIntToInt(std::uint32_t InHandle) const noexcept
    {
        return (host != nullptr && host->bigIntToInt != nullptr) ? host->bigIntToInt(user, InHandle) : 0;
    }

    void BigIntLog(std::uint32_t InHandle) const noexcept
    {
        if (host != nullptr && host->bigIntLog != nullptr)
        {
            host->bigIntLog(user, InHandle);
        }
    }

    void BigIntRelease(std::uint32_t InHandle) const noexcept
    {
        if (host != nullptr && host->bigIntRelease != nullptr)
        {
            host->bigIntRelease(user, InHandle);
        }
    }

    [[nodiscard]] std::uint32_t DecimalFromInt(std::int32_t InValue) const noexcept
    {
        return (host != nullptr && host->decimalFromInt != nullptr) ? host->decimalFromInt(user, InValue) : 0u;
    }

    [[nodiscard]] std::uint32_t DecimalAdd(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->decimalAdd != nullptr) ? host->decimalAdd(user, InLeft, InRight) : 0u;
    }

    [[nodiscard]] std::uint32_t DecimalSub(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->decimalSub != nullptr) ? host->decimalSub(user, InLeft, InRight) : 0u;
    }

    [[nodiscard]] std::uint32_t DecimalMul(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->decimalMul != nullptr) ? host->decimalMul(user, InLeft, InRight) : 0u;
    }

    [[nodiscard]] std::uint32_t DecimalDiv(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->decimalDiv != nullptr) ? host->decimalDiv(user, InLeft, InRight) : 0u;
    }

    [[nodiscard]] std::uint32_t DecimalDivRounded(std::uint32_t InLeft, std::uint32_t InRight,
                                                  std::int32_t InRounding) const noexcept
    {
        return (host != nullptr && host->decimalDivRounded != nullptr)
                   ? host->decimalDivRounded(user, InLeft, InRight, InRounding)
                   : 0u;
    }

    [[nodiscard]] std::int32_t DecimalCmp(std::uint32_t InLeft, std::uint32_t InRight) const noexcept
    {
        return (host != nullptr && host->decimalCmp != nullptr) ? host->decimalCmp(user, InLeft, InRight) : 0;
    }

    [[nodiscard]] std::int64_t DecimalToInt(std::uint32_t InHandle) const noexcept
    {
        return (host != nullptr && host->decimalToInt != nullptr) ? host->decimalToInt(user, InHandle) : 0;
    }

    void DecimalLog(std::uint32_t InHandle) const noexcept
    {
        if (host != nullptr && host->decimalLog != nullptr)
        {
            host->decimalLog(user, InHandle);
        }
    }

    void DecimalRelease(std::uint32_t InHandle) const noexcept
    {
        if (host != nullptr && host->decimalRelease != nullptr)
        {
            host->decimalRelease(user, InHandle);
        }
    }
};

// One exported record per cooked program. `componentHashes` is an array of
struct Module
{
    std::uint32_t magic = kMagic;
    std::uint16_t abiVersion = kAbiVersion;
    std::uint16_t flags = 0;
    std::uint64_t graphHash = 0;
    std::uint64_t sourceHash = 0;
    std::uint32_t componentCount = 0;
    std::uint32_t reserved = 0;
    const std::uint64_t* componentHashes = nullptr;
    void (*start)(Context& InContext) noexcept = nullptr;
    void (*update)(Context& InContext) noexcept = nullptr;
    void (*fixedUpdate)(Context& InContext) noexcept = nullptr;
};
}

#if defined(_WIN32)
#define FBY_AOT_EXPORT extern "C" __declspec(dllexport)
#else
#define FBY_AOT_EXPORT extern "C" __attribute__((visibility("default")))
#endif
