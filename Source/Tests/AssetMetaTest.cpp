// FemboyEngine2D - .femeta metadata + AssetDatabase transaction tests.
#include "Engine/Assets/AssetDatabase.hpp"

#include <array>
#include "Engine/Assets/AssetMeta.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void TestMetaFileRoundTrip()
{
    Fby::Assets::AssetMeta ScopedMeta;
    ScopedMeta.guid = 0x1234ABCDEFull;
    ScopedMeta.id = Fby::AssetId{0x99ull};
    ScopedMeta.kind = Fby::AssetKind::Texture;
    std::snprintf(ScopedMeta.sourcePath, sizeof(ScopedMeta.sourcePath), "textures/hero.png");
    std::snprintf(ScopedMeta.cookedPath, sizeof(ScopedMeta.cookedPath), "textures/hero.ftex");
    ScopedMeta.sourceHash = 0xAAu;
    ScopedMeta.cookedHash = 0xBBu;
    ScopedMeta.import.maxSize = 2048;
    ScopedMeta.import.srgb = false;
    ScopedMeta.valid = true;
    Fby::Assets::AssetDependency ScopedDependencies[2]{};
    ScopedDependencies[0].guid = 0x1u;
    ScopedDependencies[1].guid = 0x2u;

    std::string ScopedText;
    std::string ScopedError;
    Check(Fby::Assets::WriteAssetMetaFile(ScopedMeta,
                                          std::span<const Fby::Assets::AssetDependency>(ScopedDependencies, 2),
                                          &ScopedText, &ScopedError),
          "write femeta");
    Check(ScopedText.find("guid = \"0x0000001234ABCDEF\"") != std::string::npos, "guid is written as hex");
    Check(ScopedText.find("type = \"texture\"") != std::string::npos, "type is written");
    Check(ScopedText.find("srgb = false") != std::string::npos, "import settings are written");

    Fby::Assets::AssetMeta ScopedParsed;
    Fby::Assets::AssetDependency ScopedParsedDependencies[4]{};
    std::uint32_t ScopedDependencyCount = 0;
    Check(Fby::Assets::ParseAssetMetaFile(ScopedText, &ScopedParsed, ScopedParsedDependencies, 4,
                                          &ScopedDependencyCount, &ScopedError),
          "parse femeta");
    Check(ScopedParsed.guid == ScopedMeta.guid, "guid round-trips");
    Check(ScopedParsed.id == ScopedMeta.id, "asset id round-trips");
    Check(ScopedParsed.kind == Fby::AssetKind::Texture, "kind round-trips");
    Check(std::strcmp(ScopedParsed.sourcePath, "textures/hero.png") == 0, "source path round-trips");
    Check(ScopedDependencyCount == 2 && ScopedParsedDependencies[0].guid == 1 && ScopedParsedDependencies[1].guid == 2,
          "dependencies round-trip");
    Check(ScopedParsed.import.srgb == false && ScopedParsed.import.maxSize == 2048, "import settings round-trip");
}

