// FemboyEngine2D - sandbox.
#include "Engine/Physics/SdfCharacter.hpp"
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/AssetPaths.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Core/Pause.hpp"
#include "PieClient.hpp"
#include "SandboxOptions.hpp"

#include "Engine/Assets/Localization.hpp"
#include "Engine/Assets/TextureArray.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Splash.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/ChangeMask.hpp"
#include "Engine/RHI/ChangeMaskExtract.hpp"
#include "Engine/Navigation/CrowdPool.hpp"
#include "Engine/Navigation/CrowdRender.hpp"
#include "Engine/Navigation/PagedNavGrid.hpp"
#include "Engine/Effects/ParticleScene.hpp"
#include "Engine/ECS/ParallelForEach.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/ECS/Hierarchy.hpp"
#include "Engine/Animation/Tween.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/RHI/Sort.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextRender.hpp"
#include "Engine/Text/TextRenderSystem.hpp"
#include "Engine/Text/TextRuntime.hpp"
#include "Engine/Text/TextShapedLayout.hpp"
#include "Engine/Text/TextShapeSystem.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/UI/AccessibilityOverlay.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <algorithm>
#include <filesystem>

#if defined(TOML_ENABLE_FORMATTERS)
#undef TOML_ENABLE_FORMATTERS
#endif
#define TOML_ENABLE_FORMATTERS 1
#include <toml.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr float kFixedStep = 1.0f / 120.0f;
constexpr int kMaxFixedStepsPerFrame = 8;
constexpr float kMaxFrameTime = 0.25f;
constexpr std::uint32_t kChunkSize = 4096;

class RenderExtract
{
public:
    void Resize(std::size_t InCapacity)
    {
        MItems.resize(InCapacity);
        MKeys.resize(InCapacity);
        MOrder.resize(InCapacity);
        MSorted.resize(InCapacity);
        MSortedKeys.resize(InCapacity);
        MCapacity = InCapacity;
    }

    void Begin(std::size_t InCount)
    {
        // Batches survive when FinishSorted reuses a previous key order; the
        // sort path clears them when the key set changes.
        MCount = InCount;
    }

    [[nodiscard]] std::size_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return MCapacity;
    }
    [[nodiscard]] Fby::Rhi::RenderItem* Items() noexcept
    {
        return MItems.data();
    }
    // Sorted view for the CPU accessibility pass: colours only, order untouched.
    [[nodiscard]] std::span<Fby::Rhi::RenderItem> SortedItems() noexcept
    {
        return std::span<Fby::Rhi::RenderItem>(MIdentityOrder ? MItems.data() : MSorted.data(), MCount);
    }
    [[nodiscard]] Fby::Rhi::RenderKey* Keys() noexcept
    {
        return MKeys.data();
    }

    // Sorts the frame by (layer, depth) and packs it into one contiguous array.
    // A dynamic scene usually keeps the same sort keys while positions change;
    // reuse the previous order and batches in that case and only gather items.
    void FinishSorted()
    {
        const bool ScopedSameKeys =
            MHasLast && MCount == MLastCount &&
            (MCount == 0 || std::memcmp(MKeys.data(), MLastKeys.data(),
                                        static_cast<std::size_t>(MCount) * sizeof(Fby::Rhi::RenderKey)) == 0);
        if (!ScopedSameKeys)
        {
            for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
            {
                MOrder[ScopedI] = ScopedI;
            }
            MSorter.Sort(std::span<const Fby::Rhi::RenderKey>(MKeys.data(), MCount),
                         std::span<std::uint32_t>(MOrder.data(), MCount));

            MBatches.clear();
            std::uint32_t ScopedBegin = 0;
            for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
            {
                const bool ScopedLayerChanged =
                    ScopedI > 0 && (MKeys[MOrder[ScopedI]] >> 48) != (MKeys[MOrder[ScopedI - 1]] >> 48);
                if (ScopedLayerChanged)
                {
                    MBatches.push_back(Fby::Rhi::DrawBatch{ScopedBegin, ScopedI - ScopedBegin, 0, 0});
                    ScopedBegin = ScopedI;
                }
            }
            if (MCount > 0)
            {
                MBatches.push_back(Fby::Rhi::DrawBatch{ScopedBegin, MCount - ScopedBegin, 0, 0});
            }
            MIdentityOrder = true;
            for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
            {
                if (MOrder[ScopedI] != ScopedI)
                {
                    MIdentityOrder = false;
                    break;
                }
            }
            MLastKeys.assign(MKeys.begin(), MKeys.begin() + MCount);
            MLastCount = MCount;
            MHasLast = true;
        }

        // Gather only when the sort permutation is not the identity. With one
        // layer and equal depth the common case keeps MItems contiguous.
        if (!MIdentityOrder)
        {
            for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
            {
                MSorted[ScopedI] = MItems[MOrder[ScopedI]];
                MSortedKeys[ScopedI] = MKeys[MOrder[ScopedI]];
            }
        }
    }

    [[nodiscard]] Fby::Rhi::FramePacket Packet(const Fby::Rhi::CameraData& InCamera,
                                               Fby::Color32 InClear) const noexcept
    {
        Fby::Rhi::FramePacket ScopedPacket;
        ScopedPacket.items =
            std::span<const Fby::Rhi::RenderItem>(MIdentityOrder ? MItems.data() : MSorted.data(), MCount);
        ScopedPacket.keys = std::span<const Fby::Rhi::RenderKey>(
            MIdentityOrder ? MKeys.data() : MSortedKeys.data(), MCount);
        ScopedPacket.batches = std::span<const Fby::Rhi::DrawBatch>(MBatches.data(), MBatches.size());
        ScopedPacket.camera = InCamera;
        ScopedPacket.clear = InClear;
        return ScopedPacket;
    }

private:
    std::vector<Fby::Rhi::RenderItem> MItems;
    std::vector<Fby::Rhi::RenderItem> MSorted;
    std::vector<Fby::Rhi::RenderKey> MKeys;
    std::vector<Fby::Rhi::RenderKey> MLastKeys;
    std::vector<Fby::Rhi::RenderKey> MSortedKeys;
    std::vector<std::uint32_t> MOrder;
    std::vector<Fby::Rhi::DrawBatch> MBatches;
    Fby::Rhi::KeySorter MSorter;
    std::size_t MCapacity = 0;
    std::uint32_t MCount = 0;
    std::uint32_t MLastCount = 0;
    bool MHasLast = false;
    bool MIdentityOrder = false;
};

struct World
{
    Fby::Ecs::Registry registry;
    // Stable while no entity is destroyed
    std::vector<Fby::Entity> sprites;
    // The player sprite is the first entity bound to InputSystem user 0.
    Fby::Entity player = Fby::kNullEntity;
    Fby::Vector2D bounds{4000.0f, 2400.0f};
    Fby::Vector2D cameraCenter{2000.0f, 1200.0f};
    float zoom = 0.42f;
};

