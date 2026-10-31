// FemboyEngine2D - transactional editor command stack (Undo / Redo).
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby::Editor
{

inline constexpr std::uint32_t kMaxCommandCodecs = 64;
inline constexpr std::uint32_t kMaxUndoCommands = 512;
inline constexpr std::uint32_t kMaxTrackedComponentBytes = 32768;
inline constexpr std::uint32_t kMaxTransactionEdits = 16;
inline constexpr std::uint32_t kMaxSpawnedEntitiesPerTransaction = 32;
// A spawn command stores the complete component image of one entity. The limit
inline constexpr std::uint32_t kMaxSpawnSnapshotBytes = 128u * 1024u;
inline constexpr std::uint32_t kMaxCommandLabel = 48;

class CommandStack
{
public:
    enum class CommandKind : std::uint8_t
    {
        Component = 0,
        Spawn = 1,
        // Structural inverse of Spawn: the payload is the component image taken
        // before the entity was destroyed, so undo recreates it and redo removes
        // it again.
        Destroy = 2,
    };

    enum class Result : std::uint8_t
    {
        Ok = 0,
        NotInitialized,
        NoOpenTransaction,
        TransactionOpen,
        StackFull,
        PayloadFull,
        CodecFull,
        UnknownCodec,
        InvalidArgument,
        StaleEntity,
        NotBound,
    };

    [[nodiscard]] static constexpr const char* ResultName(Result InResult) noexcept
    {
        switch (InResult)
        {
            case Result::Ok:
                return "Ok";
            case Result::NotInitialized:
                return "NotInitialized";
            case Result::NoOpenTransaction:
                return "NoOpenTransaction";
            case Result::TransactionOpen:
                return "TransactionOpen";
            case Result::StackFull:
                return "StackFull";
            case Result::PayloadFull:
                return "PayloadFull";
            case Result::CodecFull:
                return "CodecFull";
            case Result::UnknownCodec:
                return "UnknownCodec";
            case Result::InvalidArgument:
                return "InvalidArgument";
            case Result::StaleEntity:
                return "StaleEntity";
            case Result::NotBound:
                return "NotBound";
        }
        return "?";
    }

    struct Config
    {
        // 4 MiB of compressed undo deltas
        std::size_t payloadBytes = 4u << 20;
        std::uint32_t maxCommands = kMaxUndoCommands;
    };

    struct Stats
    {
        std::uint32_t undoDepth = 0;
        std::uint32_t redoDepth = 0;
        std::uint32_t commands = 0;
        std::uint32_t bytesUsed = 0;
        std::uint32_t bytesCapacity = 0;
        std::uint32_t committedTransactions = 0;
        std::uint32_t rejectedCommands = 0;
    };

    // Observer runs after Undo/Redo applied a component (e.g. re-evaluate a
    using ApplyObserver = void (*)(void* InUser, Entity InEntity, std::uint16_t InCodec) noexcept;

    CommandStack() = default;
    ~CommandStack() = default;

    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;

    [[nodiscard]] bool Initialize(const Config& InConfig);
    [[nodiscard]] bool Initialize()
    {
        return Initialize(Config{});
    }
    void Shutdown() noexcept;
    [[nodiscard]] bool Initialized() const noexcept
    {
        return MPayload != nullptr;
    }
    void Bind(entt::registry* InRegistry, const Scene::ComponentRegistry* InComponents) noexcept;

    // Component codecs. Register every component the editor may edit. The raw
    [[nodiscard]] std::uint16_t RegisterComponentOps(const Scene::ComponentOps& InOps) noexcept;
    template <typename T> [[nodiscard]] std::uint16_t RegisterComponent(const char* InName = nullptr) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>, "a command-stack component must be POD");
        static_assert(sizeof(T) <= kMaxTrackedComponentBytes, "component exceeds the command-stack limit");
        std::string_view ScopedLabel = InName != nullptr ? std::string_view(InName) : std::string_view{};
        if (ScopedLabel.empty())
        {
            if constexpr (Reflect::IsRegistered<T>())
            {
                ScopedLabel = Reflect::Reflection<T>::kName;
            }
            else
            {
                ScopedLabel = "<unregistered>";
            }
        }
        Scene::ComponentOps ScopedOps;
        ScopedOps.name = ScopedLabel;
        ScopedOps.size = static_cast<std::uint32_t>(sizeof(T));
        if constexpr (Reflect::IsRegistered<T>())
        {
            ScopedOps.layoutHash = Reflect::LayoutHash<T>();
        }
        else
        {
            // Derived components (SkeletonComponent, ...) are intentionally not
            ScopedOps.layoutHash = Reflect::Fnv1a(ScopedLabel);
        }
        ScopedOps.get = &Scene::Detail::ComponentOpsFor<T>::Get;
        ScopedOps.getMutable = &Scene::Detail::ComponentOpsFor<T>::GetMutable;
        ScopedOps.has = &Scene::Detail::ComponentOpsFor<T>::Has;
        return RegisterComponentOps(ScopedOps);
    }

    // Registers a non-ECS buffer (project/editor settings, a timeline document)
    [[nodiscard]] std::uint16_t RegisterExternalBuffer(const char* InName, void* InData, std::uint32_t InSize) noexcept;

    [[nodiscard]] std::uint16_t CodecForLayoutHash(std::uint64_t InLayoutHash) const noexcept;
    [[nodiscard]] const char* CodecName(std::uint16_t InCodec) const noexcept;
    // Canonical byte size of a codec, or 0 for an unknown codec. The
    [[nodiscard]] std::uint32_t CodecSize(std::uint16_t InCodec) const noexcept;

    [[nodiscard]] Result BeginTransaction(std::string_view InLabel) noexcept;
    // Records one component edit. `oldBytes`/`newBytes` must have codec size;
    [[nodiscard]] Result Record(Entity InEntity, std::uint16_t InCodec, std::span<const std::byte> InOldBytes,
                                std::span<const std::byte> InNewBytes) noexcept;
    // Convenience: reads the component's current bytes as the "new" side.
    [[nodiscard]] Result RecordAfterMutation(Entity InEntity, std::uint16_t InCodec,
                                             std::span<const std::byte> InOldBytes) noexcept;
    // Records a structural spawn. The entity must already exist; the component
    [[nodiscard]] Result RecordSpawn(Entity InEntity) noexcept;
    // Snapshots the entity's components now; the caller destroys the entity
    // afterwards. Undo recreates it with the same identifier.
    [[nodiscard]] Result RecordDestroy(Entity InEntity) noexcept;
    [[nodiscard]] Result CommitTransaction() noexcept;
    void CancelTransaction() noexcept;
    [[nodiscard]] bool InTransaction() const noexcept
    {
        return MTransactionOpen;
    }

    [[nodiscard]] bool Undo() noexcept;
    [[nodiscard]] bool Redo() noexcept;
    [[nodiscard]] bool CanUndo() const noexcept
    {
        return MCursor > 0;
    }
    [[nodiscard]] bool CanRedo() const noexcept
    {
        return MCursor < MCommandCount;
    }
    void ClearHistory() noexcept;

    [[nodiscard]] Result CaptureComponent(Entity InEntity, std::uint16_t InCodec,
                                          std::span<std::byte> Out) const noexcept;
    [[nodiscard]] Result ApplyComponentBytes(Entity InEntity, std::uint16_t InCodec,
                                             std::span<const std::byte> InBytes) noexcept;

    [[nodiscard]] std::string_view UndoLabel() const noexcept;
    [[nodiscard]] std::string_view RedoLabel() const noexcept;
    [[nodiscard]] const Stats& GetStats() const noexcept
    {
        return MStats;
    }
    void SetObserver(ApplyObserver InObserver, void* InUser) noexcept
    {
        MObserver = InObserver;
        MObserverUser = InUser;
    }

