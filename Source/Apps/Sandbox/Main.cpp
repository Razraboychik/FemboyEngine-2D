// FemboyEngine2D - sandbox.
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/AssetPaths.hpp"
#include "Engine/Core/Time.hpp"
#include "PieClient.hpp"
#include "SandboxOptions.hpp"

#include "Engine/Assets/Localization.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/ParallelForEach.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/RHI/Sort.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextRuntime.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/UI/AccessibilityOverlay.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <algorithm>
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

// Render extract
class RenderExtract
{
public:
    void Resize(std::size_t InCapacity)
    {
        MItems.resize(InCapacity);
        MKeys.resize(InCapacity);
        MOrder.resize(InCapacity);
        MSorted.resize(InCapacity);
        MCapacity = InCapacity;
    }

    void Begin(std::size_t InCount)
    {
        MCount = InCount;
        MBatches.clear();
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
        return std::span<Fby::Rhi::RenderItem>(MSorted.data(), MCount);
    }
    [[nodiscard]] Fby::Rhi::RenderKey* Keys() noexcept
    {
        return MKeys.data();
    }

    // Sorts the frame by (layer, depth) and packs it into one contiguous array.
    void FinishSorted()
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            MOrder[ScopedI] = ScopedI;
        }
        MSorter.Sort(std::span<const Fby::Rhi::RenderKey>(MKeys.data(), MCount),
                     std::span<std::uint32_t>(MOrder.data(), MCount));

        // Gather: one linear pass, cache friendly because it is a sequential
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            MSorted[ScopedI] = MItems[MOrder[ScopedI]];
        }

        // One batch per layer change. With a texture array there is no reason to
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
    }

    [[nodiscard]] Fby::Rhi::FramePacket Packet(const Fby::Rhi::CameraData& InCamera,
                                               Fby::Color32 InClear) const noexcept
    {
        return Fby::Rhi::FramePacket{std::span<const Fby::Rhi::RenderItem>(MSorted.data(), MCount),
                                     std::span<const Fby::Rhi::RenderKey>(MKeys.data(), MCount),
                                     std::span<const Fby::Rhi::DrawBatch>(MBatches.data(), MBatches.size()), InCamera,
                                     InClear};
    }

private:
    std::vector<Fby::Rhi::RenderItem> MItems;
    std::vector<Fby::Rhi::RenderItem> MSorted;
    std::vector<Fby::Rhi::RenderKey> MKeys;
    std::vector<std::uint32_t> MOrder;
    std::vector<Fby::Rhi::DrawBatch> MBatches;
    Fby::Rhi::KeySorter MSorter;
    std::size_t MCapacity = 0;
    std::uint32_t MCount = 0;
};

struct World
{
    Fby::Ecs::Registry registry;
    // Stable while no entity is destroyed
    std::vector<Fby::Entity> sprites;
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

struct SandboxSystems
{
    World* world = nullptr;
    RenderExtract* extract = nullptr;
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

void RenderExtractSystem(Fby::Ecs::SystemContext& InContext, void* InUser)
{
    SandboxSystems& ScopedSystems = *static_cast<SandboxSystems*>(InUser);
    const std::uint32_t ScopedCount = static_cast<std::uint32_t>(ScopedSystems.world->sprites.size());
    ScopedSystems.extract->Begin(ScopedCount);
    auto ScopedView = InContext.registry.view<Fby::Transform2D, Fby::Sprite2D, Fby::DrawOrder>();
    Fby::Rhi::RenderItem* ScopedItems = ScopedSystems.extract->Items();
    Fby::Rhi::RenderKey* ScopedKeys = ScopedSystems.extract->Keys();

    InContext.jobs.ParallelFor(
        ScopedCount, kChunkSize,
        [&](std::uint32_t InBegin, std::uint32_t InEnd)
        {
            for (std::uint32_t ScopedI = InBegin; ScopedI < InEnd; ++ScopedI)
            {
                const Fby::Entity ScopedEntity = ScopedSystems.world->sprites[ScopedI];
                const auto& ScopedTransform = ScopedView.get<Fby::Transform2D>(ScopedEntity);
                const auto& ScopedSprite = ScopedView.get<Fby::Sprite2D>(ScopedEntity);
                const auto& ScopedOrder = ScopedView.get<Fby::DrawOrder>(ScopedEntity);

                ScopedItems[ScopedI] = Fby::Rhi::RenderItem{
                    ScopedTransform.Position, ScopedSprite.Size,       ScopedSprite.UVMin,       ScopedSprite.UVMax,
                    ScopedSprite.Layer,       ScopedSprite.Color.rgba, ScopedTransform.Rotation, ScopedSprite.Flags};
                ScopedKeys[ScopedI] = Fby::Rhi::MakeRenderKey(ScopedOrder.Layer, ScopedOrder.Depth);
            }
        });
}

void ExtractRegistry(Fby::Ecs::Registry& InRegistry, RenderExtract& InExtract)
{
    auto ScopedView = InRegistry.view<Fby::Transform2D, Fby::Sprite2D, Fby::DrawOrder>();
    const std::size_t ScopedCount = ScopedView.size_hint();
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
    if (!ScopedClient.Start(ScopedConfig, Fby::Ecs::SnapshotSchema::BuiltIns(), &ScopedError))
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
    ScopedWindowDesc.title = "FemboyEngine2D 1.0 - sandbox";
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
    ScopedDeviceDesc.vsync = true;
    ScopedDeviceDesc.maxItemsPerFrame =
        std::max<std::uint32_t>(ScopedOptions.PieClient ? 4096u : ScopedOptions.SpriteCount + 4096u, 1u << 16);

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
        if (!ScopedClient.Start(ScopedClientConfig, Fby::Ecs::SnapshotSchema::BuiltIns(), &ScopedClientError))
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
                (ScopedSize.width != ScopedDeviceDesc.size.width ||
                 ScopedSize.height != ScopedDeviceDesc.size.height))
            {
                ScopedDeviceDesc.size = {ScopedSize.width, ScopedSize.height};
                ScopedGpu->Resize(ScopedDeviceDesc.size);
            }