void SpawnSprites(World& InWorld, std::uint32_t InCount, std::uint32_t InSeed)
{
    InWorld.sprites.reserve(InCount);
    std::uint32_t ScopedState = InSeed != 0 ? InSeed : 0x9E3779B9u;
    auto ScopedNext = [&ScopedState]()
    {
        ScopedState ^= ScopedState << 13;
        ScopedState ^= ScopedState >> 17;
        ScopedState ^= ScopedState << 5;
        return ScopedState;
    };

    for (std::uint32_t ScopedI = 0; ScopedI < InCount; ++ScopedI)
    {
        const auto ScopedEntity = InWorld.registry.create();
        if (ScopedEntity == Fby::kNullEntity)
        {
            Fby::LogError("sandbox", "entity limit reached while spawning sprites at {}", ScopedI);
            break;
        }
        const float ScopedX = static_cast<float>(ScopedNext() % 100000) / 100000.0f * InWorld.bounds.X;
        const float ScopedY = static_cast<float>(ScopedNext() % 100000) / 100000.0f * InWorld.bounds.Y;
        const float ScopedSpeed = 20.0f + static_cast<float>(ScopedNext() % 1000) / 10.0f;
        const float ScopedAngle = static_cast<float>(ScopedNext() % 6283) / 1000.0f;

        InWorld.registry.emplace<Fby::Transform2D>(
            ScopedEntity, Fby::Transform2D{Fby::Vector2D{ScopedX, ScopedY}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
        InWorld.registry.emplace<Fby::Velocity>(
            ScopedEntity,
            Fby::Velocity{Fby::Vector2D{std::cos(ScopedAngle) * ScopedSpeed, std::sin(ScopedAngle) * ScopedSpeed}});
        InWorld.registry.emplace<Fby::Sprite2D>(
            ScopedEntity, Fby::Sprite2D{Fby::Vector2D{16.0f, 16.0f}, Fby::Vector2D{0.0f, 0.0f},
                                        Fby::Vector2D{1.0f, 1.0f}, static_cast<std::uint32_t>(ScopedI % 4),
                                        Fby::Color32::FromBytes(static_cast<std::uint8_t>(80 + ScopedNext() % 176),
                                                                static_cast<std::uint8_t>(80 + ScopedNext() % 176),
                                                                static_cast<std::uint8_t>(80 + ScopedNext() % 176))});
        // Four sorting layers so the batcher has something real to split on.
        InWorld.registry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{static_cast<std::uint16_t>(ScopedI % 4),
                                                                              static_cast<float>(ScopedNext() % 1000)});
        InWorld.sprites.push_back(ScopedEntity);
    }
}

void SpawnParticleEffect(Fby::Fcs::Registry& InRegistry, const char* InPreset, Fby::Vector2D InPosition)
{
    const Fby::Entity ScopedEntity = InRegistry.create();
    if (ScopedEntity == Fby::kNullEntity)
    {
        Fby::LogError("sandbox", "entity limit reached while spawning a particle effect");
        return;
    }
    InRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Transform2D{InPosition, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    Fby::ParticleEffectComponent ScopedComponent{};
    if (FillParticleEffectComponent(ScopedComponent, InPreset))
    {
        InRegistry.emplace<Fby::ParticleEffectComponent>(ScopedEntity, ScopedComponent);
    }
}

void SpawnPlayer(World& InWorld, Fby::Vector2D InPosition)
{
    const Fby::Entity ScopedEntity = InWorld.registry.create();
    if (ScopedEntity == Fby::kNullEntity)
    {
        Fby::LogError("sandbox", "entity limit reached while spawning the player");
        return;
    }
    InWorld.registry.emplace<Fby::Transform2D>(
        ScopedEntity, Fby::Transform2D{InPosition, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    InWorld.registry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{Fby::Vector2D{0.0f, 0.0f}});
    InWorld.registry.emplace<Fby::Sprite2D>(
        ScopedEntity, Fby::Sprite2D{Fby::Vector2D{44.0f, 44.0f}, Fby::Vector2D{0.0f, 0.0f},
                                    Fby::Vector2D{1.0f, 1.0f}, 0u, Fby::Color32::FromBytes(255, 210, 120)});
    InWorld.registry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{3u, 0.0f});
    InWorld.registry.emplace<Fby::InputUserComponent>(ScopedEntity, Fby::InputUserComponent{0u, 0u, 0u});
    InWorld.player = ScopedEntity;
    InWorld.sprites.push_back(ScopedEntity);
}

struct SandboxSystems
{
    World* world = nullptr;
    RenderExtract* extract = nullptr;
    Fby::ParticleSceneSystem* particles = nullptr;
    Fby::InputSystem* input = nullptr;
    std::uint16_t moveAction = Fby::kInvalidInputId;
    Fby::Rhi::ChangeMaskRenderExtract<64>* maskExtract = nullptr;
    Fby::Ecs::Registry* maskWorld = nullptr;
    Fby::Navigation::CrowdPool* crowd = nullptr;
};

void MovementSystem(Fby::Ecs::SystemContext& InContext, void* InUser)
{
    SandboxSystems& ScopedSystems = *static_cast<SandboxSystems*>(InUser);
    auto ScopedView = InContext.registry.view<Fby::Transform2D, Fby::Velocity>();
    const Fby::Vector2D ScopedBounds = ScopedSystems.world->bounds;
    InContext.jobs.ParallelFor(static_cast<std::uint32_t>(ScopedSystems.world->sprites.size()), kChunkSize,
                               [&](std::uint32_t InBegin, std::uint32_t InEnd)
                               {
                                   for (std::uint32_t ScopedI = InBegin; ScopedI < InEnd; ++ScopedI)
                                   {
                                       auto& ScopedTransform =
                                           ScopedView.get<Fby::Transform2D>(ScopedSystems.world->sprites[ScopedI]);
                                       const auto& ScopedVelocity =
                                           ScopedView.get<Fby::Velocity>(ScopedSystems.world->sprites[ScopedI]);
                                       ScopedTransform.Position += ScopedVelocity.Value * InContext.dt;
                                       if (ScopedTransform.Position.X < 0.0f)
                                       {
                                           ScopedTransform.Position.X += ScopedBounds.X;
                                       }
                                       else if (ScopedTransform.Position.X >= ScopedBounds.X)
                                       {
                                           ScopedTransform.Position.X -= ScopedBounds.X;
                                       }
                                       if (ScopedTransform.Position.Y < 0.0f)
                                       {
                                           ScopedTransform.Position.Y += ScopedBounds.Y;
                                       }
                                       else if (ScopedTransform.Position.Y >= ScopedBounds.Y)
                                       {
                                           ScopedTransform.Position.Y -= ScopedBounds.Y;
                                       }
                                   }
                               });
}

void PlayerControlSystem(Fby::Ecs::SystemContext& InContext, void* InUser)
{
    SandboxSystems& ScopedSystems = *static_cast<SandboxSystems*>(InUser);
    if (ScopedSystems.input == nullptr || ScopedSystems.world->player == Fby::kNullEntity)
    {
        return;
    }
    const Fby::InputActionState* ScopedAction = ScopedSystems.input->Action(0u, ScopedSystems.moveAction);
    Fby::Velocity* ScopedVelocity = InContext.registry.try_get<Fby::Velocity>(ScopedSystems.world->player);
    if (ScopedAction == nullptr || ScopedVelocity == nullptr)
    {
        return;
    }
    // The action is the sum of the keyboard and gamepad mappings; clamp so a
    // diagonal keyboard chord cannot exceed the authored speed.
    const float ScopedX = std::clamp(ScopedAction->X, -1.0f, 1.0f);
    const float ScopedY = std::clamp(ScopedAction->Y, -1.0f, 1.0f);
    constexpr float kPlayerSpeed = 420.0f;
    ScopedVelocity->Value = Fby::Vector2D{ScopedX * kPlayerSpeed, ScopedY * kPlayerSpeed};
}

void RenderExtractSystem(const Fby::Ecs::ConstRenderSystemView& InView, void* InUser)
{
    SandboxSystems& ScopedSystems = *static_cast<SandboxSystems*>(InUser);
    const std::size_t ScopedCapacity = ScopedSystems.extract->Capacity();
    const std::uint32_t ScopedCount = static_cast<std::uint32_t>(
        std::min(ScopedSystems.world->sprites.size(), ScopedCapacity));
    if (ScopedSystems.world->sprites.size() > ScopedCapacity)
    {
        static bool ScopedWarned = false;
        if (!ScopedWarned)
        {
            ScopedWarned = true;
            Fby::LogWarn("sandbox", "sprite extract capped at {} of {} sprites",
                         ScopedCapacity, ScopedSystems.world->sprites.size());
        }
    }
    ScopedSystems.extract->Begin(ScopedCount);
    auto ScopedView = InView.registry.view<Fby::Transform2D, Fby::Sprite2D, Fby::DrawOrder>();
    Fby::Rhi::RenderItem* ScopedItems = ScopedSystems.extract->Items();
    Fby::Rhi::RenderKey* ScopedKeys = ScopedSystems.extract->Keys();

    InView.jobs.ParallelFor(ScopedCount, kChunkSize,
                            [&](std::uint32_t InBegin, std::uint32_t InEnd)
                            {
                                for (std::uint32_t ScopedI = InBegin; ScopedI < InEnd; ++ScopedI)
                                {
                                    const Fby::Entity ScopedEntity = ScopedSystems.world->sprites[ScopedI];
                                    const auto& ScopedTransform = ScopedView.get<Fby::Transform2D>(ScopedEntity);
                                    const auto& ScopedSprite = ScopedView.get<Fby::Sprite2D>(ScopedEntity);
                                    const auto& ScopedOrder = ScopedView.get<Fby::DrawOrder>(ScopedEntity);

                                    ScopedItems[ScopedI] = Fby::Rhi::RenderItem{
                                        ScopedTransform.Position, ScopedSprite.Size,  ScopedSprite.UVMin,
                                        ScopedSprite.UVMax,       ScopedSprite.Layer, ScopedSprite.Color.rgba,
                                        ScopedTransform.Rotation, ScopedSprite.Flags};
                                    ScopedKeys[ScopedI] = Fby::Rhi::MakeRenderKey(ScopedOrder.Layer, ScopedOrder.Depth);
                                }
                            });

    std::uint32_t ScopedParticleCount = 0;
    if (ScopedSystems.particles != nullptr)
    {
        static const Fby::ParticleShapeAtlas ScopedParticleAtlas = []
        {
            Fby::ParticleShapeAtlas ScopedAtlas = Fby::ParticleShapeAtlas::Defaults();
            for (Fby::ParticleShapeFrame& ScopedFrame : ScopedAtlas.Frames)
            {
                ScopedFrame.Layer = 0;
            }
            return ScopedAtlas;
        }();
        const std::uint32_t ScopedTail =
            static_cast<std::uint32_t>(ScopedSystems.extract->Capacity() - static_cast<std::size_t>(ScopedCount));
        ScopedParticleCount = ScopedSystems.particles->BuildRenderItems(
            std::span<Fby::Rhi::RenderItem>(ScopedItems + ScopedCount, ScopedTail), ScopedParticleAtlas);
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedParticleCount; ++ScopedI)
        {
            ScopedKeys[ScopedCount + ScopedI] = Fby::Rhi::MakeRenderKey(8, 0);
        }
        ScopedSystems.extract->Begin(ScopedCount + ScopedParticleCount);
    }
    if (ScopedSystems.maskExtract != nullptr && ScopedSystems.maskWorld != nullptr &&
        ScopedSystems.maskExtract->Extract(*ScopedSystems.maskWorld))
    {
        const std::span<const Fby::Rhi::RenderItem> ScopedMaskItems = ScopedSystems.maskExtract->Items();
        const std::span<const Fby::Rhi::RenderKey> ScopedMaskKeys = ScopedSystems.maskExtract->Keys();
        const std::uint32_t ScopedBase = static_cast<std::uint32_t>(ScopedSystems.extract->Count());
        if (ScopedBase + ScopedMaskItems.size() <= ScopedSystems.extract->Capacity())
        {
            Fby::Rhi::RenderItem* ScopedOutputItems = ScopedSystems.extract->Items();
            Fby::Rhi::RenderKey* ScopedOutputKeys = ScopedSystems.extract->Keys();
            for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedMaskItems.size(); ++ScopedIndex)
            {
                ScopedOutputItems[ScopedBase + ScopedIndex] = ScopedMaskItems[ScopedIndex];
                ScopedOutputKeys[ScopedBase + ScopedIndex] = ScopedMaskKeys[ScopedIndex];
            }
            ScopedSystems.extract->Begin(ScopedBase + ScopedMaskItems.size());
        }
    }
    if (ScopedSystems.crowd != nullptr)
    {
        const std::uint32_t ScopedBase = static_cast<std::uint32_t>(ScopedSystems.extract->Count());
        const std::size_t ScopedTail = ScopedSystems.extract->Capacity() - ScopedBase;
        if (ScopedTail > 0)
        {
            Fby::Navigation::CrowdRenderSettings ScopedCrowdRender;
            ScopedCrowdRender.Size = Fby::Vector2D{10.0f, 10.0f};
            // Texture-array slice 0 is the built-in white sprite; layer 6 is
            // only the render-key bucket below.
            ScopedCrowdRender.Layer = 0;
            const std::uint32_t ScopedCrowdCount = Fby::Navigation::BuildCrowdRenderItems(
                *ScopedSystems.crowd, ScopedCrowdRender,
                std::span<Fby::Rhi::RenderItem>(ScopedSystems.extract->Items() + ScopedBase, ScopedTail));
            Fby::Rhi::RenderKey* ScopedCrowdKeys = ScopedSystems.extract->Keys();
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCrowdCount; ++ScopedIndex)
            {
                ScopedCrowdKeys[ScopedBase + ScopedIndex] = Fby::Rhi::MakeRenderKey(6, 0.0f);
            }
            ScopedSystems.extract->Begin(ScopedBase + ScopedCrowdCount);
        }
    }
}

