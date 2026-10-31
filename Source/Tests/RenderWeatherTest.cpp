// FemboyEngine2D - render graph, post FX, weather and card-board integration.
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/RHI/CameraStack.hpp"
#include "Engine/RHI/RenderGraph.hpp"
#include "Engine/RHI/RenderPasses.hpp"
#include "Engine/UI/CardBoard.hpp"
#include "Engine/UI/PinkSdf.hpp"
#include "Engine/Weather/Weather.hpp"
#include "Engine/Weather/WeatherPreconciliated.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
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

void EmptyPass(Fby::Rhi::RenderGraphContext&, void*)
{
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Rhi;
    using namespace Fby::Ui;
    using namespace Fby::Weather;

    RenderGraph ScopedGraph;
    RenderPassDesc ScopedFirst{};
    ScopedFirst.name = "Producer";
    ScopedFirst.execute = &EmptyPass;
    ScopedFirst.writes[0] = RenderResourceHash("a");
    ScopedFirst.writeCount = 1;
    FBY_CHECK(ScopedGraph.AddPass(ScopedFirst));
    RenderPassDesc ScopedMiddle{};
    ScopedMiddle.name = "Middle";
    ScopedMiddle.execute = &EmptyPass;
    ScopedMiddle.reads[0] = RenderResourceHash("a");
    ScopedMiddle.readCount = 1;
    ScopedMiddle.writes[0] = RenderResourceHash("b");
    ScopedMiddle.writeCount = 1;
    FBY_CHECK(ScopedGraph.AddPass(ScopedMiddle));
    RenderPassDesc ScopedConsumer{};
    ScopedConsumer.name = "Consumer";
    ScopedConsumer.execute = &EmptyPass;
    ScopedConsumer.reads[0] = RenderResourceHash("b");
    ScopedConsumer.readCount = 1;
    FBY_CHECK(ScopedGraph.AddPass(ScopedConsumer));
    FBY_CHECK(ScopedGraph.Compile());
    FBY_CHECK(ScopedGraph.OrderCount() == 3);
    FBY_CHECK(ScopedGraph.OrderAt(0) == 0);
    FBY_CHECK(ScopedGraph.OrderAt(1) == 1);
    FBY_CHECK(ScopedGraph.OrderAt(2) == 2);

    Ecs::Registry ScopedRegistry;
    JobSystem ScopedJobs(1);
    Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.dt = 1.0f / 120.0f;
    CameraStackSystem ScopedCameras;
    CameraStackDesc ScopedCameraDesc{};
    FBY_CHECK(ScopedCameras.Push(ScopedRegistry, ScopedCameraDesc));
    PostFxSettings ScopedPost{};
    ScopedPost.flags = PostFxCrt | PostFxVignette | PostFxBloom;
    ScopedPost.crtCurvature = 0.1f;
    ScopedCameras.SetPostFx(ScopedPost);
    FBY_CHECK(ScopedCameras.ActivePostFx().crtCurvature == 0.1f);

    SdfShadowDesc ScopedShadow{};
    ScopedShadow.fieldWidth = 128;
    ScopedShadow.fieldHeight = 128;
    ScopedShadow.fieldCellSize = Vector2D{2.0f, 2.0f};
    SdfShadowPass ScopedSdfPass;
    ScopedSdfPass.Initialize(ScopedShadow);
    PostFxPass ScopedPostPass;
    FBY_CHECK(ScopedSdfPass.Build(ScopedGraph));
    FBY_CHECK(ScopedPostPass.Build(ScopedGraph, ScopedCameras));

    WeatherSystem ScopedWeather;
    ScopedWeather.Initialize({});
    WeatherContextComponent ScopedWeatherContext{};
    ScopedWeatherContext.rainIntensity = 0.8f;
    ScopedWeatherContext.cloudCoverage = 0.7f;
    ScopedWeatherContext.fogDensity = 0.03f;
    ScopedWeatherContext.fireIntensity = 0.2f;
    const Entity ScopedWeatherEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<WeatherContextComponent>(ScopedWeatherEntity, ScopedWeatherContext);
    ScopedWeather.Update(ScopedContext);
    const WeatherContextComponent& ScopedResolved = ScopedRegistry.get<WeatherContextComponent>(ScopedWeatherEntity);
    FBY_CHECK(ScopedResolved.resolvedWindX != 0.0f || ScopedResolved.resolvedWindY != 0.0f);
    FBY_CHECK((ScopedResolved.flags & WeatherRain) != 0);

    ClothSystem ScopedCloth;
    FBY_CHECK(ScopedCloth.Initialize(nullptr));
    ClothDesc ScopedClothDesc{};
    ScopedClothDesc.columns = 4;
    ScopedClothDesc.rows = 4;
    ScopedClothDesc.size = Vector2D{64.0f, 64.0f};
    const ClothHandle ScopedClothHandle = ScopedCloth.Create(ScopedRegistry, ScopedClothDesc);
    FBY_CHECK(ScopedClothHandle.IsValid());
    ScopedContext.cloth = &ScopedCloth;
    ScopedWeather.Update(ScopedContext);
    ScopedCloth.Step(1.0f / 120.0f);
    FBY_CHECK(ScopedWeather.WindField().windAndGust.X != 0.0f || ScopedWeather.WindField().windAndGust.Y != 0.0f);

    std::vector<float> ScopedSdfData(64 * 64, 100.0f);
    SdfField ScopedField{};
    ScopedField.samples = ScopedSdfData.data();
    ScopedField.width = 64;
    ScopedField.height = 64;
    ScopedField.origin = Vector2D{-64.0f, -64.0f};
    ScopedField.cellSize = Vector2D{2.0f, 2.0f};
    WeatherPreconciliatedDesc ScopedRainDesc{};
    ScopedRainDesc.particleCapacity = 4096;
    WeatherPreconciliatedSystem ScopedRain;
    ScopedRain.Initialize(ScopedRainDesc);
    ScopedRain.SetSdfField(ScopedField);
    FBY_CHECK(ScopedRain.Update(ScopedContext));
    FBY_CHECK(ScopedRain.Stats().particleCount > 0);
    FBY_CHECK(ScopedRain.Stats().dispatchGroups > 0);
    FBY_CHECK(ScopedRain.Stats().cpuParticleReads == 0);
    FBY_CHECK(ScopedRain.BuildRenderGraph(ScopedGraph));

    WeatherAtmospherePass ScopedAtmosphere;
    FBY_CHECK(ScopedAtmosphere.Build(ScopedGraph, ScopedWeather));
    ColorGradingSettings ScopedGrading{};
    ScopedGrading.intensity = 0.8f;
    ScopedGrading.lutTexture = 0x1234u;
    ColorGradingPass ScopedColorGrading;
    FBY_CHECK(ScopedColorGrading.Build(ScopedGraph, ScopedGrading));
    FBY_CHECK(ScopedGraph.Compile());

    std::array<RenderCommandRecord, 32> ScopedRecords{};
    std::uint32_t ScopedRecordCount = 0;
    RenderGraphContext ScopedRenderContext{};
    ScopedRenderContext.cameras = &ScopedCameras;
    ScopedRenderContext.timeSeconds = 1.0f;
    ScopedRenderContext.extent = Extent2D{320, 180};
    ScopedRenderContext.records = ScopedRecords;
    ScopedRenderContext.recordCount = &ScopedRecordCount;
    ScopedGraph.Execute(ScopedRenderContext);
    FBY_CHECK(ScopedRecordCount >= 7);
    bool ScopedSawRain = false;
    bool ScopedSawPost = false;
    bool ScopedSawGrading = false;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedRecordCount; ++ScopedI)
    {
        const char* ScopedName = ScopedRecords[ScopedI].pipelineName;
        if (ScopedName != nullptr && std::string(ScopedName).find("weather_rain") != std::string::npos)
        {
            ScopedSawRain = true;
        }
        if (ScopedName != nullptr && std::string(ScopedName).find("postfx") != std::string::npos)
        {
            ScopedSawPost = true;
        }
        if (ScopedName != nullptr && std::string(ScopedName).find("color_grading") != std::string::npos)
        {
            ScopedSawGrading = true;
        }
        if (ScopedRecords[ScopedI].kind == RenderPassKind::Graphics)
        {
            FBY_CHECK(ScopedRecords[ScopedI].instanceCount == 1);
        }
    }
    FBY_CHECK(ScopedSawRain);
    FBY_CHECK(ScopedSawPost);
    FBY_CHECK(ScopedSawGrading);

    const Entity ScopedBoard = ScopedRegistry.create();
    CardBoardComponent ScopedBoardData{};
    ScopedBoardData.boardId = 7;
    ScopedBoardData.columns = 2;
    ScopedBoardData.rows = 1;
    ScopedBoardData.origin = Vector2D{100.0f, 100.0f};
    ScopedBoardData.cardSize = Vector2D{72.0f, 104.0f};
    ScopedBoardData.spacing = Vector2D{12.0f, 12.0f};
    ScopedRegistry.emplace<CardBoardComponent>(ScopedBoard, ScopedBoardData);
    const Entity ScopedSlotEntity = ScopedRegistry.create();
    SlotComponent ScopedSlot{};
    ScopedSlot.boardId = 7;
    ScopedSlot.slotIndex = 1;
    ScopedSlot.flags = SlotFaceUp;
    ScopedSlot.cardId = 42;
    ScopedRegistry.emplace<SlotComponent>(ScopedSlotEntity, ScopedSlot);
    CardBoardSystem::Update(ScopedContext);
    const Transform2D& ScopedSlotTransform = ScopedRegistry.get<Transform2D>(ScopedSlotEntity);
    FBY_CHECK(ScopedSlotTransform.Position.X > 100.0f);
    FBY_CHECK(ScopedRegistry.get<SlotComponent>(ScopedSlotEntity).isOccupied);
    FBY_CHECK(ScopedRegistry.get<SlotComponent>(ScopedSlotEntity).position.X > 100.0f);
    std::array<Ui::PinkSdfPrimitive, 8> ScopedPrimitives{};
    const std::uint32_t ScopedPrimitiveCount =
        Ui::BuildCardBoardPrimitives(ScopedRegistry, 7, ScopedPrimitives);
    FBY_CHECK(ScopedPrimitiveCount == 2);
    std::array<Ui::PinkSdfGpuPrimitive, 8> ScopedGpuPrimitives{};
    FBY_CHECK(Ui::PackPinkSdfPrimitives(
                  std::span<const Ui::PinkSdfPrimitive>(ScopedPrimitives.data(), ScopedPrimitiveCount),
                  ScopedGpuPrimitives) == 2);
    FBY_CHECK(ScopedGpuPrimitives[0].rect.Z > 0.0f);

    // SDF shadow mask is a graph pass: it records exactly one compute dispatch at
    // half viewport resolution and nothing when the frame has no SDF shadows.
    FramePacket ScopedSdfFrame{};
    ScopedSdfFrame.camera.center = Vector2D{320.0f, 240.0f};
    ScopedSdfFrame.camera.viewport = Vector2D{640.0f, 480.0f};
    ScopedSdfFrame.camera.zoom = 1.0f;
    std::array<float, 64> ScopedSdfField{};
    ScopedSdfFrame.sdf.field = ScopedSdfField.data();
    ScopedSdfFrame.sdf.width = 8;
    ScopedSdfFrame.sdf.height = 8;
    ScopedSdfFrame.sdf.cellSize = Vector2D{8.0f, 8.0f};
    ScopedSdfFrame.sdf.enabled = true;
    ScopedSdfFrame.sdf.steps = 8;
    ScopedSdfFrame.sdf.occluderCount = 1;
    ScopedSdfFrame.sdf.occluders[0].Shape = Vector4D{1.0f, 2.0f, 3.0f, 0.0f};
    ScopedSdfFrame.sdf.occluders[0].Extents = Vector4D{4.0f, 5.0f, 1.0f, 0.0f};

    SdfShadowPass ScopedShadowPass;
    SdfShadowDesc ScopedShadowDesc;
    ScopedShadowDesc.resolution = Extent2D{320, 240};
    ScopedShadowPass.Initialize(ScopedShadowDesc);
    RenderGraph ScopedShadowGraph;
    FBY_CHECK(ScopedShadowPass.Build(ScopedShadowGraph));
    FBY_CHECK(ScopedShadowGraph.Compile());
    std::array<RenderCommandRecord, 8> ScopedShadowRecords{};
    std::uint32_t ScopedShadowRecordCount = 0;
    RenderGraphContext ScopedSdfContext;
    ScopedSdfContext.frame = &ScopedSdfFrame;
    ScopedSdfContext.extent = Extent2D{640, 480};
    ScopedSdfContext.records = ScopedShadowRecords;
    ScopedSdfContext.recordCount = &ScopedShadowRecordCount;
    ScopedShadowGraph.Execute(ScopedSdfContext);
    bool ScopedShadowRecorded = false;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedShadowRecordCount; ++ScopedI)
    {
        if (ScopedShadowRecords[ScopedI].kind == RenderPassKind::Compute &&
            std::string_view(ScopedShadowRecords[ScopedI].pipelineName) == "sdf_shadow.comp")
        {
            ScopedShadowRecorded = true;
            FBY_CHECK(ScopedShadowRecords[ScopedI].groupCountX == (320u + 7u) / 8u);
            FBY_CHECK(ScopedShadowRecords[ScopedI].groupCountY == (240u + 7u) / 8u);
            FBY_CHECK(ScopedShadowRecords[ScopedI].constantsBytes == sizeof(SdfShadowConstants));
            const auto* ScopedConstants = static_cast<const SdfShadowConstants*>(ScopedShadowRecords[ScopedI].constants);
            FBY_CHECK(ScopedConstants->camera.Z == 1.0f);
            FBY_CHECK(ScopedConstants->occluders[0].Z == 3.0f);
        }
    }
    FBY_CHECK(ScopedShadowRecorded);
    ScopedSdfFrame.sdf.enabled = false;
    ScopedShadowRecordCount = 0;
    ScopedShadowGraph.Execute(ScopedSdfContext);
    bool ScopedAnyCompute = false;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedShadowRecordCount; ++ScopedI)
    {
        ScopedAnyCompute = ScopedAnyCompute || ScopedShadowRecords[ScopedI].kind == RenderPassKind::Compute;
    }
    FBY_CHECK(!ScopedAnyCompute);

    ScopedCloth.Shutdown(ScopedRegistry);
    if (GFailures == 0)
    {
        std::printf("render_weather_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "render_weather_test: %u failure(s)\n", GFailures);
    return 1;
}
