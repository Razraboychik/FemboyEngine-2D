// FemboyEngine2D - integration tests.
#include "Engine/ECS/RegistryOps.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/ECS/SnapshotWorld.hpp"
#include "Engine/ECS/StableIds.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"
#include "Engine/Physics/SnapshotRemap.hpp"
#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/PinkUI.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

[[nodiscard]] std::vector<std::byte> MakeTestFont()
{
    using namespace Fby::Ui;
    constexpr std::uint32_t ScopedKWidth = 32;
    constexpr std::uint32_t ScopedKHeight = 32;
    constexpr std::uint32_t ScopedKGlyphCount = 2;
    const std::size_t ScopedPixelBytes = ScopedKWidth * ScopedKHeight * 4;
    std::vector<std::byte> ScopedBytes(
        sizeof(FontAtlasHeader) + ScopedKGlyphCount * sizeof(GlyphRecord) + ScopedPixelBytes, std::byte{0});

    FontAtlasHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic));
    ScopedHeader.version = kFontAtlasVersion;
    ScopedHeader.headerBytes = sizeof(FontAtlasHeader);
    ScopedHeader.glyphCount = ScopedKGlyphCount;
    ScopedHeader.atlasWidth = ScopedKWidth;
    ScopedHeader.atlasHeight = ScopedKHeight;
    ScopedHeader.pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::Rgba8);
    ScopedHeader.glyphTableOffset = sizeof(FontAtlasHeader);
    ScopedHeader.pixelOffset =
        static_cast<std::uint32_t>(sizeof(FontAtlasHeader) + ScopedKGlyphCount * sizeof(GlyphRecord));
    ScopedHeader.lineHeight = 16.0f;
    ScopedHeader.ascent = 12.0f;
    ScopedHeader.descent = 4.0f;
    ScopedHeader.fontId = 1;
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));

    GlyphRecord ScopedGlyphs[ScopedKGlyphCount]{};
    ScopedGlyphs[0].codepoint = 'A';
    ScopedGlyphs[0].u0 = 0.0f;
    ScopedGlyphs[0].v0 = 0.0f;
    ScopedGlyphs[0].u1 = 0.5f;
    ScopedGlyphs[0].v1 = 0.5f;
    ScopedGlyphs[0].advance = 9.0f;
    ScopedGlyphs[0].bearingX = 0.0f;
    ScopedGlyphs[0].bearingY = 12.0f;
    ScopedGlyphs[0].width = 8.0f;
    ScopedGlyphs[0].height = 12.0f;
    ScopedGlyphs[1] = ScopedGlyphs[0];
    ScopedGlyphs[1].codepoint = 'B';
    ScopedGlyphs[1].u0 = 0.5f;
    ScopedGlyphs[1].u1 = 1.0f;
    std::memcpy(ScopedBytes.data() + sizeof(FontAtlasHeader), ScopedGlyphs, sizeof(ScopedGlyphs));
    return ScopedBytes;
}
}