void ExtractRegistry(const Fby::Ecs::Registry& InRegistry, RenderExtract& InExtract, std::size_t InBudget)
{
    auto ScopedView = InRegistry.view<Fby::Transform2D, Fby::Sprite2D, Fby::DrawOrder>();
    const std::size_t ScopedCount = std::min(ScopedView.size_hint(), InBudget);
    if (ScopedCount > InExtract.Capacity())
    {
        InExtract.Resize(ScopedCount);
    }
    InExtract.Begin(ScopedCount);
    Fby::Rhi::RenderItem* ScopedItems = InExtract.Items();
    Fby::Rhi::RenderKey* ScopedKeys = InExtract.Keys();
    std::uint32_t ScopedIndex = 0;
    for (auto [ScopedEntity, ScopedTransform, ScopedSprite, ScopedOrder] : ScopedView.each())
    {
        if (ScopedIndex >= ScopedCount)
        {
            break;
        }
        (void)ScopedEntity;
        ScopedItems[ScopedIndex] = Fby::Rhi::RenderItem{
            ScopedTransform.Position, ScopedSprite.Size,       ScopedSprite.UVMin,       ScopedSprite.UVMax,
            ScopedSprite.Layer,       ScopedSprite.Color.rgba, ScopedTransform.Rotation, ScopedSprite.Flags};
        ScopedKeys[ScopedIndex] = Fby::Rhi::MakeRenderKey(ScopedOrder.Layer, ScopedOrder.Depth);
        ++ScopedIndex;
    }
}

// Headless PIE client: no window, no GPU. It is the CI/manual path for the
// second copy and exits when the host stops publishing snapshots.
[[nodiscard]] int RunHeadlessClient(const Fby::Sandbox::SandboxOptions& InOptions)
{
    Fby::Sandbox::PieClientConfig ScopedConfig;
    ScopedConfig.Host = InOptions.Connect;
    ScopedConfig.ListenPort = InOptions.ListenPort;

    Fby::Sandbox::PieClient ScopedClient;
    std::string ScopedError;
    Fby::Ecs::SnapshotSchema ScopedClientSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
    (void)Fby::RegisterSdfCharacterSnapshotCodec(ScopedClientSchema);
    if (!ScopedClient.Start(ScopedConfig, ScopedClientSchema, &ScopedError))
    {
        Fby::LogError("sandbox", "PIE client start failed: {}", ScopedError);
        return 3;
    }

    Fby::Core::Clock ScopedClock;
    // This loop paces itself with a sleep, so the frame clamp must not clip it.
    ScopedClock.SetMaxDelta(600.0);
    std::uint64_t ScopedNextLogMs = 0;
    while (!ScopedClient.ShouldQuit())
    {
        Fby::Core::SleepMilliseconds(1.0);
        ScopedClock.Tick();
        const float ScopedDt = static_cast<float>(ScopedClock.Delta());
        const auto ScopedNowMs = static_cast<std::uint64_t>(ScopedClock.RealElapsed() * 1000.0);
        ScopedClient.Tick(ScopedNowMs, ScopedDt);
        if (ScopedNowMs >= ScopedNextLogMs)
        {
            ScopedNextLogMs = ScopedNowMs + 1000;
            Fby::LogInfo("sandbox", "PIE client: tick {}, {} entities, state {}",
                         static_cast<unsigned long long>(ScopedClient.AppliedTick()),
                         ScopedClient.World().storage<Fby::Entity>().size(),
                         Fby::Net::ReplicationStatusName(ScopedClient.Status()));
        }
    }
    const bool ScopedTimedOut = ScopedClient.TimedOut();
    ScopedClient.Stop();
    Fby::LogInfo("sandbox", "PIE client stopped ({})", ScopedTimedOut ? "host timeout" : "requested");
    return 0;
}
}

