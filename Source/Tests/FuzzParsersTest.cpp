// FemboyEngine2D - deterministic mutation tests for the shipped parsers.
#include "Engine/Assets/AsepriteImporter.hpp"
#include "Engine/Assets/FemArchiver.hpp"
#include "Engine/Assets/FempakWriter.hpp"
#include "Engine/Assets/TiledImporter.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Network/NetProtocol.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

std::uint64_t GState = 0x9E3779B97F4A7C15ull;

std::uint32_t FuzzCount(std::uint32_t InBase)
{
    const char* ScopedEnv = std::getenv("FBY_FUZZ_ITERATIONS");
    if (ScopedEnv == nullptr)
    {
        return InBase;
    }
    const unsigned long long ScopedRequested = std::strtoull(ScopedEnv, nullptr, 10);
    return ScopedRequested == 0ull ? InBase : static_cast<std::uint32_t>(ScopedRequested);
}

std::uint32_t NextRandom()
{
    GState ^= GState << 13;
    GState ^= GState >> 7;
    GState ^= GState << 17;
    return static_cast<std::uint32_t>(GState >> 32);
}

void FillRandom(std::vector<std::byte>& InBuffer, std::size_t InLength)
{
    InBuffer.resize(InLength);
    for (std::byte& ScopedByte : InBuffer)
    {
        ScopedByte = static_cast<std::byte>(NextRandom() & 0xFFu);
    }
}

void TestInputPacketMutations()
{
    Fby::Net::InputPacket ScopedPacket;
    ScopedPacket.Player = Fby::Net::NetPlayerId{1};
    ScopedPacket.PacketSequence = 7;
    ScopedPacket.FirstSequence = 3;
    ScopedPacket.Count = 2;
    for (std::size_t ScopedI = 0; ScopedI < ScopedPacket.Count; ++ScopedI)
    {
        ScopedPacket.Frames[ScopedI].tick = static_cast<std::uint64_t>(ScopedI) + 3;
        ScopedPacket.Frames[ScopedI].moveX = 100;
        ScopedPacket.Frames[ScopedI].moveY = -50;
    }

    std::vector<std::byte> ScopedEncoded(Fby::Net::kInputPacketBytes + 32);
    const std::size_t ScopedBytes = Fby::Net::EncodeInputPacket(ScopedPacket, ScopedEncoded);
    Check(ScopedBytes > 0, "valid input packet encodes");

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < FuzzCount(4000); ++ScopedIteration)
    {
        std::vector<std::byte> ScopedMutated = ScopedEncoded;
        ScopedMutated.resize(ScopedBytes);
        const std::uint32_t ScopedFlips = 1u + (NextRandom() % 4u);
        for (std::uint32_t ScopedFlip = 0; ScopedFlip < ScopedFlips; ++ScopedFlip)
        {
            if (ScopedMutated.empty())
            {
                break;
            }
            const std::size_t ScopedIndex = NextRandom() % ScopedMutated.size();
            ScopedMutated[ScopedIndex] = static_cast<std::byte>(NextRandom() & 0xFFu);
        }
        Fby::Net::InputPacket ScopedDecoded;
        (void)Fby::Net::DecodeInputPacket(ScopedMutated, &ScopedDecoded);
    }

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < FuzzCount(1000); ++ScopedIteration)
    {
        std::vector<std::byte> ScopedRandom;
        FillRandom(ScopedRandom, NextRandom() % 256u);
        Fby::Net::InputPacket ScopedDecoded;
        (void)Fby::Net::DecodeInputPacket(ScopedRandom, &ScopedDecoded);
    }
}

