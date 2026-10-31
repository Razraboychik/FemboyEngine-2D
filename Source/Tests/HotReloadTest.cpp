// FemboyEngine2D - font/texture hot reload while jobs run.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/UI/FontAtlas.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

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

std::vector<std::byte> MakeFontAtlas(float InAdvance)
{
    using namespace Fby::Ui;
    constexpr std::uint32_t kWidth = 8;
    constexpr std::uint32_t kHeight = 8;
    constexpr std::uint32_t kGlyphs = 2;
    const std::size_t ScopedPixelBytes = static_cast<std::size_t>(kWidth) * kHeight * 4u;
    std::vector<std::byte> ScopedBytes(
        sizeof(FontAtlasHeader) + kGlyphs * sizeof(GlyphRecord) + ScopedPixelBytes, std::byte{0});
    FontAtlasHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic));
    ScopedHeader.version = kFontAtlasVersion;
    ScopedHeader.headerBytes = sizeof(FontAtlasHeader);
    ScopedHeader.glyphCount = kGlyphs;
    ScopedHeader.atlasWidth = kWidth;
    ScopedHeader.atlasHeight = kHeight;
    ScopedHeader.pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::Rgba8);
    ScopedHeader.glyphTableOffset = sizeof(FontAtlasHeader);
    ScopedHeader.pixelOffset = static_cast<std::uint32_t>(sizeof(FontAtlasHeader) + kGlyphs * sizeof(GlyphRecord));
    ScopedHeader.lineHeight = 8.0f;
    ScopedHeader.ascent = 6.0f;
    ScopedHeader.descent = 2.0f;
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));
    GlyphRecord ScopedRecords[kGlyphs]{};
    ScopedRecords[0].codepoint = ' ';
    ScopedRecords[0].width = 4.0f;
    ScopedRecords[0].height = 8.0f;
    ScopedRecords[1] = ScopedRecords[0];
    ScopedRecords[1].codepoint = 'A';
    ScopedRecords[1].advance = InAdvance;
    std::memcpy(ScopedBytes.data() + sizeof(FontAtlasHeader), ScopedRecords, sizeof(ScopedRecords));
    return ScopedBytes;
}

bool WriteBytes(const std::filesystem::path& InPath, const std::vector<std::byte>& InBytes)
{
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
    return static_cast<bool>(ScopedStream);
}

std::atomic<std::uint32_t> GJobHits{0};
}

int main()
{
    using namespace Fby;

    const std::filesystem::path ScopedRoot = "_fby_hot_reload";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot, ScopedError);

    Check(WriteBytes(ScopedRoot / "font.fbyfont", MakeFontAtlas(4.0f)), "initial font writes");
    const std::vector<std::byte> ScopedTexture = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    Check(WriteBytes(ScopedRoot / "tex.bin", ScopedTexture), "initial texture writes");

    VirtualFileSystem ScopedVfs;
    std::string ScopedVfsError;
    Check(ScopedVfs.MountDirectory(ScopedRoot.string(), 0, &ScopedVfsError), "the reload directory mounts");
    Ui::FontAtlasSet ScopedFonts;
    Check(ScopedFonts.LoadFromVfs(ScopedVfs, "font.fbyfont"), "the font loads from the VFS");
    const Ui::FontAtlas* ScopedAtlas = ScopedFonts.FindByFontId(0);
    Check(ScopedAtlas != nullptr && ScopedAtlas->Glyph('A') != nullptr, "the font exposes 'A'");
    Check(ScopedAtlas != nullptr && ScopedAtlas->Glyph('A')->advance == 4.0f, "the initial advance is loaded");

    // Jobs run while the main thread swaps the files underneath them.
    JobSystem ScopedJobs(4);
    GJobHits = 0;
    ScopedJobs.ParallelFor(4096, 64,
                           [](std::uint32_t InBegin, std::uint32_t InEnd)
                           {
                               for (std::uint32_t ScopedI = InBegin; ScopedI < InEnd; ++ScopedI)
                               {
                                   (void)ScopedI;
                                   GJobHits.fetch_add(1, std::memory_order_relaxed);
                               }
                           });

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 10; ++ScopedIteration)
    {
        const float ScopedAdvance = (ScopedIteration % 2u) == 0u ? 5.0f : 4.0f;
        // Drop the cached mappings first: on Windows the read mapping keeps the
        // file open, so the rewrite has to happen after the invalidation.
        (void)ScopedVfs.InvalidateFile("font.fbyfont");
        (void)ScopedVfs.InvalidateFile("tex.bin");
        ScopedVfs.RefreshDirectoryMounts();
        Check(WriteBytes(ScopedRoot / "font.fbyfont", MakeFontAtlas(ScopedAdvance)), "the font rewrites");
        const std::vector<std::byte> ScopedNewTexture{std::byte{static_cast<unsigned char>(ScopedIteration + 10)},
                                                     std::byte{0}, std::byte{0}, std::byte{0}};
        Check(WriteBytes(ScopedRoot / "tex.bin", ScopedNewTexture), "the texture rewrites");
        ScopedVfs.RefreshDirectoryMounts();
        ScopedFonts.Clear();
        Check(ScopedFonts.LoadFromVfs(ScopedVfs, "font.fbyfont"), "the font hot-reloads");
        const Ui::FontAtlas* ScopedReloaded = ScopedFonts.FindByFontId(0);
        Check(ScopedReloaded != nullptr && ScopedReloaded->Glyph('A') != nullptr &&
                  ScopedReloaded->Glyph('A')->advance == ScopedAdvance,
              "the reloaded font carries the new advance");
        const auto ScopedRead = ScopedVfs.Read("tex.bin");
        Check(ScopedRead.has_value() && ScopedRead->size() == ScopedNewTexture.size() &&
                  (*ScopedRead)[0] == ScopedNewTexture[0],
              "the texture reload returns the new bytes");
    }

    ScopedJobs.WaitIdle();
    Check(GJobHits.load() == 4096, "the background jobs all completed during the reloads");

    std::filesystem::remove_all(ScopedRoot, ScopedError);

    if (GFailures == 0)
    {
        std::printf("hot_reload: OK\n");
        return 0;
    }
    std::fprintf(stderr, "hot_reload: %d failure(s)\n", GFailures);
    return 1;
}
