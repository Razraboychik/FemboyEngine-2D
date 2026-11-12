// FemboyEngine2D - offline generator for the built-in voice reaction SFX.
#include "Engine/Audio/VoiceSfxSynth.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace
{
void WriteU16(std::ofstream& InStream, std::uint16_t InValue)
{
    const std::array<char, 2> ScopedBytes = {static_cast<char>(InValue & 0xFFu),
                                             static_cast<char>((InValue >> 8u) & 0xFFu)};
    InStream.write(ScopedBytes.data(), ScopedBytes.size());
}

void WriteU32(std::ofstream& InStream, std::uint32_t InValue)
{
    const std::array<char, 4> ScopedBytes = {
        static_cast<char>(InValue & 0xFFu), static_cast<char>((InValue >> 8u) & 0xFFu),
        static_cast<char>((InValue >> 16u) & 0xFFu), static_cast<char>((InValue >> 24u) & 0xFFu)};
    InStream.write(ScopedBytes.data(), ScopedBytes.size());
}

[[nodiscard]] bool WriteWav(const std::filesystem::path& InPath, std::span<const float> InSamples,
                            std::uint32_t InSampleRate)
{
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    if (!ScopedStream)
    {
        return false;
    }
    const std::uint32_t ScopedDataBytes = static_cast<std::uint32_t>(InSamples.size() * sizeof(std::int16_t));
    ScopedStream.write("RIFF", 4);
    WriteU32(ScopedStream, 36u + ScopedDataBytes);
    ScopedStream.write("WAVE", 4);
    ScopedStream.write("fmt ", 4);
    WriteU32(ScopedStream, 16);
    WriteU16(ScopedStream, 1);
    WriteU16(ScopedStream, 1);
    WriteU32(ScopedStream, InSampleRate);
    WriteU32(ScopedStream, InSampleRate * sizeof(std::int16_t));
    WriteU16(ScopedStream, static_cast<std::uint16_t>(sizeof(std::int16_t)));
    WriteU16(ScopedStream, 16);
    ScopedStream.write("data", 4);
    WriteU32(ScopedStream, ScopedDataBytes);
    for (const float ScopedSample : InSamples)
    {
        const float ScopedClamped = std::clamp(ScopedSample, -1.0f, 1.0f);
        const auto ScopedPcm = static_cast<std::int16_t>(ScopedClamped * 32767.0f);
        WriteU16(ScopedStream, static_cast<std::uint16_t>(ScopedPcm));
    }
    return ScopedStream.good();
}
}

int main(int InArgumentCount, char** InArguments)
{
    std::filesystem::path ScopedOutput = "assets/audio/voice_sfx";
    if (InArgumentCount > 1 && InArguments[1] != nullptr)
    {
        ScopedOutput = InArguments[1];
    }
    std::error_code ScopedError;
    std::filesystem::create_directories(ScopedOutput, ScopedError);
    if (ScopedError)
    {
        std::fprintf(stderr, "cannot create %s: %s\n", ScopedOutput.string().c_str(), ScopedError.message().c_str());
        return 1;
    }

    constexpr std::uint32_t kSampleRate = 48000;
    std::vector<float> ScopedBuffer(Fby::kVoiceSfxMaxSamples);
    std::uint32_t ScopedWritten = 0;
    for (std::uint8_t ScopedKind = 1; ScopedKind < static_cast<std::uint8_t>(Fby::VoiceSfxKind::Count); ++ScopedKind)
    {
        const auto ScopedValue = static_cast<Fby::VoiceSfxKind>(ScopedKind);
        std::fill(ScopedBuffer.begin(), ScopedBuffer.end(), 0.0f);
        const std::uint32_t ScopedCount = Fby::SynthesizeVoiceSfx(ScopedValue, kSampleRate, ScopedBuffer);
        if (ScopedCount == 0)
        {
            continue;
        }
        const std::filesystem::path ScopedPath =
            ScopedOutput / (std::string(Fby::VoiceSfxKindName(ScopedValue)) + ".wav");
        if (!WriteWav(ScopedPath, std::span<const float>(ScopedBuffer.data(), ScopedCount), kSampleRate))
        {
            std::fprintf(stderr, "cannot write %s\n", ScopedPath.string().c_str());
            return 1;
        }
        std::printf("wrote %s (%u samples)\n", ScopedPath.string().c_str(), ScopedCount);
        ++ScopedWritten;
    }
    std::printf("voice sfx: %u cue(s) generated\n", ScopedWritten);
    return ScopedWritten == 0 ? 1 : 0;
}
