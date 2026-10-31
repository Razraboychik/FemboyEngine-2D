// FemboyEngine2D - Pink UI native font-array integration test.
#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
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

[[nodiscard]] std::vector<std::byte> MakeFontAtlas()
{
    using namespace Fby::Ui;
    constexpr std::uint32_t ScopedWidth = 8;
    constexpr std::uint32_t ScopedHeight = 8;
    constexpr std::uint32_t ScopedGlyphs = 2;
    const std::size_t ScopedPixelBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u;
    std::vector<std::byte> ScopedBytes(sizeof(FontAtlasHeader) + ScopedGlyphs * sizeof(GlyphRecord) +
                                           ScopedPixelBytes,
                                       std::byte{0});

    FontAtlasHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic));
    ScopedHeader.version = kFontAtlasVersion;
    ScopedHeader.headerBytes = sizeof(FontAtlasHeader);
    ScopedHeader.glyphCount = ScopedGlyphs;
    ScopedHeader.atlasWidth = ScopedWidth;
    ScopedHeader.atlasHeight = ScopedHeight;
    ScopedHeader.pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::Rgba8);
    ScopedHeader.glyphTableOffset = sizeof(FontAtlasHeader);
    ScopedHeader.pixelOffset = static_cast<std::uint32_t>(sizeof(FontAtlasHeader) + ScopedGlyphs * sizeof(GlyphRecord));
    ScopedHeader.lineHeight = 8.0f;
    ScopedHeader.ascent = 6.0f;
    ScopedHeader.descent = 2.0f;
    ScopedHeader.fontId = 0;
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));

    GlyphRecord ScopedGlyphsRecords[ScopedGlyphs]{};
    ScopedGlyphsRecords[0].codepoint = ' ';
    ScopedGlyphsRecords[0].u1 = 0.5f;
    ScopedGlyphsRecords[0].v1 = 1.0f;
    ScopedGlyphsRecords[0].advance = 4.0f;
    ScopedGlyphsRecords[0].width = 4.0f;
    ScopedGlyphsRecords[0].height = 8.0f;
    ScopedGlyphsRecords[1] = ScopedGlyphsRecords[0];
    ScopedGlyphsRecords[1].codepoint = 'A';
    ScopedGlyphsRecords[1].u0 = 0.5f;
    ScopedGlyphsRecords[1].u1 = 1.0f;
    ScopedGlyphsRecords[1].bearingY = 6.0f;
    std::memcpy(ScopedBytes.data() + sizeof(FontAtlasHeader), ScopedGlyphsRecords, sizeof(ScopedGlyphsRecords));

    std::byte* ScopedPixels = ScopedBytes.data() + ScopedHeader.pixelOffset;
    for (std::size_t ScopedTexel = 0; ScopedTexel < static_cast<std::size_t>(ScopedWidth) * ScopedHeight; ++ScopedTexel)
    {
        ScopedPixels[ScopedTexel * 4 + 0] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4 + 1] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4 + 2] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4 + 3] = std::byte{0xFF};
    }
    return ScopedBytes;
}
}