            ExtractRegistry(ScopedClient.World(), ScopedClientExtract);
            ScopedClientExtract.FinishSorted();
            if (ScopedGpu->BeginFrame())
            {
                const Fby::Rhi::CameraData ScopedCamera{
                    ScopedCameraCenter,
                    {static_cast<float>(ScopedDeviceDesc.size.width),
                     static_cast<float>(ScopedDeviceDesc.size.height)},
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
                             ScopedClientExtract.Count(),
                             Fby::Net::ReplicationStatusName(ScopedClient.Status()));
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
    SpawnSprites(ScopedWorld, ScopedOptions.SpriteCount, 0xC0FFEEu);

    RenderExtract ScopedExtract;
    ScopedExtract.Resize(ScopedWorld.sprites.size());

    // Accessibility is a frame policy, not a postscript: the profile is applied
    // once and every frame receives the flags the RHI already understands.
    Fby::AccessibilityKernel ScopedAccessibility;
    Fby::AccessibilityProfile ScopedProfile = Fby::DefaultAccessibilityProfile();
    ScopedAccessibility.ApplyProfile(ScopedProfile);

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
    if (ScopedUiFontStack.AddFont(ScopedVfs, "fonts/IBMPlexSans-Regular.ttf", 0, Fby::Text::TextScript::Latin,
                                  32.0f, 1024, false, &ScopedUiFontError))
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
    Fby::Ecs::SystemContext ScopedContext{ScopedWorld.registry, ScopedJobs, 0.0f, 0};
    ScopedContext.localization = &ScopedLocalization;
    ScopedContext.text = &ScopedTextRuntime.Service();
    ScopedContext.textShapes = &ScopedTextRuntime.Shapes();
    ScopedContext.textShaper = ScopedTextRuntime.Shaper();
    if (ScopedUiFontsReady)
    {
        ScopedContext.fonts = &ScopedUiFontStack.Fonts();
    }
    SandboxSystems ScopedSystems{&ScopedWorld, &ScopedExtract};

    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "movement", &MovementSystem, &ScopedSystems);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::Render, "render_extract", &RenderExtractSystem, &ScopedSystems);
    if (!ScopedGraph.Build())
    {
        Fby::LogError("sandbox", "SystemGraph build failed");
        return 2;
    }

    Fby::Core::Clock ScopedClock;
    float ScopedAccumulator = 0.0f;
    std::uint64_t ScopedFrameIndex = 0;

    double ScopedStatsTimer = 0.0;
    std::uint32_t ScopedStatsFrames = 0;
    bool ScopedShowStats = true;

    while (ScopedWindow.PumpEvents() && !ScopedWindow.ShouldClose())
    {
        ScopedClock.Tick();
        float ScopedDt = static_cast<float>(ScopedClock.Delta());
        ScopedDt = std::min(ScopedDt, kMaxFrameTime);

        if (ScopedWindow.Input().WasPressed(Fby::Key::Escape))
        {
            break;
        }
        if (ScopedWindow.Input().WasPressed(Fby::Key::F1))
        {
            ScopedShowStats = !ScopedShowStats;
        }
        // Accessibility hotkeys: F2 blind, F3 deaf, F4 flash guard, F5 colour
        // vision cycle. The same profile fields ship in project.femproj.
        if (ScopedWindow.Input().WasPressed(Fby::Key::F2))
        {
            Fby::AccessibilitySetFlag(&ScopedProfile.runtime.flags, Fby::AccessibilityBlindMode,
                                      !Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags,
                                                                 Fby::AccessibilityBlindMode));
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
            const std::uint8_t ScopedNext = static_cast<std::uint8_t>(
                (static_cast<std::uint8_t>(ScopedProfile.runtime.colorVision) + 1u) % 4u);
            ScopedProfile.runtime.colorVision = static_cast<Fby::ColorVisionDeficiency>(ScopedNext);
            Fby::AccessibilitySetFlag(&ScopedProfile.runtime.flags, Fby::AccessibilityColorVisionMode,
                                      ScopedNext != 0);
            ScopedAccessibility.ApplyProfile(ScopedProfile);
        }