void TestFontMetaRoundTrip()
{
    Fby::Assets::AssetMeta ScopedMeta;
    ScopedMeta.guid = 0x777u;
    ScopedMeta.kind = Fby::AssetKind::Font;
    std::snprintf(ScopedMeta.sourcePath, sizeof(ScopedMeta.sourcePath), "fonts/Inter-Regular.ttf");
    std::snprintf(ScopedMeta.cookedPath, sizeof(ScopedMeta.cookedPath), "fonts/Inter-Regular.fbyfont");
    ScopedMeta.import.fontStyle = 1;
    ScopedMeta.import.fontPixels = 24;
    ScopedMeta.import.fontOversample = 2;
    ScopedMeta.import.fontSdf = true;
    ScopedMeta.valid = true;

    std::string ScopedText;
    std::string ScopedError;
    Check(Fby::Assets::WriteAssetMetaFile(ScopedMeta, {}, &ScopedText, &ScopedError), "write font femeta");
    Check(ScopedText.find("type = \"font\"") != std::string::npos, "font type is written");
    Check(ScopedText.find("font_sdf = true") != std::string::npos, "font settings are written");

    Fby::Assets::AssetMeta ScopedParsed;
    Fby::Assets::AssetDependency ScopedDependencies[1]{};
    std::uint32_t ScopedCount = 0;
    Check(Fby::Assets::ParseAssetMetaFile(ScopedText, &ScopedParsed, ScopedDependencies, 1, &ScopedCount,
                                          &ScopedError),
          "parse font femeta");
    Check(ScopedParsed.kind == Fby::AssetKind::Font, "font kind round-trips");
    Check(ScopedParsed.import.fontStyle == 1 && ScopedParsed.import.fontPixels == 24 &&
              ScopedParsed.import.fontOversample == 2 && ScopedParsed.import.fontSdf,
          "font import settings round-trip");

    Fby::AssetKind ScopedFamilyKind = Fby::AssetKind::Unknown;
    Check(Fby::Assets::ParseAssetKind("font-family", &ScopedFamilyKind) &&
              ScopedFamilyKind == Fby::AssetKind::FontFamily,
          "font-family kind parses");
}

void TestDatabaseTransactions()
{
    Fby::Assets::AssetDatabase ScopedDatabase;
    Fby::Assets::AssetMeta ScopedMeta;
    ScopedMeta.guid = 0x5150u;
    ScopedMeta.kind = Fby::AssetKind::SpriteSheet;
    std::snprintf(ScopedMeta.sourcePath, sizeof(ScopedMeta.sourcePath), "sprites/sheet.png");
    ScopedMeta.cookedHash = 1;
    ScopedMeta.valid = true;
    const Fby::Assets::AssetHandle ScopedHandle = ScopedDatabase.RegisterMeta(ScopedMeta, {});
    Check(ScopedHandle.IsValid(), "register meta returns a handle");
    Check(ScopedDatabase.Meta(ScopedHandle) != nullptr, "handle resolves");
    Check(ScopedDatabase.SourcePath(ScopedHandle) == "sprites/sheet.png", "source path resolves");
    Check(ScopedDatabase.FindHandleByGuid(0x5150u) == ScopedHandle, "guid lookup");
    Check(ScopedDatabase.FindHandleByAssetId(ScopedDatabase.Id(ScopedHandle)) == ScopedHandle, "asset id lookup");
    Check(ScopedDatabase.FindHandleBySourcePath("sprites/sheet.png") == ScopedHandle, "path lookup");

    // Transaction: live row untouched until commit.
    Check(ScopedDatabase.BeginTransaction(), "begin transaction");
    Fby::Assets::AssetMeta ScopedStaged = *ScopedDatabase.Meta(ScopedHandle);
    ScopedStaged.cookedHash = 2;
    Check(ScopedDatabase.StageMeta(ScopedHandle, ScopedStaged, nullptr), "stage meta");
    Check(ScopedDatabase.Meta(ScopedHandle)->cookedHash == 1, "live row unchanged before commit");
    Check(ScopedDatabase.CommitTransaction(), "commit transaction");
    Check(ScopedDatabase.Meta(ScopedHandle)->cookedHash == 2, "content updated after commit");
    Check(ScopedDatabase.Meta(ScopedHandle)->contentGeneration == 1, "content generation bumped");
    Check(ScopedHandle == ScopedDatabase.FindHandleByGuid(0x5150u), "handle survives commit");

    Fby::Assets::AssetDatabase::ReloadNotice ScopedNotice{};
    Check(ScopedDatabase.PopReload(&ScopedNotice) && ScopedNotice.handle == ScopedHandle,
          "commit queues a reload notice");

    // Rollback keeps the old bytes.
    Check(ScopedDatabase.BeginTransaction(), "begin rollback transaction");
    ScopedStaged.cookedHash = 99;
    Check(ScopedDatabase.StageMeta(ScopedHandle, ScopedStaged, nullptr), "stage rollback");
    ScopedDatabase.RollbackTransaction();
    Check(ScopedDatabase.Meta(ScopedHandle)->cookedHash == 2, "rollback discards staged data");

    Check(ScopedDatabase.MarkSourceChanged(ScopedHandle, 7, nullptr), "mark source changed");
    Check(ScopedDatabase.Meta(ScopedHandle)->sourceHash == 7, "source hash updated");
    Check(ScopedDatabase.PopReload(&ScopedNotice), "source change queues reload");
    Check(ScopedDatabase.Meta(ScopedHandle)->contentGeneration == 2, "generation bumped for the change");
}