private:
    struct Codec
    {
        std::string_view name{};
        std::uint64_t layoutHash = 0;
        std::uint32_t size = 0;
        const void* (*get)(const entt::registry&, Entity) = nullptr;
        void* (*getMutable)(entt::registry&, Entity) = nullptr;
        bool (*has)(const entt::registry&, Entity) = nullptr;
        void (*emplaceBytes)(entt::registry&, Entity, const void*) = nullptr;
        void (*remove)(entt::registry&, Entity) = nullptr;
        // Non-null for an external buffer registered with RegisterExternalBuffer.
        void* external = nullptr;
    };

    struct Command
    {
        Entity entity = entt::null;
        std::uint16_t codec = 0;
        std::uint16_t rangeCount = 0;
        // >0 only on the first command of a transaction
        std::uint16_t groupSize = 1;
        CommandKind kind = CommandKind::Component;
        std::uint8_t reserved = 0;
        std::uint32_t payloadOffset = 0;
        std::uint32_t payloadSize = 0;
        std::uint32_t labelHash = 0;
        char label[kMaxCommandLabel]{};
    };

    struct Range
    {
        std::uint16_t offset = 0;
        std::uint16_t length = 0;
    };

    struct PendingEdit
    {
        Entity entity = entt::null;
        std::uint16_t codec = 0;
        std::uint32_t oldOffset = 0;
        std::uint32_t newOffset = 0;
        bool active = false;
    };

    struct PendingSpawn
    {
        Entity entity = entt::null;
        bool active = false;
    };

    // Serialized spawn payload:
    struct SpawnHeader
    {
        std::uint16_t componentCount = 0;
        std::uint16_t reserved = 0;
        std::uint32_t byteCount = 0;
    };

    struct SpawnComponent
    {
        std::uint16_t codec = 0;
        std::uint16_t reserved = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    [[nodiscard]] Result PushCommand(const Command& InCommand, std::span<const std::byte> InPayload) noexcept;
    [[nodiscard]] bool ApplyCommand(const Command& InCommand, bool InUseNewBytes) noexcept;
    [[nodiscard]] bool ApplySpawn(const Command& InCommand, bool InUseNewBytes) noexcept;
    [[nodiscard]] bool ApplyDestroy(const Command& InCommand, bool InUseNewBytes) noexcept;
    [[nodiscard]] bool ApplySpawnSnapshot(const Command& InCommand) noexcept;
    [[nodiscard]] std::size_t MeasureSpawn(Entity InEntity, bool* InValid) const noexcept;
    [[nodiscard]] Result BuildSpawn(Entity InEntity, const Command& InBase, Command* Out) noexcept;
    [[nodiscard]] const Codec* CodecAt(std::uint16_t InCodec) const noexcept;
    [[nodiscard]] bool CodecFits(const Codec& InCodec) const noexcept;
    void ResetTransactionBuffer() noexcept;

    std::array<Codec, kMaxCommandCodecs> MCodecs{};
    std::uint32_t MCodecCount = 0;
    std::array<Command, kMaxUndoCommands> MCommands{};
    std::uint32_t MCommandCount = 0;
    std::uint32_t MCursor = 0;
    std::uint32_t MArenaUsed = 0;
    std::byte* MPayload = nullptr;
    std::size_t MPayloadCapacity = 0;
    std::size_t MTransactionStart = 0;
    std::array<PendingEdit, kMaxTransactionEdits> MPending{};
    std::uint32_t MPendingCount = 0;
    std::array<PendingSpawn, kMaxSpawnedEntitiesPerTransaction> MPendingSpawns{};
    std::uint32_t MPendingSpawnCount = 0;
    std::array<PendingSpawn, kMaxSpawnedEntitiesPerTransaction> MPendingDestroys{};
    std::uint32_t MPendingDestroyCount = 0;
    std::array<Command, kMaxSpawnedEntitiesPerTransaction> MPendingDestroyCommands{};
    entt::registry* MRegistry = nullptr;
    const Scene::ComponentRegistry* MComponents = nullptr;
    ApplyObserver MObserver = nullptr;
    void* MObserverUser = nullptr;
    Stats MStats{};
    bool MTransactionOpen = false;
    char MTransactionLabel[kMaxCommandLabel]{};
};
}