void TestArchiveMutations()
{
    const std::array<Fby::Assets::ArchiveInput, 1> ScopedEntries{{
        {.path = "a.txt", .bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>("hello"), 5)},
    }};
    Fby::Assets::ArchiveBuildOptions ScopedOptions;
    ScopedOptions.codec = Fby::Assets::ArchiveCodec::Store;
    std::vector<std::byte> ScopedArchive(Fby::Assets::FemArchiver::MaxArchiveBytes(ScopedEntries, ScopedOptions.codec) +
                                         256u);
    std::size_t ScopedArchiveBytes = 0;
    Check(Fby::Assets::FemArchiver::Build(ScopedEntries, ScopedArchive, ScopedOptions, &ScopedArchiveBytes) ==
              Fby::Assets::ArchiveStatus::Ok,
          "valid archive builds");
    Check(ScopedArchiveBytes > 0, "archive has bytes");
    ScopedArchive.resize(ScopedArchiveBytes);

    std::vector<std::byte> ScopedDecryptBuffer(8192);
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < FuzzCount(3000); ++ScopedIteration)
    {
        std::vector<std::byte> ScopedMutated = ScopedArchive;
        const std::uint32_t ScopedFlips = 1u + (NextRandom() % 3u);
        for (std::uint32_t ScopedFlip = 0; ScopedFlip < ScopedFlips; ++ScopedFlip)
        {
            const std::size_t ScopedIndex = NextRandom() % ScopedMutated.size();
            ScopedMutated[ScopedIndex] = static_cast<std::byte>(NextRandom() & 0xFFu);
        }
        Fby::Assets::ArchiveReader ScopedReader;
        (void)Fby::Assets::FemArchiver::Open(ScopedMutated, nullptr, ScopedDecryptBuffer, ScopedReader);
    }

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < FuzzCount(500); ++ScopedIteration)
    {
        std::vector<std::byte> ScopedRandom;
        FillRandom(ScopedRandom, NextRandom() % 512u);
        Fby::Assets::ArchiveReader ScopedReader;
        (void)Fby::Assets::FemArchiver::Open(ScopedRandom, nullptr, ScopedDecryptBuffer, ScopedReader);
    }
}

void TestFempakMutations()
{
    Fby::Assets::FempakWriter ScopedWriter;
    const std::array<std::byte, 9> ScopedPayload{std::byte{'h'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'},
                                                 std::byte{'o'}, std::byte{' '}, std::byte{'p'}, std::byte{'a'},
                                                 std::byte{'k'}};
    ScopedWriter.Add("a.txt", std::span<const std::byte>(ScopedPayload.data(), ScopedPayload.size()));
    const std::vector<std::byte> ScopedValid = ScopedWriter.Build();
    Check(!ScopedValid.empty(), "valid pak builds");

    const std::filesystem::path ScopedTemp =
        std::filesystem::temp_directory_path() / "fby_fuzz_input.fempak";
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < FuzzCount(400); ++ScopedIteration)
    {
        std::vector<std::byte> ScopedMutated = ScopedValid;
        if ((ScopedIteration % 4u) == 3u)
        {
            FillRandom(ScopedMutated, NextRandom() % 256u);
        }
        else
        {
            const std::uint32_t ScopedFlips = 1u + (NextRandom() % 3u);
            for (std::uint32_t ScopedFlip = 0; ScopedFlip < ScopedFlips; ++ScopedFlip)
            {
                const std::size_t ScopedIndex = NextRandom() % ScopedMutated.size();
                ScopedMutated[ScopedIndex] = static_cast<std::byte>(NextRandom() & 0xFFu);
            }
        }
        {
            std::ofstream ScopedStream(ScopedTemp, std::ios::binary | std::ios::trunc);
            ScopedStream.write(reinterpret_cast<const char*>(ScopedMutated.data()),
                               static_cast<std::streamsize>(ScopedMutated.size()));
        }
        {
            Fby::VirtualFileSystem ScopedVfs;
            std::string ScopedError;
            if (ScopedVfs.MountPak(ScopedTemp.string(), 0, &ScopedError))
            {
                (void)ScopedVfs.Read("a.txt");
            }
        }
    }
    std::error_code ScopedCode;
    std::filesystem::remove(ScopedTemp, ScopedCode);
}

void TestAsepriteMutations()
{
    constexpr const char* kSheet = R"({
  "frames": {
    "idle 0": {"frame": {"x": 0, "y": 0, "w": 16, "h": 16}, "duration": 120},
    "idle 1": {"frame": {"x": 16, "y": 0, "w": 16, "h": 16}, "duration": 80}
  },
  "meta": {
    "image": "hero.png",
    "size": {"w": 64, "h": 32},
    "frameTags": [{"name": "idle", "from": 0, "to": 1, "direction": "forward"}]
  }
})";
    std::string ScopedBase = kSheet;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < FuzzCount(4000); ++ScopedIteration)
    {
        std::string ScopedMutated = ScopedBase;
        const std::uint32_t ScopedMode = NextRandom() % 4u;
        if (ScopedMode == 0u && !ScopedMutated.empty())
        {
            ScopedMutated[NextRandom() % ScopedMutated.size()] = static_cast<char>(NextRandom() & 0x7Fu);
        }
        else if (ScopedMode == 1u)
        {
            ScopedMutated.resize(NextRandom() % (ScopedMutated.size() + 1u));
        }
        else if (ScopedMode == 2u && !ScopedMutated.empty())
        {
            ScopedMutated.erase(NextRandom() % ScopedMutated.size(), 1u + (NextRandom() % 8u));
        }
        else
        {
            ScopedMutated.insert(NextRandom() % (ScopedMutated.size() + 1u), 1u + (NextRandom() % 8u), 'x');
        }
        Fby::Assets::AsepriteSheet ScopedSheet;
        std::string ScopedError;
        (void)Fby::Assets::ParseAsepriteJson(ScopedMutated, &ScopedSheet, &ScopedError);
    }
}

