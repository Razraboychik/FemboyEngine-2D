// FemboyEngine2D - ECS shape-cache fill system tests.
#include "Engine/Assets/Localization.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/AssetId.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Text/TextRenderSystem.hpp"
#include "Engine/Text/TextShapeSystem.hpp"

#include <array>
#include <cstdio>
#include <memory>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

class MarkerShaper final : public Fby::Text::ITextShaper
{
public:
    explicit MarkerShaper(std::uint32_t InMarker)
        : Marker(InMarker)
    {
    }

    [[nodiscard]] bool Shape(std::string_view, const Fby::Text::TextShapeParams&,
                             Fby::Text::TextShapeResult* OutResult) noexcept override
    {
        if (OutResult == nullptr)
        {
            return false;
        }
        OutResult->Count = 1;
        OutResult->Glyphs[0].GlyphId = Marker;
        return true;
    }

    std::uint32_t Marker = 0;
};

class NotdefShaper final : public Fby::Text::ITextShaper
{
public:
    [[nodiscard]] bool Shape(std::string_view, const Fby::Text::TextShapeParams&,
                             Fby::Text::TextShapeResult* OutResult) noexcept override
    {
        if (OutResult == nullptr)
        {
            return false;
        }
        OutResult->Count = 1;
        OutResult->Glyphs[0].GlyphId = 0;
        return true;
    }
};
}