int main()
{
    using namespace Fby;

    Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Rhi::Backend::Null;
    const std::unique_ptr<Rhi::RHI> ScopedGpu = Rhi::RHI::Create(ScopedDesc);
    FBY_CHECK(ScopedGpu != nullptr);

    Ui::FontAtlasSet ScopedFonts;
    const std::vector<std::byte> ScopedAtlasBytes = MakeFontAtlas();
    FBY_CHECK(ScopedFonts.Add(ScopedAtlasBytes));
    FBY_CHECK(ScopedFonts.LayerForFontId(0) == 1);
    FBY_CHECK(!ScopedFonts.Mounted());
    FBY_CHECK(!ScopedFonts.IsCurrent());

    // Cold path: mount before the first frame. BuildFrame must never call
    Ui::WidgetCanvasSystem ScopedCanvasSystem{&ScopedFonts};
    FBY_CHECK(ScopedCanvasSystem.MountFonts(*ScopedGpu));
    FBY_CHECK(ScopedFonts.Mounted());
    FBY_CHECK(ScopedFonts.IsCurrent());

    Ui::WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{200.0f, 100.0f});
    Ui::WidgetDesc ScopedPanel;
    ScopedPanel.kind = Ui::WidgetKind::Panel;
    ScopedPanel.offsetMax = Vector2D{-8.0f, -8.0f};
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedPanel) >= 0);
    Ui::WidgetDesc ScopedLabel;
    ScopedLabel.kind = Ui::WidgetKind::Label;
    ScopedLabel.anchorMin = Vector2D{0.1f, 0.1f};
    ScopedLabel.anchorMax = Vector2D{0.6f, 0.3f};
    ScopedLabel.text = "A";
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedLabel) >= 0);

    std::array<Rhi::RenderItem, 64> ScopedItems{};
    std::array<Rhi::RenderKey, 64> ScopedKeys{};
    std::array<Rhi::DrawBatch, 8> ScopedBatches{};
    Ui::FrameOutput ScopedOutput;
    ScopedOutput.items = ScopedItems;
    ScopedOutput.keys = ScopedKeys;
    ScopedOutput.batches = ScopedBatches;

    Ecs::Registry ScopedRegistry;
    JobSystem ScopedJobs(1);
    Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.renderer = ScopedGpu.get();
    ScopedContext.fonts = &ScopedFonts;

    FBY_CHECK(Ui::WidgetCanvasSystem::BuildFrame(ScopedContext, ScopedCanvas, ScopedOutput) == Ui::BuildStatus::Ok);
    FBY_CHECK(ScopedFonts.Mounted());
    FBY_CHECK(ScopedOutput.fontTextureArray == ScopedFonts.TextureArray().id);
    FBY_CHECK(ScopedOutput.batchesWritten == 1);
    FBY_CHECK(ScopedBatches[0].textureArray == ScopedFonts.TextureArray().id);
    FBY_CHECK(ScopedOutput.itemsWritten >= 2);
    FBY_CHECK(ScopedItems[0].layer == Ui::kWhiteLayer);
    FBY_CHECK(ScopedItems[1].layer == 1);

    // Adding a font invalidates the GPU array: IsCurrent() goes false until the
    FBY_CHECK(ScopedFonts.Add(ScopedAtlasBytes));
    FBY_CHECK(ScopedFonts.Mounted());
    FBY_CHECK(!ScopedFonts.IsCurrent());
    FBY_CHECK(ScopedCanvasSystem.MountFonts(*ScopedGpu));
    FBY_CHECK(ScopedFonts.IsCurrent());

    // An image-only canvas may target its own 32-bit texture array. The batch
    Ui::WidgetCanvas ScopedImageCanvas;
    ScopedImageCanvas.SetCanvasSize(Vector2D{64.0f, 64.0f});
    ScopedImageCanvas.SetTextureArray(3);
    Ui::WidgetDesc ScopedImage;
    ScopedImage.kind = Ui::WidgetKind::Image;
    ScopedImage.layer = 2;
    FBY_CHECK(ScopedImageCanvas.AddWidget(ScopedImage) >= 0);
    Ui::FrameOutput ScopedImageOutput;
    ScopedImageOutput.items = ScopedItems;
    ScopedImageOutput.keys = ScopedKeys;
    ScopedImageOutput.batches = ScopedBatches;
    FBY_CHECK(Ui::WidgetCanvasSystem::BuildFrame(ScopedContext, ScopedImageCanvas, ScopedImageOutput) ==
              Ui::BuildStatus::Ok);
    FBY_CHECK(ScopedImageOutput.fontTextureArray == 0xFFFFFFFFu);
    FBY_CHECK(ScopedBatches[0].textureArray == 3);

    // Strict contract: a text canvas backed by an unmounted set is refused, and
    Ui::FontAtlasSet ScopedUnmounted;
    FBY_CHECK(ScopedUnmounted.Add(ScopedAtlasBytes));
    ScopedContext.fonts = &ScopedUnmounted;
    Ui::WidgetCanvas ScopedSimpleCanvas;
    ScopedSimpleCanvas.SetCanvasSize(Vector2D{64.0f, 64.0f});
    Ui::WidgetDesc ScopedSimpleLabel;
    ScopedSimpleLabel.kind = Ui::WidgetKind::Label;
    ScopedSimpleLabel.text = "A";
    FBY_CHECK(ScopedSimpleCanvas.AddWidget(ScopedSimpleLabel) >= 0);
    Ui::FrameOutput ScopedUnmountedOutput;
    ScopedUnmountedOutput.items = ScopedItems;
    ScopedUnmountedOutput.keys = ScopedKeys;
    ScopedUnmountedOutput.batches = ScopedBatches;
#if defined(FBY_ENABLE_ASSERTS) && FBY_ENABLE_ASSERTS
    std::printf("pinkui_font_mount_test: assert build, unmounted-frame probe skipped by design\n");
#else
    FBY_CHECK(Ui::WidgetCanvasSystem::BuildFrame(ScopedContext, ScopedSimpleCanvas, ScopedUnmountedOutput) ==
              Ui::BuildStatus::FontsNotMounted);
    FBY_CHECK(!ScopedUnmounted.Mounted());
    FBY_CHECK(ScopedUnmountedOutput.fontContractViolated);
    FBY_CHECK(ScopedUnmountedOutput.itemsWritten == 0);
    FBY_CHECK(ScopedUnmountedOutput.batchesWritten == 0);
    FBY_CHECK(ScopedUnmountedOutput.fontTextureArray == 0xFFFFFFFFu);
#endif

    // Hostile .fbyfont header: an absurd atlas dimension must be rejected at
    std::vector<std::byte> ScopedHostile = ScopedAtlasBytes;
    Ui::FontAtlasHeader ScopedHostileHeader{};
    std::memcpy(&ScopedHostileHeader, ScopedHostile.data(), sizeof(ScopedHostileHeader));
    ScopedHostileHeader.atlasWidth = 0xFFFFFFFFu;
    std::memcpy(ScopedHostile.data(), &ScopedHostileHeader, sizeof(ScopedHostileHeader));
    Ui::FontAtlasSet ScopedHostileSet;
    FBY_CHECK(!ScopedHostileSet.Add(ScopedHostile));
    FBY_CHECK(ScopedHostileSet.Count() == 0);

    if (GFailures == 0)
    {
        std::printf("pinkui_font_mount_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_font_mount_test: %u failure(s)\n", GFailures);
    return 1;
}