int main(int argc, char** argv)
{
    Fby::Core::InstallCrashHandler("fby_sandbox");
    Fby::SetLogCategory("sandbox");

    Fby::Sandbox::SandboxOptions ScopedOptions;
    std::string ScopedArgumentError;
    if (!Fby::Sandbox::ParseSandboxOptions(argc, argv, &ScopedOptions, &ScopedArgumentError))
    {
        if (!ScopedArgumentError.empty())
        {
            Fby::LogError("sandbox", "{}", ScopedArgumentError);
        }
        return ScopedArgumentError.empty() ? 0 : 1;
    }

    Fby::SetLogLevel(Fby::LogLevel::Info);
    if (ScopedOptions.PieClient)
    {
        Fby::LogInfo("sandbox", "FemboyEngine2D {} PIE client (host {}, listen {})", FBY_ENGINE_VERSION,
                     Fby::Net::AddressToString(ScopedOptions.Connect), ScopedOptions.ListenPort);
        if (ScopedOptions.Headless)
        {
            return RunHeadlessClient(ScopedOptions);
        }
    }
    else
    {
        Fby::LogInfo("sandbox", "FemboyEngine2D {} starting ({} sprites)", FBY_ENGINE_VERSION,
                     ScopedOptions.SpriteCount);
    }

    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "Femboy Engine 2D v" FBY_ENGINE_VERSION;
    ScopedWindowDesc.width = 1600;
    ScopedWindowDesc.height = 900;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }

    Fby::Rhi::DeviceDesc ScopedDeviceDesc;
    ScopedDeviceDesc.backend = Fby::Rhi::SelectDefaultBackend();
    ScopedDeviceDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDeviceDesc.size = {ScopedWindow.Size().width, ScopedWindow.Size().height};
    ScopedDeviceDesc.vsync = !ScopedOptions.NoVsync;
    ScopedDeviceDesc.maxItemsPerFrame =
        std::max<std::uint32_t>(ScopedOptions.PieClient ? 4096u : ScopedOptions.SpriteCount + 4096u, 1u << 16);
    if (ScopedDeviceDesc.backend == Fby::Rhi::Backend::D3D11)
    {
        // D3D11's Submit contract is 4096 items per packet.
        ScopedDeviceDesc.maxItemsPerFrame = std::min<std::uint32_t>(ScopedDeviceDesc.maxItemsPerFrame, 4096u);
    }

    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDeviceDesc);
    if (!ScopedGpu)
    {
        Fby::LogError("sandbox", "no rendering backend could be created");
        return 2;
    }
    Fby::LogInfo("sandbox", "backend: {} ({})", Fby::Rhi::BackendName(ScopedGpu->GetBackend()), ScopedGpu->Info().name);

    // Hardware_concurrency() - 1 workers + this thread
    Fby::JobSystem ScopedJobs;
    Fby::LogInfo("sandbox", "job system: {} worker threads", ScopedJobs.ThreadCount());

    // The VFS maps files instead of copying them; the localization table reads
    Fby::VirtualFileSystem ScopedVfs;
    Fby::LocalizationTable ScopedLocalization;
    std::string ScopedVfsError;
    const std::filesystem::path ScopedAssetRoot =
        Fby::Core::FindAssetRoot(argc > 0 && argv[0] != nullptr ? argv[0] : "");
    if (ScopedAssetRoot.empty())
    {
        Fby::LogWarn("sandbox", "assets directory not found; set FBY_ASSET_DIR or run from the repository root");
    }
    else if (!ScopedVfs.MountDirectory(ScopedAssetRoot.string(), 0, &ScopedVfsError))
    {
        Fby::LogWarn("sandbox", "assets not mounted at '{}' ({})", ScopedAssetRoot.string(), ScopedVfsError);
    }
    else
    {
        Fby::LogInfo("sandbox", "assets: {}", ScopedAssetRoot.string());
        if (const auto ScopedBytes = ScopedVfs.Read("loc/ui.fbin"))
        {
            std::string ScopedError;
            ScopedLocalization = Fby::LocalizationTable::Open(*ScopedBytes, &ScopedError);
            if (ScopedLocalization.IsValid())
            {
                ScopedLocalization.SelectLocale("ru");
                Fby::LogInfo("sandbox", "localization: {} locales, id 1001 (ru) = '{}'",
                             ScopedLocalization.LocaleCount(), ScopedLocalization.FindWithFallback(1001));
            }
            else
            {
                Fby::LogWarn("sandbox", "localization pack rejected: {}", ScopedError);
            }
        }
    }

    if (ScopedOptions.PieClient)
    {
        Fby::Sandbox::PieClientConfig ScopedClientConfig;
        ScopedClientConfig.Host = ScopedOptions.Connect;
        ScopedClientConfig.ListenPort = ScopedOptions.ListenPort;

        Fby::Sandbox::PieClient ScopedClient;
        std::string ScopedClientError;
        Fby::Ecs::SnapshotSchema ScopedPieClientSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
        (void)Fby::RegisterSdfCharacterSnapshotCodec(ScopedPieClientSchema);
        if (!ScopedClient.Start(ScopedClientConfig, ScopedPieClientSchema, &ScopedClientError))
        {
            Fby::LogError("sandbox", "PIE client start failed: {}", ScopedClientError);
            ScopedGpu->Shutdown();
            ScopedWindow.Destroy();
            return 3;
        }
        Fby::LogInfo("sandbox", "PIE client: UDP {} -> host {}", ScopedClient.ListenPort(),
                     Fby::Net::AddressToString(ScopedOptions.Connect));

        RenderExtract ScopedClientExtract;
        Fby::Vector2D ScopedCameraCenter{640.0f, 360.0f};
        float ScopedZoom = 1.0f;
        bool ScopedCameraFromWorld = false;
        Fby::Core::Clock ScopedClientClock;
        std::uint64_t ScopedNextLogMs = 0;
        while (ScopedWindow.PumpEvents() && !ScopedWindow.ShouldClose())
        {
            ScopedClientClock.Tick();
            const float ScopedDt = std::min(static_cast<float>(ScopedClientClock.Delta()), kMaxFrameTime);
            const auto ScopedNowMs = static_cast<std::uint64_t>(ScopedClientClock.RealElapsed() * 1000.0);

            if (ScopedWindow.Input().WasPressed(Fby::Key::Escape))
            {
                break;
            }
            ScopedClient.Tick(ScopedNowMs, ScopedDt);
            if (ScopedClient.ShouldQuit())
            {
                break;
            }

            // The camera latches the replicated Camera2D once, then stays under
            // the user's control so a late baseline does not reset a pan.
            if (!ScopedCameraFromWorld && ScopedClient.HasState())
            {
                for (const Fby::Entity ScopedEntity : ScopedClient.World().view<Fby::Camera2D>())
                {
                    const Fby::Camera2D& ScopedCamera2D = ScopedClient.World().get<Fby::Camera2D>(ScopedEntity);
                    ScopedCameraCenter = ScopedCamera2D.Center;
                    ScopedZoom = ScopedCamera2D.Zoom > 0.0f ? ScopedCamera2D.Zoom : 1.0f;
                    break;
                }
                ScopedCameraFromWorld = true;
            }

            const Fby::InputState& ScopedInput = ScopedWindow.Input();
            const float ScopedPanSpeed = 900.0f / std::max(ScopedZoom, 0.05f) * ScopedDt;
            if (ScopedInput.IsDown(Fby::Key::Left))
            {
                ScopedCameraCenter.X -= ScopedPanSpeed;
            }
            if (ScopedInput.IsDown(Fby::Key::Right))
            {
                ScopedCameraCenter.X += ScopedPanSpeed;
            }
            if (ScopedInput.IsDown(Fby::Key::Up))
            {
                ScopedCameraCenter.Y -= ScopedPanSpeed;
            }
            if (ScopedInput.IsDown(Fby::Key::Down))
            {
                ScopedCameraCenter.Y += ScopedPanSpeed;
            }
            if (ScopedInput.wheel != 0.0f)
            {
                ScopedZoom = std::clamp(ScopedZoom * (1.0f + ScopedInput.wheel * 0.1f), 0.05f, 4.0f);
            }

            const Fby::WindowExtent ScopedSize = ScopedWindow.Size();
            if (ScopedSize.width > 0 && ScopedSize.height > 0 &&
                (ScopedSize.width != ScopedDeviceDesc.size.width || ScopedSize.height != ScopedDeviceDesc.size.height))
            {
                ScopedDeviceDesc.size = {ScopedSize.width, ScopedSize.height};
                ScopedGpu->Resize(ScopedDeviceDesc.size);
            }

            constexpr std::size_t ScopedPieReservedItems =
                Fby::AccessibilityOverlay::kMaxOverlayItems + 2u * Fby::Text::kMaxShapedGlyphs;
            const std::size_t ScopedPieBudget =
                std::max<std::size_t>(ScopedGpu->Info().maxItemsPerFrame > ScopedPieReservedItems
                                          ? ScopedGpu->Info().maxItemsPerFrame - ScopedPieReservedItems
                                          : 0u,
                                      1u);
            ExtractRegistry(ScopedClient.World(), ScopedClientExtract, ScopedPieBudget);
            ScopedClientExtract.FinishSorted();
            if (ScopedGpu->BeginFrame())
            {
                const Fby::Rhi::CameraData ScopedCamera{
                    ScopedCameraCenter,
                    {static_cast<float>(ScopedDeviceDesc.size.width), static_cast<float>(ScopedDeviceDesc.size.height)},
                    ScopedZoom};
                ScopedGpu->Submit(ScopedClientExtract.Packet(ScopedCamera, Fby::Color32::FromBytes(8, 10, 18)));
                ScopedGpu->EndFrame();
            }

            if (ScopedNowMs >= ScopedNextLogMs)
            {
                ScopedNextLogMs = ScopedNowMs + 1000;
                const std::size_t ScopedEntities = ScopedClient.World().storage<Fby::Entity>().size();
                Fby::LogInfo("sandbox", "PIE client: tick {}, {} entities, {} items, state {}",
                             static_cast<unsigned long long>(ScopedClient.AppliedTick()), ScopedEntities,
                             ScopedClientExtract.Count(), Fby::Net::ReplicationStatusName(ScopedClient.Status()));
                ScopedWindow.SetTitle(std::format("FemboyEngine2D 1.0 - PIE client | tick {} | {} items",
                                                  static_cast<unsigned long long>(ScopedClient.AppliedTick()),
                                                  ScopedClientExtract.Count()));
            }
        }

        const bool ScopedTimedOut = ScopedClient.TimedOut();
        ScopedClient.Stop();
        ScopedGpu->WaitIdle();
        ScopedGpu->Shutdown();
        ScopedWindow.Destroy();
        Fby::LogInfo("sandbox", "PIE client stopped ({})", ScopedTimedOut ? "host timeout" : "requested");
        return 0;
    }

    World ScopedWorld;
    Fby::Animation::Tween ScopedZoomTween;
    // A separate registry drives the change-mask extractor; its output is
    // appended to the main extract, so the demo does not duplicate entities.
    Fby::Ecs::Registry ScopedMaskWorld;
    Fby::Rhi::ChangeMaskRenderExtract<64> ScopedMaskExtract;
    std::array<Fby::Entity, 3> ScopedMaskEntities{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 3; ++ScopedIndex)
    {
        const Fby::Entity ScopedEntity = ScopedMaskWorld.create();
        Fby::Transform2D ScopedTransform;
        ScopedTransform.Position = Fby::Vector2D{2000.0f + static_cast<float>(ScopedIndex) * 90.0f, 1200.0f};
        ScopedMaskWorld.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
        Fby::Sprite2D ScopedSprite;
        ScopedSprite.Size = Fby::Vector2D{42.0f, 42.0f};
        ScopedSprite.Color = Fby::Color32::FromBytes(120, 210, 255);
        ScopedMaskWorld.emplace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
        ScopedMaskWorld.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{5u, 0.0f});
        ScopedMaskWorld.emplace<Fby::ComponentChangeMask>(ScopedEntity, Fby::ComponentChangeMask{});
        ScopedMaskEntities[ScopedIndex] = ScopedEntity;
    }
    Fby::Navigation::CrowdPool ScopedCrowd;
    Fby::Navigation::PagedNavGrid ScopedNav;
    Fby::Navigation::CrowdSettings ScopedCrowdSettings;
    (void)ScopedCrowd.Configure(48, 24.0f);
    Fby::Navigation::PagedNavGridDesc ScopedNavDesc;
    ScopedNavDesc.Origin = Fby::Vector2D{1500.0f, 800.0f};
    ScopedNavDesc.CellSize = Fby::Vector2D{8.0f, 8.0f};
    ScopedNavDesc.PagesX = 2;
    ScopedNavDesc.PagesY = 2;
    (void)ScopedNav.Configure(ScopedNavDesc);
    (void)ScopedNav.BuildFlow(80, 60);
    ScopedCrowdSettings.Field = &ScopedNav.Field();
    ScopedCrowdSettings.FieldOrigin = Fby::Vector2D{
        ScopedNavDesc.Origin.X + static_cast<float>(ScopedNav.WindowOriginX()) * ScopedNavDesc.CellSize.X,
        ScopedNavDesc.Origin.Y + static_cast<float>(ScopedNav.WindowOriginY()) * ScopedNavDesc.CellSize.Y};
    ScopedCrowdSettings.FieldCellSize = ScopedNavDesc.CellSize.X;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 48; ++ScopedIndex)
    {
        const float ScopedAngle = static_cast<float>(ScopedIndex) * 0.261799f;
        const Fby::Vector2D ScopedStart{2000.0f + std::cos(ScopedAngle) * 240.0f,
                                        1200.0f + std::sin(ScopedAngle) * 180.0f};
        (void)ScopedCrowd.Add(ScopedStart, ScopedIndex % 4u, Fby::Vector2D{2000.0f, 1200.0f}, 70.0f);
    }
    SpawnSprites(ScopedWorld, ScopedOptions.SpriteCount, 0xC0FFEEu);
    SpawnPlayer(ScopedWorld, ScopedWorld.cameraCenter);

    std::unique_ptr<Fby::ParticleSceneSystem> ScopedParticles = std::make_unique<Fby::ParticleSceneSystem>();
    SpawnParticleEffect(ScopedWorld.registry, "Particle.Fire", Fby::Vector2D{2000.0f, 1200.0f});
    SpawnParticleEffect(ScopedWorld.registry, "Particle.Snow", Fby::Vector2D{1800.0f, 900.0f});
    SpawnParticleEffect(ScopedWorld.registry, "Particle.Bubbles", Fby::Vector2D{2200.0f, 1400.0f});

    RenderExtract ScopedExtract;
    const std::size_t ScopedDesiredItems = ScopedWorld.sprites.size() + Fby::kParticleEffectMaxParticles +
                                           ScopedMaskEntities.size() + ScopedCrowd.Capacity();
    constexpr std::size_t ScopedReservedItems =
        Fby::AccessibilityOverlay::kMaxOverlayItems + 2u * Fby::Text::kMaxShapedGlyphs;
    const std::size_t ScopedItemBudget =
        std::max<std::size_t>(ScopedGpu->Info().maxItemsPerFrame > ScopedReservedItems
                                  ? ScopedGpu->Info().maxItemsPerFrame - ScopedReservedItems
                                  : 0u, 1u);
    if (ScopedDesiredItems > ScopedItemBudget)
    {
        Fby::LogWarn("sandbox", "render extract capped from {} to the backend budget of {} items",
                     ScopedDesiredItems, ScopedItemBudget);
    }
    ScopedExtract.Resize(std::min(ScopedDesiredItems, ScopedItemBudget));

    // Accessibility is a frame policy, not a postscript: the profile is applied
    // once and every frame receives the flags the RHI already understands.
    Fby::AccessibilityKernel ScopedAccessibility;
    Fby::AccessibilityProfile ScopedProfile = Fby::DefaultAccessibilityProfile();
    ScopedAccessibility.ApplyProfile(ScopedProfile);
    // A standalone game owns the voice and publishes its semantic tree to the
    // operating system. Both are project policy and can be switched off.
    ScopedAccessibility.SetSpeechOwner(Fby::NarratorOwner::Game);
    (void)ScopedAccessibility.StartBridge(ScopedWindow.NativeWindow());

    // Caption overlay: the class owns the canvas and the storage, the vectors are
    // reserved once so the frame path never reallocates.
    Fby::AccessibilityOverlay ScopedOverlay;
    std::vector<Fby::Rhi::RenderItem> ScopedFrameItems;
    std::vector<Fby::Rhi::RenderKey> ScopedFrameKeys;
    std::vector<Fby::Rhi::DrawBatch> ScopedFrameBatches;

    Fby::Ecs::SystemGraph ScopedGraph;
    // Host-owned strict text runtime. When the asset VFS can provide the TTF,
    // the UI text path uses a real HarfBuzz script stack; otherwise it falls
    // back to the compatibility shaper.
    Fby::Text::TextRuntime ScopedTextRuntime;
    Fby::Text::TextFontStack ScopedUiFontStack;
    Fby::Text::CodepointShaper ScopedUiFallbackShaper;
    std::string ScopedUiFontError;
    bool ScopedUiFontsReady = false;
    if (ScopedUiFontStack.AddFont(ScopedVfs, "fonts/IBMPlexSans-Regular.ttf", 0, Fby::Text::TextScript::Latin, 32.0f,
                                  1024, false, &ScopedUiFontError))
    {
        ScopedUiFontsReady = ScopedUiFontStack.Fonts().MountToTextureArray(*ScopedGpu);
    }
    if (ScopedUiFontsReady)
    {
        ScopedTextRuntime.SetShaper(&ScopedUiFontStack.Shaper());
    }
    else
    {
        Fby::LogWarn("sandbox", "UI text font stack unavailable ({}); using the codepoint shaper", ScopedUiFontError);
        ScopedTextRuntime.SetShaper(&ScopedUiFallbackShaper);
    }
    // ECS text demo: the world-space label goes through TextShapeSystem and
    // TextRenderSystem exactly like a game label would.
    const Fby::AssetId ScopedTextAsset{0x4659425954455854ull};
    if (!ScopedTextRuntime.Service().AddMarkupAsset(ScopedTextAsset, "[b]FemboyEngine2D[/b]\nECS text pipeline"))
    {
        Fby::LogWarn("sandbox", "text asset could not be compiled");
    }
    const Fby::Entity ScopedWorldTextEntity = ScopedWorld.registry.create();
    if (ScopedWorldTextEntity == Fby::kNullEntity)
    {
        Fby::LogError("sandbox", "entity limit reached while spawning the world text label");
    }
    else
    {
        ScopedWorld.registry.emplace<Fby::Transform2D>(
            ScopedWorldTextEntity,
            Fby::Transform2D{Fby::Vector2D{2000.0f, 1350.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
        Fby::TextComponent ScopedWorldTextComponent;
        ScopedWorldTextComponent.SetRef(ScopedTextRuntime.Service().MakeRef(ScopedTextAsset));
        ScopedWorld.registry.emplace<Fby::TextComponent>(ScopedWorldTextEntity, ScopedWorldTextComponent);
    }
    Fby::Assets::TextureArrayAsset ScopedSplashAtlas;
    {
        const std::string ScopedManifest = "name = \"splash\"\nwidth = 1024\nheight = 1024\nsrgb = false\n"
                                           "layers = [ { name = \"logo\", source = \"Logo.ftex\" } ]\n";
        const auto* ScopedBytes = reinterpret_cast<const std::byte*>(ScopedManifest.data());
        std::string ScopedSplashError;
        if (!ScopedSplashAtlas.LoadFromManifest(ScopedVfs, *ScopedGpu,
                                                std::span<const std::byte>(ScopedBytes, ScopedManifest.size()),
                                                &ScopedSplashError))
        {
            Fby::LogWarn("sandbox", "splash logo unavailable: {}", ScopedSplashError);
        }
    }
    bool ScopedShowSplash = true;
    try
    {
        std::filesystem::path ScopedDescriptor = ScopedAssetRoot / "project.femproj";
        if (!std::filesystem::exists(ScopedDescriptor) && ScopedAssetRoot.has_parent_path())
        {
            ScopedDescriptor = ScopedAssetRoot.parent_path() / "project.femproj";
        }
        if (std::filesystem::exists(ScopedDescriptor))
        {
            const toml::table ScopedTable = toml::parse_file(ScopedDescriptor.string());
            if (const toml::table* ScopedProject = ScopedTable["project"].as_table())
            {
                ScopedShowSplash = (*ScopedProject)["show_splash"].value_or(true);
            }
        }
    }
    catch (const toml::parse_error&)
    {
        Fby::LogWarn("sandbox", "project.femproj could not be read; splash defaults to on");
    }
    Fby::SplashClock ScopedSplash(ScopedShowSplash);
    if (ScopedOptions.Static)
    {
        ScopedSplash.Skip();
    }

    Fby::Ecs::SystemContext ScopedContext{ScopedWorld.registry, ScopedJobs, 0.0f, 0};
    ScopedContext.localization = &ScopedLocalization;
    ScopedContext.text = &ScopedTextRuntime.Service();
    ScopedContext.textShapes = &ScopedTextRuntime.Shapes();
    ScopedContext.textShaper = ScopedTextRuntime.Shaper();
    if (ScopedUiFontsReady)
    {
        ScopedContext.fonts = &ScopedUiFontStack.Fonts();
    }
    Fby::InputSystem ScopedInputMapper;
    const std::uint16_t ScopedMoveAction =
        ScopedInputMapper.RegisterAction("Move", Fby::InputActionType::Axis2D);
    const std::uint16_t ScopedGameplayContext = ScopedInputMapper.RegisterContext("Gameplay", 0, false);
    (void)ScopedInputMapper.BindUser(ScopedWorld.player, 0u, 0u);
    (void)ScopedInputMapper.PushContext(0u, ScopedGameplayContext);
    const auto ScopedAddKey = [&](Fby::Key InKey, bool InVertical, float InSign)
    {
        Fby::InputMappingDesc ScopedMapping;
        ScopedMapping.Action = ScopedMoveAction;
        ScopedMapping.Context = ScopedGameplayContext;
        ScopedMapping.Source.Kind = Fby::InputSourceKind::Key;
        ScopedMapping.Source.Code = static_cast<std::uint16_t>(InKey);
        if (InSign < 0.0f)
        {
            ScopedMapping.Modifiers[ScopedMapping.ModifierCount++] =
                Fby::InputModifier{Fby::InputModifierKind::Scale, -1.0f, 0.0f};
        }
        if (InVertical)
        {
            ScopedMapping.Modifiers[ScopedMapping.ModifierCount++] =
                Fby::InputModifier{Fby::InputModifierKind::Swizzle, 0.0f, 0.0f};
        }
        (void)ScopedInputMapper.AddMapping(ScopedMapping);
    };
    ScopedAddKey(Fby::Key::A, false, -1.0f);
    ScopedAddKey(Fby::Key::D, false, 1.0f);
    ScopedAddKey(Fby::Key::S, true, -1.0f);
    ScopedAddKey(Fby::Key::W, true, 1.0f);
    const auto ScopedAddPadAxis = [&](std::uint16_t InCode, bool InVertical)
    {
        Fby::InputMappingDesc ScopedMapping;
        ScopedMapping.Action = ScopedMoveAction;
        ScopedMapping.Context = ScopedGameplayContext;
        ScopedMapping.Source.Kind = Fby::InputSourceKind::GamepadAxis;
        ScopedMapping.Source.Code = InCode;
        ScopedMapping.Modifiers[ScopedMapping.ModifierCount++] =
            Fby::InputModifier{Fby::InputModifierKind::Deadzone, 0.18f, 0.0f};
        if (InVertical)
        {
            ScopedMapping.Modifiers[ScopedMapping.ModifierCount++] =
                Fby::InputModifier{Fby::InputModifierKind::Swizzle, 0.0f, 0.0f};
        }
        (void)ScopedInputMapper.AddMapping(ScopedMapping);
    };
    ScopedAddPadAxis(0u, false);
    ScopedAddPadAxis(1u, true);

    SandboxSystems ScopedSystems{&ScopedWorld, &ScopedExtract, ScopedParticles.get(), &ScopedInputMapper,
                                 ScopedMoveAction};
    ScopedSystems.maskExtract = &ScopedMaskExtract;
    ScopedSystems.maskWorld = &ScopedMaskWorld;
    ScopedSystems.crowd = &ScopedCrowd;

    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "player_input", &PlayerControlSystem, &ScopedSystems);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "movement", &MovementSystem, &ScopedSystems);
    Fby::Ecs::SystemAccess ScopedExtractAccess;
    ScopedExtractAccess.ComponentReads =
        Fby::Ecs::MakeComponentAccessMask<Fby::Transform2D, Fby::Sprite2D, Fby::DrawOrder,
                                          Fby::ComponentChangeMask>();
    ScopedExtractAccess.ComponentWrites =
        Fby::Ecs::MakeComponentAccessMask<Fby::Transform2D, Fby::Sprite2D, Fby::DrawOrder,
                                          Fby::ComponentChangeMask>();
    ScopedExtractAccess.Writes = Fby::Ecs::kServiceRenderer;
    ScopedExtractAccess.ParallelSafe = true;
    (void)ScopedGraph.Add(Fby::Ecs::Phase::Render, "render_extract",
                          Fby::Ecs::BindConstRenderSystem<&RenderExtractSystem>(), &ScopedSystems, 0, 0,
                          ScopedExtractAccess);
    if (!ScopedGraph.Build())
    {
        Fby::LogError("sandbox", "SystemGraph build failed");
        return 2;
    }

    Fby::Core::Clock ScopedClock;
    Fby::Core::PauseController ScopedPause;
    float ScopedAccumulator = 0.0f;
    std::uint64_t ScopedFrameIndex = 0;
    std::uint64_t ScopedTickIndex = 0;

    double ScopedStatsTimer = 0.0;
    std::uint32_t ScopedStatsFrames = 0;
    bool ScopedShowStats = true;
    std::array<Fby::Rhi::RenderItem, Fby::Text::kMaxShapedGlyphs> ScopedSplashTextItems{};
    std::array<Fby::Rhi::RenderKey, Fby::Text::kMaxShapedGlyphs> ScopedSplashTextKeys{};
    std::uint32_t ScopedSplashTextCount = 0;
    std::array<Fby::Rhi::RenderItem, Fby::Text::kMaxShapedGlyphs> ScopedWorldTextItems{};
    std::array<Fby::Rhi::RenderKey, Fby::Text::kMaxShapedGlyphs> ScopedWorldTextKeys{};
    std::uint32_t ScopedWorldTextCount = 0;

    while (ScopedWindow.PumpEvents() && !ScopedWindow.ShouldClose())
    {
        if (ScopedOptions.Frames > 0 && ScopedFrameIndex >= ScopedOptions.Frames)
        {
            break;
        }
        ScopedClock.Tick();
        float ScopedDt = static_cast<float>(ScopedClock.Delta());
        ScopedDt = std::min(ScopedDt, kMaxFrameTime);

        if (!ScopedSplash.Done())
        {
            ScopedSplash.Tick(ScopedDt);
            const Fby::InputState& ScopedSplashInput = ScopedWindow.Input();
            if (ScopedSplashInput.mousePressed[0] || ScopedSplashInput.mousePressed[1] ||
                ScopedSplashInput.textInputLength > 0)
            {
                ScopedSplash.Skip();
            }
        }

        if (ScopedWindow.Input().WasPressed(Fby::Key::Escape))
        {
            break;
        }
        if (ScopedWindow.Input().WasPressed(Fby::Key::F1))
        {
            ScopedShowStats = !ScopedShowStats;
        }
        if (ScopedWindow.Input().WasPressed(Fby::Key::P))
        {
            const bool ScopedPaused = !ScopedPause.Paused();
            ScopedPause.SetPaused(ScopedPaused, ScopedClock, nullptr);
            ScopedGraph.SetPaused(ScopedPaused, !ScopedPause.Settings().PauseHudAnimations);
            Fby::LogInfo("sandbox", "{}", ScopedPaused ? "paused" : "resumed");
        }
        // Accessibility hotkeys: F2 blind, F3 deaf, F4 flash guard, F5 colour
        // vision cycle. The same profile fields ship in project.femproj.
        if (ScopedWindow.Input().WasPressed(Fby::Key::F2))
        {
            Fby::AccessibilitySetFlag(
                &ScopedProfile.runtime.flags, Fby::AccessibilityBlindMode,
                !Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags, Fby::AccessibilityBlindMode));
            ScopedAccessibility.ApplyProfile(ScopedProfile);
        }
        if (ScopedWindow.Input().WasPressed(Fby::Key::F3))
        {
            const bool ScopedDeaf = !Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags, Fby::AccessibilityDeafMode);
            Fby::AccessibilitySetFlag(&ScopedProfile.runtime.flags, Fby::AccessibilityDeafMode, ScopedDeaf);
            ScopedAccessibility.ApplyProfile(ScopedProfile);
            if (ScopedDeaf)
            {
                // A demo caption so the overlay is visible without game audio: the
                // engine itself ships no sounds, the game publishes the text.
                Fby::SoundTrigger ScopedCaption{};
                ScopedCaption.cueId = 1;
                ScopedCaption.duration = 4.0f;
                ScopedAccessibility.PublishSoundTrigger(ScopedCaption, "Deaf mode on: captions are live");
            }
        }
        if (ScopedWindow.Input().WasPressed(Fby::Key::F4))
        {
            const bool ScopedGuard =
                !Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags, Fby::AccessibilityFlashGuard);
            Fby::AccessibilitySetFlag(&ScopedProfile.runtime.flags, Fby::AccessibilityFlashGuard, ScopedGuard);
            ScopedAccessibility.ApplyProfile(ScopedProfile);
        }
        if (ScopedWindow.Input().WasPressed(Fby::Key::F5))
        {
            const std::uint8_t ScopedNext =
                static_cast<std::uint8_t>((static_cast<std::uint8_t>(ScopedProfile.runtime.colorVision) + 1u) % 4u);
            ScopedProfile.runtime.colorVision = static_cast<Fby::ColorVisionDeficiency>(ScopedNext);
            Fby::AccessibilitySetFlag(&ScopedProfile.runtime.flags, Fby::AccessibilityColorVisionMode, ScopedNext != 0);
            ScopedAccessibility.ApplyProfile(ScopedProfile);
        }

        // Camera: arrow keys pan, wheel zooms.
        const Fby::InputState& ScopedInput = ScopedWindow.Input();
        const float ScopedPanSpeed = 900.0f / std::max(ScopedWorld.zoom, 0.05f) * ScopedDt;
        if (ScopedInput.IsDown(Fby::Key::Left))
        {
            ScopedWorld.cameraCenter.X -= ScopedPanSpeed;
        }
        if (ScopedInput.IsDown(Fby::Key::Right))
        {
            ScopedWorld.cameraCenter.X += ScopedPanSpeed;
        }
        if (ScopedInput.IsDown(Fby::Key::Up))
        {
            ScopedWorld.cameraCenter.Y -= ScopedPanSpeed;
        }
        if (ScopedInput.IsDown(Fby::Key::Down))
        {
            ScopedWorld.cameraCenter.Y += ScopedPanSpeed;
        }
        if (ScopedInput.wheel != 0.0f)
        {
            Fby::Animation::TweenDesc ScopedZoomDesc;
            ScopedZoomDesc.from = ScopedWorld.zoom;
            ScopedZoomDesc.to = std::clamp(ScopedWorld.zoom * (1.0f + ScopedInput.wheel * 0.1f), 0.05f, 4.0f);
            ScopedZoomDesc.durationSeconds = 0.20f;
            ScopedZoomDesc.easing = Fby::Animation::Ease::CubicOut;
            (void)ScopedZoomTween.Start(ScopedZoomDesc);
        }
        if (ScopedZoomTween.Running())
        {
            (void)ScopedZoomTween.Update(ScopedDt, ScopedPause.Paused(), 1.0f);
            ScopedWorld.zoom = ScopedZoomTween.Value();
        }

        // The swapchain and the RHI size follow the drawable size.
        const Fby::WindowExtent ScopedSize = ScopedWindow.Size();
        if (ScopedSize.width > 0 && ScopedSize.height > 0 &&
            (ScopedSize.width != ScopedDeviceDesc.size.width || ScopedSize.height != ScopedDeviceDesc.size.height))
        {
            ScopedDeviceDesc.size = {ScopedSize.width, ScopedSize.height};
            ScopedGpu->Resize(ScopedDeviceDesc.size);
        }

        const bool ScopedFrozen = ScopedOptions.Static && ScopedFrameIndex > 0;
        ++ScopedFrameIndex;
        ScopedContext.frameIndex = ScopedFrameIndex;
        ScopedContext.dt = ScopedDt;
        ScopedContext.uiDt = ScopedPause.HudDelta(ScopedDt, static_cast<float>(ScopedClock.RealDelta()));
        if (!ScopedFrozen)
        {
            ScopedGraph.Run(Fby::Ecs::Phase::PreUpdate, ScopedContext);

            // Fixed step: gameplay sees a constant dt while presentation interpolates.
            ScopedAccumulator += ScopedDt;
            int ScopedSteps = 0;
            Fby::InputState ScopedTickInput = ScopedWindow.Input();
            bool ScopedFirstFixedStep = true;
            while (ScopedAccumulator >= kFixedStep && ScopedSteps < kMaxFixedStepsPerFrame)
            {
                // A frame may run several fixed steps with one device snapshot.
                // Edge flags belong to the first step only, so a single press
                // cannot fire twice inside the same frame.
                if (!ScopedFirstFixedStep)
                {
                    ScopedTickInput.pressed.fill(false);
                    ScopedTickInput.released.fill(false);
                    ScopedTickInput.mousePressed.fill(false);
                    ScopedTickInput.mouseReleased.fill(false);
                    for (Fby::GamepadState& ScopedPad : ScopedTickInput.gamepads)
                    {
                        ScopedPad.pressed.fill(false);
                        ScopedPad.released.fill(false);
                    }
                }
                const std::uint64_t ScopedTimestamp =
                    static_cast<std::uint64_t>(ScopedClock.Elapsed() * 1.0e9);
                ScopedInputMapper.Update(ScopedTickInput, ScopedTickIndex, ScopedTimestamp, kFixedStep);
                ++ScopedTickIndex;
                ScopedFirstFixedStep = false;
                ScopedContext.dt = kFixedStep;
                ScopedGraph.Run(Fby::Ecs::Phase::FixedUpdate, ScopedContext);
                (void)Fby::Ecs::UpdateHierarchy(ScopedWorld.registry);
                ScopedAccumulator -= kFixedStep;
                ++ScopedSteps;
            }
            if (ScopedAccumulator >= kFixedStep)
            {
                ScopedAccumulator = std::fmod(ScopedAccumulator, kFixedStep);
            }

            ScopedContext.dt = ScopedDt;
            ScopedParticles->Update(ScopedWorld.registry, ScopedDt);
            ScopedGraph.Run(Fby::Ecs::Phase::PostUpdate, ScopedContext);
            ScopedGraph.Run(Fby::Ecs::Phase::Ui, ScopedContext);
            ScopedCrowd.Step(ScopedDt, ScopedCrowdSettings);
            const float ScopedMaskTime = static_cast<float>(ScopedClock.Elapsed());
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedMaskEntities.size(); ++ScopedIndex)
            {
                const Fby::Entity ScopedEntity = ScopedMaskEntities[ScopedIndex];
                Fby::Transform2D* ScopedTransform = ScopedMaskWorld.try_get<Fby::Transform2D>(ScopedEntity);
                Fby::ComponentChangeMask* ScopedMask = ScopedMaskWorld.try_get<Fby::ComponentChangeMask>(ScopedEntity);
                if (ScopedTransform == nullptr || ScopedMask == nullptr)
                {
                    continue;
                }
                const float ScopedAngle = ScopedMaskTime * 0.8f + static_cast<float>(ScopedIndex) * 2.0943951f;
                ScopedTransform->Position = Fby::Vector2D{
                    2000.0f + std::cos(ScopedAngle) * 180.0f, 1200.0f + std::sin(ScopedAngle) * 120.0f};
                Fby::Change::Mark(*ScopedMask,
                                  Fby::ChangeMaskBit::Transform2D | Fby::ChangeMaskBit::Sprite2D |
                                      Fby::ChangeMaskBit::DrawOrder,
                                  ScopedContext.frameIndex);
            }
            ScopedGraph.Run(Fby::Ecs::Phase::Render, ScopedContext);
            ScopedExtract.FinishSorted();
            ScopedWorldTextCount = 0;
            if (ScopedUiFontsReady)
            {
                Fby::Text::TextLayoutParams ScopedTextLayout;
                ScopedTextLayout.Fonts = &ScopedUiFontStack.Fonts();
                ScopedTextLayout.FontId = 0;
                ScopedTextLayout.FontSize = 32.0f;
                ScopedTextLayout.MaxWidth = 800.0f;
                (void)Fby::Text::UpdateTextShapes(ScopedWorld.registry, ScopedTextRuntime.Service(),
                                                  ScopedUiFontStack.Shaper(), ScopedTextRuntime.Shapes(), 0u, 32.0f,
                                                  ScopedTextLayout);
                const Fby::Text::TextRenderSystemStats ScopedWorldTextStats = Fby::Text::ExtractTextItems(
                    ScopedWorld.registry, ScopedTextRuntime.Service(), ScopedUiFontStack.Fonts(), 0u, 32.0f,
                    ScopedTextLayout, std::span<Fby::Rhi::RenderItem>(ScopedWorldTextItems), nullptr,
                    &ScopedUiFontStack.Shaper(), &ScopedTextRuntime.Shapes());
                ScopedWorldTextCount = ScopedWorldTextStats.Items;
            }
        }

        if (ScopedGpu->BeginFrame())
        {
            const Fby::Rhi::CameraData ScopedCamera{
                ScopedWorld.cameraCenter,
                {static_cast<float>(ScopedDeviceDesc.size.width), static_cast<float>(ScopedDeviceDesc.size.height)},
                ScopedWorld.zoom};
            const Fby::Rhi::CameraData ScopedSplashCamera{
                {0.0f, 0.0f},
                {static_cast<float>(ScopedDeviceDesc.size.width),
                 static_cast<float>(ScopedDeviceDesc.size.height)},
                1.0f};
            Fby::Color32 ScopedClear = Fby::Color32::FromBytes(10, 12, 20);
            Fby::Rhi::FramePacket ScopedFrame = ScopedExtract.Packet(ScopedCamera, ScopedClear);
            // Keep the splash packet's backing storage in frame scope: Submit()
            // reads these spans after the branch below has closed.
            Fby::Rhi::RenderItem ScopedLogo{};
            Fby::Rhi::RenderKey ScopedLogoKey{};
            Fby::Rhi::DrawBatch ScopedLogoBatch{0, 1, 0, 0};
            if (!ScopedSplash.Done() && ScopedSplashAtlas.IsValid())
            {
                const auto ScopedAlpha =
                    static_cast<std::uint8_t>(std::clamp(ScopedSplash.Opacity(), 0.0f, 1.0f) * 255.0f);
                ScopedLogo = Fby::Rhi::RenderItem::Quad(
                    {0.0f, 0.0f}, {512.0f, 512.0f}, Fby::Color32::FromBytes(255, 255, 255, ScopedAlpha).rgba);
                ScopedLogoKey = Fby::Rhi::RenderKey{};
                ScopedLogoBatch = Fby::Rhi::DrawBatch{0, 1, 0, 0};
                ScopedFrame = Fby::Rhi::FramePacket{};
                ScopedFrame.camera = ScopedSplashCamera;
                ScopedFrame.clear = Fby::Color32::FromBytes(6, 7, 12);
                ScopedFrame.textureArray = ScopedSplashAtlas.Handle().id;
                ScopedFrame.items = std::span<const Fby::Rhi::RenderItem>(&ScopedLogo, 1);
                ScopedFrame.keys = std::span<const Fby::Rhi::RenderKey>(&ScopedLogoKey, 1);
                ScopedFrame.batches = std::span<const Fby::Rhi::DrawBatch>(&ScopedLogoBatch, 1);

                ScopedSplashTextCount = 0;
                if (ScopedUiFontsReady)
                {
                    const std::string ScopedSplashLabel = "Femboy Engine 2D v" FBY_ENGINE_VERSION;
                    Fby::Text::TextShapeParams ScopedShapeParams;
                    ScopedShapeParams.FontId = 0;
                    ScopedShapeParams.PixelSize = 32.0f;
                    ScopedShapeParams.Script = Fby::Text::TextScript::Latin;
                    Fby::Text::TextShapeResult ScopedShaped;
                    if (ScopedUiFontStack.Shaper().Shape(ScopedSplashLabel, ScopedShapeParams, &ScopedShaped))
                    {
                        (void)Fby::Text::EnsureShapedGlyphPages(ScopedUiFontStack.Fonts(), ScopedShaped);
                        Fby::Text::TextLayoutParams ScopedLayoutParams;
                        ScopedLayoutParams.FontSize = 32.0f;
                        ScopedLayoutParams.MaxWidth = 1200.0f;
                        Fby::Text::ShapedLayoutResult ScopedLayout;
                        if (Fby::Text::LayoutShapedGlyphs(ScopedSplashLabel, ScopedShaped, ScopedLayoutParams,
                                                          &ScopedLayout))
                        {
                            Fby::Text::TextRenderParams ScopedRenderParams;
                            ScopedRenderParams.Fonts = &ScopedUiFontStack.Fonts();
                            ScopedRenderParams.FontAtlasIndex = 0;
                            ScopedRenderParams.FontSize = 32.0f;
                            ScopedRenderParams.Color = Fby::Color32::FromBytes(235, 238, 245, ScopedAlpha);
                            Fby::Text::TextRenderStats ScopedTextStats;
                            ScopedSplashTextCount = Fby::Text::BuildShapedTextRenderItems(
                                ScopedLayout, ScopedShaped, Fby::Vector2D{-250.0f, -300.0f}, ScopedRenderParams,
                                std::span<Fby::Rhi::RenderItem>(ScopedSplashTextItems), &ScopedTextStats);
                        }
                    }
                }
            }
            ScopedFrame.timeSeconds = static_cast<float>(ScopedClock.Elapsed());
            ScopedAccessibility.Update(ScopedContext.AccessibilityView());
            ScopedAccessibility.FlushSpeech();
            (void)ScopedOverlay.Build(ScopedContext.UiView(), ScopedAccessibility,
                                      Fby::Vector2D{static_cast<float>(ScopedDeviceDesc.size.width),
                                                    static_cast<float>(ScopedDeviceDesc.size.height)});
            // The caption canvas is the sandbox's semantic surface: projected
            // nodes feed the screen-reader bridge and the narrator.
            ScopedAccessibility.ProjectCanvas(ScopedOverlay.Canvas());
            ScopedFrameItems.resize(ScopedFrame.items.size() + Fby::AccessibilityOverlay::kMaxOverlayItems);
            ScopedFrameKeys.resize(ScopedFrame.keys.size() + Fby::AccessibilityOverlay::kMaxOverlayItems);
            ScopedFrameBatches.resize(ScopedFrame.batches.size() + Fby::AccessibilityOverlay::kMaxOverlayBatches);
            (void)ScopedOverlay.Compose(ScopedFrame, ScopedFrameItems, ScopedFrameKeys, ScopedFrameBatches);
            // Blind mode skips the game and UI draw in the backend; deaf mode is
            // enforced by the app's audio policy, the flags travel for the rest.
            ScopedAccessibility.ApplyToFrame(ScopedFrame);
            ScopedAccessibility.ApplyPostFx(ScopedFrame.postFx);
            ScopedFrame.postFxEnabled = true;
            ScopedGpu->Submit(ScopedFrame);
            if (ScopedSplash.Done() &&
                !Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags, Fby::AccessibilityBlindMode) &&
                ScopedWorldTextCount > 0u && ScopedUiFontStack.Fonts().TextureArray().IsValid())
            {
                Fby::Rhi::FramePacket ScopedWorldText;
                ScopedWorldText.camera = ScopedCamera;
                ScopedWorldText.textureArray = ScopedUiFontStack.Fonts().TextureArray().id;
                ScopedWorldText.items =
                    std::span<const Fby::Rhi::RenderItem>(ScopedWorldTextItems.data(), ScopedWorldTextCount);
                ScopedWorldText.keys =
                    std::span<const Fby::Rhi::RenderKey>(ScopedWorldTextKeys.data(), ScopedWorldTextCount);
                const Fby::Rhi::DrawBatch ScopedWorldTextBatch{0, ScopedWorldTextCount, 0, 0};
                ScopedWorldText.batches = std::span<const Fby::Rhi::DrawBatch>(&ScopedWorldTextBatch, 1);
                (void)ScopedGpu->DrawOverlay(ScopedWorldText);
            }
            if (!Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags, Fby::AccessibilityBlindMode) &&
                !ScopedSplash.Done() && ScopedSplashTextCount > 0u &&
                ScopedUiFontStack.Fonts().TextureArray().IsValid())
            {
                Fby::Rhi::FramePacket ScopedSplashText;
                ScopedSplashText.camera = ScopedSplashCamera;
                ScopedSplashText.textureArray = ScopedUiFontStack.Fonts().TextureArray().id;
                ScopedSplashText.items =
                    std::span<const Fby::Rhi::RenderItem>(ScopedSplashTextItems.data(), ScopedSplashTextCount);
                ScopedSplashText.keys =
                    std::span<const Fby::Rhi::RenderKey>(ScopedSplashTextKeys.data(), ScopedSplashTextCount);
                const Fby::Rhi::DrawBatch ScopedSplashTextBatch{0, ScopedSplashTextCount, 0, 0};
                ScopedSplashText.batches = std::span<const Fby::Rhi::DrawBatch>(&ScopedSplashTextBatch, 1);
                (void)ScopedGpu->DrawOverlay(ScopedSplashText);
            }
            ScopedGpu->EndFrame();
        }

        ScopedStatsTimer += ScopedDt;
        ++ScopedStatsFrames;
        if (ScopedStatsTimer >= 0.5)
        {
            const double ScopedFps = ScopedStatsFrames / ScopedStatsTimer;
            const auto ScopedStats = ScopedGpu->Stats();
            if (ScopedShowStats)
            {
                const std::string ScopedTitle = std::format(
                    "FemboyEngine2D 1.0 | {:.0f} fps | {:.2f} ms | {} items | {} draws | {} batches", ScopedFps,
                    1000.0 / std::max(ScopedFps, 1.0), ScopedStats.items, ScopedStats.drawCalls, ScopedStats.batches);
                ScopedWindow.SetTitle(ScopedTitle);
                Fby::LogInfo("sandbox", "{}", ScopedTitle);
            }
            ScopedStatsTimer = 0.0;
            ScopedStatsFrames = 0;
        }
    }

    ScopedAccessibility.StopBridge();
    ScopedGpu->WaitIdle();
    ScopedGpu->Shutdown();
    ScopedWindow.Destroy();
    Fby::LogInfo("sandbox", "shutdown");
    return 0;
}