void TestInvalidInput()
{
    Fby::Assets::AssetMeta ScopedBad;
    ScopedBad.kind = Fby::AssetKind::Unknown;
    std::string ScopedError;
    Check(!Fby::Assets::ParseAssetMetaFile("not toml = [", &ScopedBad, nullptr, 0, nullptr, &ScopedError),
          "broken TOML rejected");
    Fby::Assets::AssetMeta ScopedMeta;
    Fby::Assets::AssetDependency ScopedDependencies[1]{};
    std::uint32_t ScopedCount = 0;
    Check(!Fby::Assets::ParseAssetMetaFile("type = \"texture\"", &ScopedMeta, ScopedDependencies, 1, &ScopedCount,
                                           &ScopedError),
          "metadata without a source is rejected");
}
void TestDependents()
{
    Fby::Assets::AssetDatabase ScopedDatabase;
    Fby::Assets::AssetMeta ScopedBase;
    ScopedBase.guid = 0x700u;
    ScopedBase.kind = Fby::AssetKind::Texture;
    std::snprintf(ScopedBase.sourcePath, sizeof(ScopedBase.sourcePath), "textures/base.png");
    ScopedBase.valid = true;
    const Fby::Assets::AssetHandle ScopedBaseHandle = ScopedDatabase.RegisterMeta(ScopedBase, {});
    Check(ScopedBaseHandle.IsValid(), "base asset registers");

    const std::array<Fby::Assets::AssetDependency, 1> ScopedDependencies{{
        {0x700u, static_cast<std::uint16_t>(Fby::AssetKind::Texture), 0},
    }};
    Fby::Assets::AssetMeta ScopedDependent;
    ScopedDependent.guid = 0x701u;
    ScopedDependent.kind = Fby::AssetKind::Material;
    std::snprintf(ScopedDependent.sourcePath, sizeof(ScopedDependent.sourcePath), "materials/derived.mat");
    ScopedDependent.valid = true;
    const Fby::Assets::AssetHandle ScopedDependentHandle =
        ScopedDatabase.RegisterMeta(ScopedDependent, ScopedDependencies);
    Check(ScopedDependentHandle.IsValid(), "dependent asset registers");

    std::array<Fby::Assets::AssetHandle, 4> ScopedOut{};
    Check(ScopedDatabase.CollectDependents(0x700u, ScopedOut) == 1, "one dependent is found");
    Check(ScopedOut[0] == ScopedDependentHandle, "the dependent handle is returned");
    Check(ScopedDatabase.CollectDependents(0x999u, ScopedOut) == 0, "an unused guid has no dependents");
    Check(ScopedDatabase.CollectDependents(Fby::Assets::kInvalidAssetGuid, ScopedOut) == 0,
          "the invalid guid has no dependents");
}
}

int main()
{
    TestMetaFileRoundTrip();
    TestFontMetaRoundTrip();
    TestDatabaseTransactions();
    TestInvalidInput();
    TestDependents();
    if (GFailures != 0)
    {
        std::printf("%d asset meta test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("asset_meta_test: all tests passed\n");
    return 0;
}