        // Camera: arrow keys pan, wheel zooms. The camera is plain data in the
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
            ScopedWorld.zoom = std::clamp(ScopedWorld.zoom * (1.0f + ScopedInput.wheel * 0.1f), 0.05f, 4.0f);
        }

        // The swapchain and the RHI size follow the drawable size, not the
        const Fby::WindowExtent ScopedSize = ScopedWindow.Size();
        if (ScopedSize.width > 0 && ScopedSize.height > 0 &&
            (ScopedSize.width != ScopedDeviceDesc.size.width || ScopedSize.height != ScopedDeviceDesc.size.height))
        {
            ScopedDeviceDesc.size = {ScopedSize.width, ScopedSize.height};
            ScopedGpu->Resize(ScopedDeviceDesc.size);
        }

        ++ScopedFrameIndex;
        ScopedContext.frameIndex = ScopedFrameIndex;
        ScopedContext.dt = ScopedDt;
        ScopedGraph.Run(Fby::Ecs::Phase::PreUpdate, ScopedContext);

        // Fixed step: gameplay sees a constant dt, presentation sees the
        ScopedAccumulator += ScopedDt;
        int ScopedSteps = 0;
        while (ScopedAccumulator >= kFixedStep && ScopedSteps < kMaxFixedStepsPerFrame)
        {
            ScopedContext.dt = kFixedStep;
            ScopedGraph.Run(Fby::Ecs::Phase::FixedUpdate, ScopedContext);
            ScopedAccumulator -= kFixedStep;
            ++ScopedSteps;
        }
        if (ScopedAccumulator >= kFixedStep)
        {
            ScopedAccumulator = std::fmod(ScopedAccumulator, kFixedStep);
        }

        ScopedContext.dt = ScopedDt;
        ScopedGraph.Run(Fby::Ecs::Phase::PostUpdate, ScopedContext);
        ScopedGraph.Run(Fby::Ecs::Phase::Render, ScopedContext);
        ScopedExtract.FinishSorted();

        if (ScopedGpu->BeginFrame())
        {
            const Fby::Rhi::CameraData ScopedCamera{
                ScopedWorld.cameraCenter,
                {static_cast<float>(ScopedDeviceDesc.size.width), static_cast<float>(ScopedDeviceDesc.size.height)},
                ScopedWorld.zoom};
            Fby::Color32 ScopedClear = Fby::Color32::FromBytes(10, 12, 20);
            Fby::Rhi::FramePacket ScopedFrame = ScopedExtract.Packet(ScopedCamera, ScopedClear);
            ScopedFrame.timeSeconds = static_cast<float>(ScopedClock.Elapsed());
            ScopedAccessibility.AdvanceCaptionClock(ScopedDt);
            (void)ScopedOverlay.Build(ScopedContext, ScopedAccessibility,
                                      Fby::Vector2D{static_cast<float>(ScopedDeviceDesc.size.width),
                                                    static_cast<float>(ScopedDeviceDesc.size.height)});
            ScopedFrameItems.resize(ScopedFrame.items.size() + Fby::AccessibilityOverlay::kMaxOverlayItems);
            ScopedFrameKeys.resize(ScopedFrame.keys.size() + Fby::AccessibilityOverlay::kMaxOverlayItems);
            ScopedFrameBatches.resize(ScopedFrame.batches.size() + Fby::AccessibilityOverlay::kMaxOverlayBatches);
            (void)ScopedOverlay.Compose(ScopedFrame, ScopedFrameItems, ScopedFrameKeys, ScopedFrameBatches);
            // Blind mode skips the game and UI draw in the backend; deaf mode is
            // enforced by the app's audio policy, the flags travel for the rest.
            ScopedAccessibility.ApplyToFrame(ScopedFrame);
            ScopedGpu->Submit(ScopedFrame);
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

    ScopedGpu->WaitIdle();
    ScopedGpu->Shutdown();
    ScopedWindow.Destroy();
    Fby::LogInfo("sandbox", "shutdown");
    return 0;
}