int main()
{
    using namespace Fby;
    auto ScopedText = std::make_unique<Text::TextService>();
    auto ScopedCache = std::make_unique<Text::TextShapeCache>();
    Text::CodepointShaper ScopedShaper;
    entt::registry ScopedRegistry;
    const Entity ScopedEntity = ScopedRegistry.create();
    const AssetId ScopedAsset = AssetId::FromPath("text/system.txt", AssetKind::Text);
    Check(ScopedText->AddAsset(ScopedAsset, "AA"), "shape system asset stores");
    TextComponent ScopedComponent;
    ScopedComponent.SetRef(ScopedText->MakeRef(ScopedAsset));
    ScopedRegistry.emplace<TextComponent>(ScopedEntity, ScopedComponent);
    Text::TextLayoutParams ScopedLayoutParams;
    ScopedLayoutParams.MaxWidth = 64.0f;
    ScopedLayoutParams.FontSize = 16.0f;

    const Text::TextShapeSystemStats ScopedFirst =
        Text::UpdateTextShapes(ScopedRegistry, *ScopedText, ScopedShaper, *ScopedCache, 0, 16.0f,
                               ScopedLayoutParams);
    Check(ScopedFirst.Shaped == 1 && ScopedFirst.Reused == 0, "shape system fills a missing handle");
    Check(ScopedRegistry.all_of<TextShapeComponent>(ScopedEntity), "shape system writes the ECS component");
    const TextShapeComponent ScopedHandle = ScopedRegistry.get<TextShapeComponent>(ScopedEntity);
    Check(ScopedCache->View(Text::TextShapeHandle{ScopedHandle.ShapeIndex, ScopedHandle.ShapeGeneration})
              .Glyphs.size() == 2,
          "shape system cache entry has glyphs");

    const Text::TextShapeSystemStats ScopedSecond =
        Text::UpdateTextShapes(ScopedRegistry, *ScopedText, ScopedShaper, *ScopedCache, 0, 16.0f,
                               ScopedLayoutParams);
    Check(ScopedSecond.Shaped == 0 && ScopedSecond.Reused == 1, "shape system reuses a valid handle");

    const AssetId ScopedChangedAsset = AssetId::FromPath("text/system_changed.txt", AssetKind::Text);
    Check(ScopedText->AddAsset(ScopedChangedAsset, "AB"), "changed shape asset stores");
    TextComponent& ScopedMutable = ScopedRegistry.get<TextComponent>(ScopedEntity);
    ScopedMutable.SetRef(ScopedText->MakeRef(ScopedChangedAsset));
    const Text::TextShapeSystemStats ScopedChanged =
        Text::UpdateTextShapes(ScopedRegistry, *ScopedText, ScopedShaper, *ScopedCache, 0, 16.0f,
                               ScopedLayoutParams);
    Check(ScopedChanged.Shaped == 1 && ScopedChanged.Reused == 0,
          "shape system invalidates a stale key hash");

    auto ScopedFallbackCache = std::make_unique<Text::TextShapeCache>();
    entt::registry ScopedFallbackRegistry;
    const Entity ScopedFallbackEntity = ScopedFallbackRegistry.create();
    ScopedFallbackRegistry.emplace<TextComponent>(ScopedFallbackEntity, ScopedComponent);
    NotdefShaper ScopedNotdef;
    std::array<Text::ITextShaper*, 2> ScopedBackends{&ScopedNotdef, &ScopedShaper};
    const Text::TextShapeSystemStats ScopedFallbackStats = Text::UpdateTextShapes(
        ScopedFallbackRegistry, *ScopedText, std::span<Text::ITextShaper*>(ScopedBackends), *ScopedFallbackCache,
        0, 16.0f, ScopedLayoutParams);
    Check(ScopedFallbackStats.Shaped == 1 && ScopedFallbackStats.Overflow == 0,
          "shape system runs a fallback chain");

    auto ScopedScriptCache = std::make_unique<Text::TextShapeCache>();
    entt::registry ScopedScriptRegistry;
    const Entity ScopedScriptEntity = ScopedScriptRegistry.create();
    const AssetId ScopedScriptAsset = AssetId::FromPath("text/system_script.txt", AssetKind::Text);
    Check(ScopedText->AddAsset(ScopedScriptAsset, "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7"),
          "script-routed asset stores");
    TextComponent ScopedScriptText;
    ScopedScriptText.SetRef(ScopedText->MakeRef(ScopedScriptAsset));
    ScopedScriptRegistry.emplace<TextComponent>(ScopedScriptEntity, ScopedScriptText);
    MarkerShaper ScopedArabicMarker(77);
    MarkerShaper ScopedLatinMarker(88);
    const std::array<Text::ScriptBackend, 2> ScopedScriptBackends{{
        {Text::TextScript::Arabic, &ScopedArabicMarker},
        {Text::TextScript::Latin, &ScopedLatinMarker},
    }};
    const Text::TextShapeSystemStats ScopedScriptStats = Text::UpdateTextShapes(
        ScopedScriptRegistry, *ScopedText, std::span<const Text::ScriptBackend>(ScopedScriptBackends),
        *ScopedScriptCache, 0, 16.0f, ScopedLayoutParams);
    Check(ScopedScriptStats.Shaped == 1, "shape system routes a script backend");
    const TextShapeComponent ScopedScriptHandle = ScopedScriptRegistry.get<TextShapeComponent>(ScopedScriptEntity);
    Check(ScopedScriptCache
              ->View(Text::TextShapeHandle{ScopedScriptHandle.ShapeIndex, ScopedScriptHandle.ShapeGeneration})
              .Glyphs[0]
              .GlyphId == 77,
          "shape system used the Arabic backend");

    // Scene case: one registry with Latin, Arabic and CJK text, shaped through
    // the mounted script stack, extracted from the shaped cache, then reshaped
    // after a locale change and after a direction change.
    VirtualFileSystem ScopedVfs;
    std::string ScopedVfsError;
    if (ScopedVfs.MountDirectory("assets", 0, &ScopedVfsError))
    {
        auto ScopedStack = std::make_unique<Text::TextFontStack>();
        const std::array<Text::FontCandidate, 3> ScopedCandidates = {
            Text::FontCandidate{"fonts/Inter-Regular.ttf", 0, Text::TextScript::Latin},
            Text::FontCandidate{"fonts/tests/NotoSansArabic-Regular.ttf", 1, Text::TextScript::Arabic},
            Text::FontCandidate{"fonts/tests/NotoSansSC-Regular.ttf", 4, Text::TextScript::Han},
        };
        Check(ScopedStack->AddScriptFamily(ScopedVfs, std::span<const Text::FontCandidate>(ScopedCandidates),
                                           16.0f, 1024, &ScopedVfsError) == 3,
              "scene stack mounts the three script faces");
        ScopedStack->SetDefaultScript(Text::TextScript::Latin);

        auto ScopedSceneText = std::make_unique<Text::TextService>();
        auto ScopedSceneCache = std::make_unique<Text::TextShapeCache>();
        entt::registry ScopedScene;
        const std::array<std::string_view, 3> ScopedBlocks = {
            "Hello scene",
            "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",
            "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C",
        };
        std::array<Entity, 3> ScopedEntities{};
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedBlocks.size(); ++ScopedIndex)
        {
            char ScopedPath[64] = {};
            std::snprintf(ScopedPath, sizeof(ScopedPath), "text/scene_%u.txt", static_cast<unsigned>(ScopedIndex));
            const AssetId ScopedSceneAsset = AssetId::FromPath(ScopedPath, AssetKind::Text);
            Check(ScopedSceneText->AddAsset(ScopedSceneAsset, ScopedBlocks[ScopedIndex]),
                  "scene block stores");
            const Entity ScopedSceneEntity = ScopedScene.create();
            TextComponent ScopedSceneComponent;
            ScopedSceneComponent.SetRef(ScopedSceneText->MakeRef(ScopedSceneAsset));
            if (ScopedIndex == 1)
            {
                ScopedSceneComponent.Flags |= kTextFlagRightToLeft;
            }
            if (ScopedIndex == 2)
            {
                ScopedSceneComponent.Flags |= kTextFlagVertical;
            }
            ScopedScene.emplace<TextComponent>(ScopedSceneEntity, ScopedSceneComponent);
            ScopedScene.emplace<Transform2D>(ScopedSceneEntity);
            ScopedEntities[ScopedIndex] = ScopedSceneEntity;
        }
        Text::TextLayoutParams ScopedSceneLayout;
        ScopedSceneLayout.MaxWidth = 240.0f;
        ScopedSceneLayout.FontSize = 16.0f;
        const Text::TextShapeSystemStats ScopedSceneFirst =
            Text::UpdateTextShapes(ScopedScene, *ScopedSceneText, *ScopedStack, *ScopedSceneCache, 0, 16.0f,
                                   ScopedSceneLayout);
        Check(ScopedSceneFirst.Shaped == 3, "the stack overload shapes every scene entity");
        const Text::TextShapeSystemStats ScopedSceneSecond =
            Text::UpdateTextShapes(ScopedScene, *ScopedSceneText, *ScopedStack, *ScopedSceneCache, 0, 16.0f,
                                   ScopedSceneLayout);
        Check(ScopedSceneSecond.Shaped == 0 && ScopedSceneSecond.Reused == 3,
              "the second scene frame reuses every shaped handle");
        std::array<Rhi::RenderItem, 256> ScopedItems{};
        const Text::TextRenderSystemStats ScopedSceneRender = Text::ExtractTextItems(
            ScopedScene, *ScopedSceneText, ScopedStack->Fonts(), 0, 16.0f, ScopedSceneLayout,
            std::span<Rhi::RenderItem>(ScopedItems), nullptr, nullptr, ScopedSceneCache.get());
        Check(ScopedSceneRender.Entities == 3 && ScopedSceneRender.Items != 0,
              "scene text extracts from the shaped cache");
        Check(ScopedSceneRender.MissingGlyphs == 0, "the scene renders Arabic and CJK without missing glyphs");

        // Direction change: flipping the Latin entity to right-to-left changes
        // its shape key, so the next pass reshapes exactly one entity.
        ScopedScene.get<TextComponent>(ScopedEntities[0]).Flags |= kTextFlagRightToLeft;
        const Text::TextShapeSystemStats ScopedDirectionChange =
            Text::UpdateTextShapes(ScopedScene, *ScopedSceneText, *ScopedStack, *ScopedSceneCache, 0, 16.0f,
                                   ScopedSceneLayout);
        Check(ScopedDirectionChange.Shaped == 1 && ScopedDirectionChange.Reused == 2,
              "a direction change reshapes only the affected entity");
    }

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d shape system test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("shape system tests: ok\n");
    return 0;
}