void TestTiledMutations()
{
    constexpr const char* kMap = R"(<?xml version="1.0"?>
<map version="1.10" orientation="orthogonal" width="2" height="2" tilewidth="16" tileheight="16">
 <tileset firstgid="1" source="tiles.tsx"/>
 <layer name="ground"><data encoding="csv">1,2,0,3</data></layer>
</map>)";
    std::string ScopedBase = kMap;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < FuzzCount(4000); ++ScopedIteration)
    {
        std::string ScopedMutated = ScopedBase;
        const std::uint32_t ScopedMode = NextRandom() % 4u;
        if (ScopedMode == 0u && !ScopedMutated.empty())
        {
            ScopedMutated[NextRandom() % ScopedMutated.size()] = static_cast<char>(NextRandom() & 0x7Fu);
        }
        else if (ScopedMode == 1u)
        {
            ScopedMutated.resize(NextRandom() % (ScopedMutated.size() + 1u));
        }
        else if (ScopedMode == 2u && !ScopedMutated.empty())
        {
            ScopedMutated.erase(NextRandom() % ScopedMutated.size(), 1u + (NextRandom() % 8u));
        }
        else
        {
            ScopedMutated.insert(NextRandom() % (ScopedMutated.size() + 1u), 1u + (NextRandom() % 8u), '<');
        }
        Fby::Assets::TiledMap ScopedMap;
        std::string ScopedError;
        (void)Fby::Assets::ParseTiledMap(ScopedMutated, &ScopedMap, &ScopedError);
    }
}

void TestLzMutations()
{
    std::vector<std::byte> ScopedPlain(1024);
    for (std::size_t ScopedI = 0; ScopedI < ScopedPlain.size(); ++ScopedI)
    {
        ScopedPlain[ScopedI] = static_cast<std::byte>("FemboyEngine compression corpus"[ScopedI % 32]);
    }
    std::vector<std::byte> ScopedCompressed(ScopedPlain.size() * 2u + 256u);
    std::vector<std::byte> ScopedScratch(256u * 1024u);
    std::size_t ScopedCompressedBytes = 0;
    Check(Fby::Assets::FbyLzCompress(ScopedPlain, ScopedCompressed, ScopedScratch, &ScopedCompressedBytes),
          "valid block compresses");
    ScopedCompressed.resize(ScopedCompressedBytes);

    std::vector<std::byte> ScopedOutput(ScopedPlain.size() + 64u);
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < FuzzCount(3000); ++ScopedIteration)
    {
        std::vector<std::byte> ScopedMutated = ScopedCompressed;
        const std::uint32_t ScopedFlips = 1u + (NextRandom() % 3u);
        for (std::uint32_t ScopedFlip = 0; ScopedFlip < ScopedFlips; ++ScopedFlip)
        {
            const std::size_t ScopedIndex = NextRandom() % ScopedMutated.size();
            ScopedMutated[ScopedIndex] = static_cast<std::byte>(NextRandom() & 0xFFu);
        }
        (void)Fby::Assets::FbyLzDecompress(ScopedMutated, ScopedOutput);
    }

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < FuzzCount(1000); ++ScopedIteration)
    {
        std::vector<std::byte> ScopedRandom;
        FillRandom(ScopedRandom, NextRandom() % 256u);
        (void)Fby::Assets::FbyLzDecompress(ScopedRandom, ScopedOutput);
    }
}
}

int main()
{
    TestInputPacketMutations();
    TestArchiveMutations();
    TestFempakMutations();
    TestAsepriteMutations();
    TestTiledMutations();
    TestLzMutations();

    if (GFailures == 0)
    {
        std::printf("fuzz_parsers: OK (input packet, femarc, fempak, fby-lz mutation sweeps)\n");
        return 0;
    }
    std::fprintf(stderr, "fuzz_parsers: %d failure(s)\n", GFailures);
    return 1;
}