int main()
{
    using namespace Fby;

    const std::vector<std::byte> ScopedFontBytes = MakeTestFont();
    Ui::FontAtlasSet ScopedFonts;
    FBY_CHECK(ScopedFonts.Add(ScopedFontBytes));
    FBY_CHECK(ScopedFonts.Count() == 1);
    FBY_CHECK(ScopedFonts.At(0)->Glyph('A') != nullptr);
    // Strict Pink UI contract: cold-start mount before the first BuildFrame.
    Rhi::DeviceDesc ScopedGpuDesc;
    ScopedGpuDesc.backend = Rhi::Backend::Null;
    const std::unique_ptr<Rhi::RHI> ScopedGpu = Rhi::RHI::Create(ScopedGpuDesc);
    FBY_CHECK(ScopedGpu != nullptr);
    Ui::WidgetCanvasSystem ScopedCanvasSystem{&ScopedFonts};
    FBY_CHECK(ScopedCanvasSystem.MountFonts(*ScopedGpu));
    FBY_CHECK(ScopedFonts.IsCurrent());

    Ecs::Registry ScopedRegistry;
    JobSystem ScopedJobs(1);
    Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.fonts = &ScopedFonts;

    Ui::WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{320.0f, 120.0f});
    Ui::WidgetDesc ScopedLabel{};
    ScopedLabel.kind = Ui::WidgetKind::Label;
    ScopedLabel.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedLabel.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedLabel.offsetMin = Vector2D{8.0f, 8.0f};
    ScopedLabel.offsetMax = Vector2D{200.0f, 40.0f};
    ScopedLabel.text = "AB";
    ScopedLabel.fontSize = 32;
    ScopedLabel.fontAtlas = 0;
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedLabel) == 0);

    std::array<Rhi::RenderItem, 16> ScopedItems{};
    std::array<Rhi::RenderKey, 16> ScopedKeys{};
    std::array<Rhi::DrawBatch, 2> ScopedBatches{};
    Ui::FrameOutput ScopedOutput{ScopedItems, ScopedKeys, ScopedBatches, 0};
    FBY_CHECK(Ui::WidgetCanvasSystem::BuildFrame(ScopedContext, ScopedCanvas, ScopedOutput) == Ui::BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 2);
    FBY_CHECK(ScopedItems[0].size.X > 0.0f && ScopedItems[0].uvMax.X <= 1.0f);

    auto ScopedHyperCanvas = std::make_unique<Ui::WidgetCanvas>();
    ScopedHyperCanvas->SetCanvasSize(Vector2D{320.0f, 120.0f});
    Ui::WidgetHyperlink ScopedLink{0, 4, 0x1234u, 0, 0};
    Ui::WidgetDesc ScopedHyper{};
    ScopedHyper.kind = Ui::WidgetKind::Hypertext;
    ScopedHyper.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedHyper.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedHyper.offsetMin = Vector2D{8.0f, 8.0f};
    ScopedHyper.offsetMax = Vector2D{200.0f, 40.0f};
    ScopedHyper.text = "link";
    ScopedHyper.fontSize = 16;
    ScopedHyper.hyperlinks = std::span<const Ui::WidgetHyperlink>(&ScopedLink, 1);
    FBY_CHECK(ScopedHyperCanvas->AddWidget(ScopedHyper) == 0);
    FBY_CHECK(Ui::HyperlinkAtByte(*ScopedHyperCanvas, 0, 2) != nullptr);
    std::uint32_t ScopedByteOffset = 0;
    const Ui::WidgetHyperlink* ScopedHit =
        Ui::HitTestHyperlink(*ScopedHyperCanvas, Vector2D{10.0f, 12.0f}, nullptr, &ScopedByteOffset);
    FBY_CHECK(ScopedHit != nullptr);
    FBY_CHECK(ScopedByteOffset < 4);

    const Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedEntity, Transform2D{Vector2D{1.0f, 2.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    const Ecs::SnapshotSchema ScopedSchema = Ecs::SnapshotSchema::BuiltIns();
    auto ScopedWorld = std::make_unique<Ecs::WorldSnapshotV3>();
    ScopedWorld->chainCount = 0;
    std::vector<std::byte> ScopedWorldBytes(Ecs::WorldSnapshotBytes());
    FBY_CHECK(Ecs::EncodeWorldSnapshot(*ScopedWorld, ScopedWorldBytes));

    DeterministicRng ScopedRng = DeterministicRng::FromSeed(7u, 3u);
    std::vector<std::byte> ScopedSnapshot(Ecs::SnapshotSize(ScopedRegistry, ScopedSchema, ScopedWorldBytes.size()) +
                                          64);
    std::size_t ScopedSnapshotBytes = 0;
    FBY_CHECK(Ecs::CaptureSnapshot(ScopedRegistry, ScopedSchema, ScopedSnapshot, 9, ScopedRng, &ScopedSnapshotBytes,
                                   Ecs::SnapshotExtras{ScopedWorldBytes}));
    ScopedSnapshot.resize(ScopedSnapshotBytes);

    Ecs::Registry ScopedRestored;
    std::vector<std::byte> ScopedRestoredWorld(ScopedWorldBytes.size());
    DeterministicRng ScopedRestoredRng;
    FBY_CHECK(Ecs::RestoreSnapshot(ScopedRestored, ScopedSchema, ScopedSnapshot, &ScopedRestoredRng,
                                   Ecs::SnapshotRestoreExtras{ScopedRestoredWorld}));
    auto ScopedDecoded = std::make_unique<Ecs::WorldSnapshotV3>();
    FBY_CHECK(Ecs::DecodeWorldSnapshot(ScopedRestoredWorld, *ScopedDecoded));
    FBY_CHECK(ScopedDecoded->chainCount == 0);
    FBY_CHECK(ScopedRestoredRng.Save() == ScopedRng.Save());

    // Damping is authored state and must survive the snapshot round trip.
    {
        Ecs::Registry ScopedDampRegistry;
        const Entity ScopedDampEntity = ScopedDampRegistry.create();
        ScopedDampRegistry.emplace<Transform2D>(
            ScopedDampEntity, Transform2D{Vector2D{1.0f, 2.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
        RigidBody2D ScopedDamped{};
        ScopedDamped.LinearDamping = 0.25f;
        ScopedDamped.AngularDamping = 0.5f;
        ScopedDampRegistry.emplace<RigidBody2D>(ScopedDampEntity, ScopedDamped);
        std::vector<std::byte> ScopedDampBytes(
            Ecs::SnapshotSize(ScopedDampRegistry, ScopedSchema, 0) + 64);
        DeterministicRng ScopedDampRng = DeterministicRng::FromSeed(5u, 1u);
        std::size_t ScopedDampBytesWritten = 0;
        FBY_CHECK(Ecs::CaptureSnapshot(ScopedDampRegistry, ScopedSchema, ScopedDampBytes, 3, ScopedDampRng,
                                       &ScopedDampBytesWritten));
        ScopedDampBytes.resize(ScopedDampBytesWritten);
        Ecs::Registry ScopedDampRestored;
        DeterministicRng ScopedDampRestoreRng;
        FBY_CHECK(Ecs::RestoreSnapshot(ScopedDampRestored, ScopedSchema, ScopedDampBytes, &ScopedDampRestoreRng));
        const auto* ScopedRestoredBody = ScopedDampRestored.try_get<RigidBody2D>(ScopedDampEntity);
        FBY_CHECK(ScopedRestoredBody != nullptr);
        if (ScopedRestoredBody != nullptr)
        {
            FBY_CHECK(ScopedRestoredBody->LinearDamping == 0.25f);
            FBY_CHECK(ScopedRestoredBody->AngularDamping == 0.5f);
        }
    }

    // A delta restore must keep the base world extension even when the delta
    // carries no extension of its own.
    {
        Ecs::Registry ScopedDeltaRegistry;
        const Entity ScopedDeltaEntity = ScopedDeltaRegistry.create();
        ScopedDeltaRegistry.emplace<Transform2D>(
            ScopedDeltaEntity, Transform2D{Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
        auto ScopedDeltaWorld = std::make_unique<Ecs::WorldSnapshotV3>();
        std::vector<std::byte> ScopedDeltaWorldBytes(Ecs::WorldSnapshotBytes());
        FBY_CHECK(Ecs::EncodeWorldSnapshot(*ScopedDeltaWorld, ScopedDeltaWorldBytes));
        std::vector<std::byte> ScopedDeltaBase(
            Ecs::SnapshotSize(ScopedDeltaRegistry, ScopedSchema, ScopedDeltaWorldBytes.size()) + 64);
        DeterministicRng ScopedDeltaRng = DeterministicRng::FromSeed(9u, 4u);
        std::size_t ScopedDeltaBaseBytes = 0;
        FBY_CHECK(Ecs::CaptureSnapshot(ScopedDeltaRegistry, ScopedSchema, ScopedDeltaBase, 2, ScopedDeltaRng,
                                       &ScopedDeltaBaseBytes, Ecs::SnapshotExtras{ScopedDeltaWorldBytes}));
        ScopedDeltaBase.resize(ScopedDeltaBaseBytes);
        ScopedDeltaRegistry.get<Transform2D>(ScopedDeltaEntity).Position = Vector2D{3.0f, 4.0f};
        std::vector<std::byte> ScopedDeltaBytes(
            Ecs::SnapshotSize(ScopedDeltaRegistry, ScopedSchema, 0) + 64);
        std::size_t ScopedDeltaWritten = 0;
        FBY_CHECK(Ecs::CaptureDelta(ScopedDeltaRegistry, ScopedSchema, ScopedDeltaBase, ScopedDeltaBytes, 3,
                                    ScopedDeltaRng, &ScopedDeltaWritten, Ecs::SnapshotExtras{}));
        ScopedDeltaBytes.resize(ScopedDeltaWritten);
        Ecs::Registry ScopedDeltaRestored;
        std::vector<std::byte> ScopedDeltaOutWorld(ScopedDeltaWorldBytes.size());
        DeterministicRng ScopedDeltaRestoreRng;
        FBY_CHECK(Ecs::RestoreDelta(ScopedDeltaRestored, ScopedSchema, ScopedDeltaBase, ScopedDeltaBytes,
                                    &ScopedDeltaRestoreRng, Ecs::SnapshotRestoreExtras{ScopedDeltaOutWorld}));
        FBY_CHECK(ScopedDeltaOutWorld == ScopedDeltaWorldBytes);

        // A delta-only client remembers the baseline header and refuses a delta
        // that names another base tick or another base checksum.
        Ecs::SnapshotBaseline ScopedBaseline;
        FBY_CHECK(Ecs::RememberBaseline(ScopedBaseline, ScopedDeltaBase));
        Ecs::Registry ScopedAppliedRegistry;
        FBY_CHECK(Ecs::ApplyDeltaChecked(ScopedAppliedRegistry, ScopedSchema, ScopedDeltaBytes, ScopedBaseline));
        Ecs::SnapshotBaseline ScopedWrongTick = ScopedBaseline;
        ++ScopedWrongTick.Tick;
        Ecs::Registry ScopedWrongTickRegistry;
        FBY_CHECK(!Ecs::ApplyDeltaChecked(ScopedWrongTickRegistry, ScopedSchema, ScopedDeltaBytes, ScopedWrongTick));
        Ecs::SnapshotBaseline ScopedWrongChecksum = ScopedBaseline;
        ++ScopedWrongChecksum.PayloadChecksum;
        Ecs::Registry ScopedWrongChecksumRegistry;
        FBY_CHECK(!Ecs::ApplyDeltaChecked(ScopedWrongChecksumRegistry, ScopedSchema, ScopedDeltaBytes,
                                          ScopedWrongChecksum));
        Ecs::SnapshotBaseline ScopedInvalid;
        FBY_CHECK(!Ecs::ApplyDeltaChecked(ScopedWrongTickRegistry, ScopedSchema, ScopedDeltaBytes, ScopedInvalid));

        // A base whose tick was altered must be rejected even though the payload
        // checksum still matches: the delta names the exact base frame.
        std::vector<std::byte> ScopedForgedBase = ScopedDeltaBase;
        Ecs::SnapshotHeader ScopedForgedHeader{};
        std::memcpy(&ScopedForgedHeader, ScopedForgedBase.data(), sizeof(ScopedForgedHeader));
        ++ScopedForgedHeader.tick;
        std::memcpy(ScopedForgedBase.data(), &ScopedForgedHeader, sizeof(ScopedForgedHeader));
        Ecs::Registry ScopedForgedRestored;
        DeterministicRng ScopedForgedRng;
        FBY_CHECK(!Ecs::RestoreDelta(ScopedForgedRestored, ScopedSchema, ScopedForgedBase, ScopedDeltaBytes,
                                     &ScopedForgedRng, Ecs::SnapshotRestoreExtras{}));
    }

    // A checksum-valid snapshot with an invalid entity id is rejected before
    // the registry is cleared.
    {
        std::vector<std::byte> ScopedBad = ScopedSnapshot;
        Ecs::SnapshotHeader ScopedHeader{};
        std::memcpy(&ScopedHeader, ScopedBad.data(), sizeof(ScopedHeader));
        const std::size_t ScopedPayloadOffset = sizeof(ScopedHeader);
        FBY_CHECK(ScopedHeader.payloadBytes >= sizeof(Entity));
        const Entity ScopedNull = entt::null;
        std::memcpy(ScopedBad.data() + ScopedPayloadOffset, &ScopedNull, sizeof(ScopedNull));
        std::uint64_t ScopedHash = 1469598103934665603ull;
        for (std::size_t ScopedI = 0; ScopedI < ScopedHeader.payloadBytes; ++ScopedI)
        {
            ScopedHash ^= static_cast<std::uint64_t>(
                std::to_integer<unsigned char>(ScopedBad[ScopedPayloadOffset + ScopedI]));
            ScopedHash *= 1099511628211ull;
        }
        ScopedHeader.checksum = ScopedHash;
        std::memcpy(ScopedBad.data(), &ScopedHeader, sizeof(ScopedHeader));

        Ecs::Registry ScopedGuard;
        const Entity ScopedSentinel = ScopedGuard.create();
        DeterministicRng ScopedGuardRng;
        FBY_CHECK(!Ecs::RestoreSnapshot(ScopedGuard, ScopedSchema, ScopedBad, &ScopedGuardRng));
        FBY_CHECK(ScopedGuard.valid(ScopedSentinel));
    }

    // A header that claims two kinds at once is rejected before any restore.
    {
        std::vector<std::byte> ScopedBad = ScopedSnapshot;
        Ecs::SnapshotHeader ScopedHeader{};
        std::memcpy(&ScopedHeader, ScopedBad.data(), sizeof(ScopedHeader));
        ScopedHeader.flags = Ecs::SnapshotFull | Ecs::SnapshotDelta;
        std::memcpy(ScopedBad.data(), &ScopedHeader, sizeof(ScopedHeader));
        Ecs::Registry ScopedOther;
        DeterministicRng ScopedOtherRng;
        FBY_CHECK(!Ecs::RestoreSnapshot(ScopedOther, ScopedSchema, ScopedBad, &ScopedOtherRng));
    }

    // A component count that cannot fit the payload is rejected by the parser,
    // not reserved inside a noexcept function.
    {
        std::vector<std::byte> ScopedBad = ScopedSnapshot;
        Ecs::SnapshotHeader ScopedHeader{};
        std::memcpy(&ScopedHeader, ScopedBad.data(), sizeof(ScopedHeader));
        ScopedHeader.componentCount = 0xFFFFFFFFu;
        std::memcpy(ScopedBad.data(), &ScopedHeader, sizeof(ScopedHeader));
        Ecs::Registry ScopedOther;
        DeterministicRng ScopedOtherRng;
        FBY_CHECK(!Ecs::RestoreSnapshot(ScopedOther, ScopedSchema, ScopedBad, &ScopedOtherRng));
    }

    // The world extension validates fluid dimensions on encode as well as
    // decode.
    {
        auto ScopedBadWorld = std::make_unique<Ecs::WorldSnapshotV3>();
        ScopedBadWorld->fluid.columns = 65535u;
        std::vector<std::byte> ScopedBadBytes(Ecs::WorldSnapshotBytes());
        FBY_CHECK(!Ecs::EncodeWorldSnapshot(*ScopedBadWorld, ScopedBadBytes));
    }

    // Capture refuses a world larger than the snapshot table instead of
    // silently truncating it.
    {
        Ecs::Registry ScopedLarge;
        for (std::uint32_t ScopedI = 0; ScopedI < Ecs::kMaxSnapshotEntities + 1u; ++ScopedI)
        {
            (void)ScopedLarge.create();
        }
        std::vector<std::byte> ScopedLargeBytes(Ecs::SnapshotSize(ScopedLarge, ScopedSchema, 0) + 64);
        DeterministicRng ScopedLargeRng = DeterministicRng::FromSeed(11u, 2u);
        FBY_CHECK(!Ecs::CaptureSnapshot(ScopedLarge, ScopedSchema, ScopedLargeBytes, 1, ScopedLargeRng, nullptr));
    }

    PhysicsSystem ScopedPhysics;
    PhysicsDesc ScopedPhysicsDesc;
    ScopedPhysicsDesc.gravity = Vector2D{0.0f, 0.0f};
    FBY_CHECK(ScopedPhysics.Initialize(ScopedPhysicsDesc));
    const Entity ScopedBodyEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedBodyEntity,
                                        Transform2D{Vector2D{10.0f, 20.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    RigidBody2D ScopedBody{};
    ScopedBody.Type = BodyType::Dynamic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedBodyEntity, ScopedBody);
    Collider2D ScopedCollider{};
    ScopedCollider.HalfExtents = Vector2D{8.0f, 8.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedBodyEntity, ScopedCollider);
    ScopedPhysics.EnsureBodies(ScopedRegistry);
    const std::uint64_t ScopedOldBody = ScopedRegistry.get<RigidBody2D>(ScopedBodyEntity).Body;
    FBY_CHECK(ScopedOldBody != 0);
    PhysicsBodyRemapTable ScopedRemap;
    FBY_CHECK(RebuildPhysicsBodies(ScopedRegistry, ScopedPhysics, ScopedRemap));
    FBY_CHECK(ScopedRemap.Count() == 1);
    FBY_CHECK(ScopedRemap.RemapBody(ScopedOldBody) != 0);
    // After a restore the entity is the stable key; a zero serialized id must
    // never match the first row.
    FBY_CHECK(ScopedRemap.FindBySerialized(0) == nullptr);
    FBY_CHECK(ScopedRemap.RemapBodyForEntity(ScopedBodyEntity) != 0);
    FBY_CHECK(ScopedRegistry.get<RigidBody2D>(ScopedBodyEntity).Body != 0);

    PhysicsJointSystem ScopedJoints;
    FBY_CHECK(ScopedJoints.Initialize(ScopedPhysics));
    JointChainDesc ScopedChain;
    ScopedChain.kind = JointChainKind::Rope;
    ScopedChain.flags = JointChainPinStart;
    ScopedChain.start = Vector2D{0.0f, 0.0f};
    ScopedChain.end = Vector2D{0.0f, 64.0f};
    ScopedChain.segmentCount = 4;
    const JointChainHandle ScopedHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedChain);
    FBY_CHECK(ScopedHandle.IsValid());
    FBY_CHECK(ScopedJoints.Info(ScopedHandle).chainId.IsValid());
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedHandle));

    ClothSystem ScopedCloth;
    FBY_CHECK(ScopedCloth.Initialize(nullptr));
    ClothDesc ScopedClothDesc;
    ScopedClothDesc.origin = Vector2D{0.0f, 0.0f};
    ScopedClothDesc.size = Vector2D{64.0f, 64.0f};
    ScopedClothDesc.columns = 4;
    ScopedClothDesc.rows = 4;
    const ClothHandle ScopedClothHandle = ScopedCloth.Create(ScopedRegistry, ScopedClothDesc);
    FBY_CHECK(ScopedClothHandle.IsValid());
    FBY_CHECK(ScopedCloth.Info(ScopedClothHandle).clothId.IsValid());
    Rhi::MeshRenderItem ScopedMesh{};
    FBY_CHECK(ScopedCloth.WriteMesh(ScopedClothHandle, ScopedMesh));
    FBY_CHECK((ScopedMesh.flags & Rhi::MeshItemUnskinned) != 0);
    FBY_CHECK(ScopedMesh.boneCount == 0);

    ScopedCloth.Shutdown(ScopedRegistry);

    // A physics system that never created (or already destroyed) its world must
    // still clear stale native handles in the registry.
    {
        entt::registry ScopedStaleRegistry;
        const Entity ScopedStale = ScopedStaleRegistry.create();
        ScopedStaleRegistry.emplace<RigidBody2D>(ScopedStale).Body = 0x1234u;
        ScopedStaleRegistry.emplace<Collider2D>(ScopedStale).Shape = 0x5678u;
        PhysicsSystem ScopedNoWorld;
        FBY_CHECK(ScopedNoWorld.DestroyAllBodies(&ScopedStaleRegistry) == 0);
        FBY_CHECK(ScopedStaleRegistry.get<RigidBody2D>(ScopedStale).Body == 0);
        FBY_CHECK(ScopedStaleRegistry.get<Collider2D>(ScopedStale).Shape == 0);
    }

    // A registered native cleanup hook runs before the registry is cleared.
    {
        entt::registry ScopedHookRegistry;
        int ScopedCalls = 0;
        Scene::SetNativeCleanupHook(
            ScopedHookRegistry,
            [](void* InUser, entt::registry&) noexcept
            {
                ++*static_cast<int*>(InUser);
            },
            &ScopedCalls);
        (void)ScopedHookRegistry.create();
        Scene::DestroyAllEntities(ScopedHookRegistry);
        FBY_CHECK(ScopedCalls == 1);
        FBY_CHECK(ScopedHookRegistry.storage<Entity>().size() == 0);
    }

    ScopedJoints.Shutdown(ScopedRegistry);
    ScopedPhysics.Shutdown();

    if (GFailures == 0)
    {
        std::printf("p3_integration_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "p3_integration_test: %u failure(s)\n", GFailures);
    return 1;
}
