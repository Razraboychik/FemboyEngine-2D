// FemboyEngine2D - deterministic binary command log for rollback.
#pragma once

#include "Engine/ECS/Snapshot.hpp"
#include "Engine/Core/Mirror.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace Fby::Ecs
{

inline constexpr std::uint32_t kCommandLogVersion = 3;
inline constexpr std::uint32_t kMaxCommandLogCommands = 4096;
inline constexpr std::uint32_t kMaxCommandLogBytes = 128u * 1024u;
inline constexpr char kCommandLogMagic[8] = {'F', 'B', 'L', 'O', 'G', '0', '0', '3'};

enum class CommandType : std::uint16_t
{
    Spawn = 1,
    Despawn = 2,
    AddComponent = 3,
    RemoveComponent = 4,
    // v3: subsystem construction is part of the deterministic command stream.
    CreateChain = 5,
    DestroyChain = 6,
    CreateCloth = 7,
    DestroyCloth = 8,
    EmitParticles = 9,
};

// Replay callbacks for v3 world commands. Function pointers + user context:
struct CommandWorldCallbacks
{
    void* user = nullptr;
    bool (*createChain)(void* InUser, Entity InEntity, std::span<const std::byte> InPayload) = nullptr;
    bool (*destroyChain)(void* InUser, Entity InEntity, std::span<const std::byte> InPayload) = nullptr;
    bool (*createCloth)(void* InUser, Entity InEntity, std::span<const std::byte> InPayload) = nullptr;
    bool (*destroyCloth)(void* InUser, Entity InEntity, std::span<const std::byte> InPayload) = nullptr;
    bool (*emitParticles)(void* InUser, Entity InEntity, std::span<const std::byte> InPayload) = nullptr;
};

struct CommandLogHeader
{
    char magic[8] = {};
    std::uint32_t version = kCommandLogVersion;
    std::uint32_t commandCount = 0;
    std::uint32_t payloadBytes = 0;
    std::uint32_t reserved = 0;
    std::uint64_t firstTick = 0;
    std::uint64_t lastTick = 0;
    std::uint64_t checksum = 0;
};

struct CommandRecord
{
    CommandType type = CommandType::Spawn;
    std::uint16_t reserved = 0;
    std::uint32_t payloadBytes = 0;
    std::uint32_t sequence = 0;
    Entity entity = entt::null;
    std::uint64_t tick = 0;
};

static_assert(sizeof(CommandLogHeader) == 48);
static_assert(sizeof(CommandRecord) == 24);
static_assert(std::is_trivially_copyable_v<CommandRecord>);

class CommandLog
{
public:
    CommandLog() noexcept
    {
        Clear(0);
    }

    void Clear(std::uint64_t InTick = 0) noexcept;

    [[nodiscard]] bool Spawn(Entity InEntity, std::uint64_t InTick) noexcept;
    [[nodiscard]] bool Despawn(Entity InEntity, std::uint64_t InTick) noexcept;

    // Canonicalizes T through Mirror (padding zeroed) before writing.
    template <typename T> [[nodiscard]] bool AddComponent(Entity InEntity, const T& InComponent, std::uint64_t InTick)
    {
        static_assert(Reflect::IsRegistered<T>(), "CommandLog::AddComponent<T> requires FBY_REFLECT_COMPONENT");
        std::array<std::byte, kMaxSnapshotComponentBytes> ScopedCanonical{};
        constexpr std::uint32_t ScopedSize = std::is_empty_v<T> ? 0u : static_cast<std::uint32_t>(sizeof(T));
        if constexpr (!std::is_empty_v<T>)
        {
            const auto* ScopedSource = reinterpret_cast<const std::byte*>(&InComponent);
            for (const Reflect::FieldDesc& ScopedField : Reflect::Fields<T>())
            {
                std::memcpy(ScopedCanonical.data() + ScopedField.offset, ScopedSource + ScopedField.offset,
                            ScopedField.size);
            }
        }
        return AddComponentBytes(InEntity, SnapshotTypeId(Reflect::Reflection<T>::kName),
                                 std::span<const std::byte>(ScopedCanonical.data(), ScopedSize), InTick);
    }

    [[nodiscard]] bool AddComponentBytes(Entity InEntity, std::uint64_t InTypeId, std::span<const std::byte> InData,
                                         std::uint64_t InTick) noexcept;

    template <typename T> [[nodiscard]] bool RemoveComponent(Entity InEntity, std::uint64_t InTick)
    {
        static_assert(Reflect::IsRegistered<T>(), "CommandLog::RemoveComponent<T> requires FBY_REFLECT_COMPONENT");
        return RemoveComponent(InEntity, SnapshotTypeId(Reflect::Reflection<T>::kName), InTick);
    }

    [[nodiscard]] bool RemoveComponent(Entity InEntity, std::uint64_t InTypeId, std::uint64_t InTick) noexcept;

    // v3 world command: copies an already-canonical record into the log.
    [[nodiscard]] bool PushWorldCommand(CommandType InType, Entity InEntity, std::span<const std::byte> InPayload,
                                        std::uint64_t InTick) noexcept;

    // Writes the header and the FNV-1a checksum. Idempotent; appending after Seal
    void Seal() const noexcept;

    [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCommandCount;
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return MCommandCount == 0;
    }
    [[nodiscard]] std::uint64_t FirstTick() const noexcept
    {
        return MFirstTick;
    }
    [[nodiscard]] std::uint64_t LastTick() const noexcept
    {
        return MLastTick;
    }

    // Applies the log to a registry. The caller normally starts from the base
    [[nodiscard]] bool Replay(Registry& InRegistry, const SnapshotSchema& InSchema,
                              const CommandWorldCallbacks* InWorld = nullptr) const;

    // Cursor-based replay for rollback: applies every record with tick <= tick
    struct Cursor
    {
        std::uint32_t commandIndex = 0;
        std::size_t payloadOffset = 0;
        std::uint64_t lastTick = 0;
    };
    [[nodiscard]] bool ApplyUntil(Registry& InRegistry, const SnapshotSchema& InSchema, std::uint64_t InTick,
                                  Cursor& InCursor, const CommandWorldCallbacks* InWorld = nullptr) const;

    // Parsed view for tools/netcode. Open() validates magic, version, size and
    struct View
    {
        CommandLogHeader header{};
        std::span<const std::byte> payload;

        [[nodiscard]] static View Open(std::span<const std::byte> InBytes, std::string_view* InError = nullptr);
    };

private:
    [[nodiscard]] bool BeginRecord(CommandType InType, Entity InEntity, std::uint32_t InPayloadBytes,
                                   std::uint64_t InTick) noexcept;
    [[nodiscard]] bool CanAppend(std::uint32_t InTotalBytes) const noexcept;

    mutable std::array<std::byte, kMaxCommandLogBytes> MStorage{};
    mutable std::uint32_t MWriteOffset = sizeof(CommandLogHeader);
    mutable std::uint32_t MCommandCount = 0;
    mutable std::uint64_t MFirstTick = 0;
    mutable std::uint64_t MLastTick = 0;
    mutable bool MSealed = false;
};
}
