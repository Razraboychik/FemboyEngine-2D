// FemboyEngine2D - voice pack manifest, discovery and process protocol tests.
#include "Engine/UI/Tts.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

bool WriteFile(const std::filesystem::path& InPath, std::string_view InText)
{
    std::ofstream ScopedFile(InPath, std::ios::binary);
    if (!ScopedFile.is_open())
    {
        return false;
    }
    ScopedFile.write(InText.data(), static_cast<std::streamsize>(InText.size()));
    return ScopedFile.good();
}

std::string ReadFile(const std::filesystem::path& InPath)
{
    std::ifstream ScopedFile(InPath, std::ios::binary);
    if (!ScopedFile.is_open())
    {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(ScopedFile), std::istreambuf_iterator<char>());
}

std::filesystem::path HelperPath(const char* InArgvZero)
{
    std::filesystem::path ScopedDirectory = std::filesystem::path(InArgvZero).parent_path();
    if (ScopedDirectory.empty())
    {
        ScopedDirectory = std::filesystem::current_path();
    }
#if defined(_WIN32)
    return ScopedDirectory / "fby_tts_voicepack_helper.exe";
#else
    return ScopedDirectory / "fby_tts_voicepack_helper";
#endif
}
}

int main(int argc, char** argv)
{
    using namespace Fby;

    const std::filesystem::path ScopedHelper = HelperPath(argc > 0 ? argv[0] : "");
    FBY_CHECK(std::filesystem::is_regular_file(ScopedHelper));

    const std::filesystem::path ScopedRoot = std::filesystem::current_path() / "_fby_voicepack_test";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot / "pack1", ScopedError);
    std::filesystem::create_directories(ScopedRoot / "broken", ScopedError);
    FBY_CHECK(WriteFile(ScopedRoot / "pack1" / "model.bin", "model"));
    FBY_CHECK(WriteFile(ScopedRoot / "broken" / "voicepack.ini", "id = broken\nname = Broken\n"));
    FBY_CHECK(WriteFile(ScopedRoot / "pack1" / "voicepack.ini", "id = testpack\n"
                                                                "name = Test Pack\n"
                                                                "language = en-US\n"
                                                                "license = MIT\n"
                                                                "helper = " +
                                                                    ScopedHelper.string() +
                                                                    "\n"
                                                                    "model = model.bin\n"));

    // Registry: one good pack, one skipped manifest, resolved helper path.
    VoicePackRegistry ScopedRegistry;
    FBY_CHECK(ScopedRegistry.Scan((ScopedRoot).string()) == 1);
    FBY_CHECK(ScopedRegistry.Skipped() == 1);
    const VoicePackInfo* ScopedPack = ScopedRegistry.Find("testpack");
    FBY_CHECK(ScopedPack != nullptr);
    if (ScopedPack != nullptr)
    {
        FBY_CHECK(std::strcmp(ScopedPack->Name, "Test Pack") == 0);
        FBY_CHECK(std::strcmp(ScopedPack->Language, "en-US") == 0);
        FBY_CHECK(std::strcmp(ScopedPack->License, "MIT") == 0);
        FBY_CHECK(std::filesystem::is_regular_file(ScopedPack->Helper));
        FBY_CHECK(std::filesystem::is_regular_file(ScopedPack->Model));
    }
    FBY_CHECK(ScopedRegistry.Scan((ScopedRoot / "pack1").string()) == 1);
    FBY_CHECK(ScopedRegistry.Scan((ScopedRoot / "missing").string()) == 0);

    // Backend: enumeration, explicit pack selection and the pipe contract.
    TtsVoicePack ScopedPacks;
    FBY_CHECK(ScopedPacks.Initialize(ScopedRoot.string()));
    TtsVoiceInfo ScopedVoices[4]{};
    FBY_CHECK(ScopedPacks.EnumerateVoices(ScopedVoices, 4) == 1);
    FBY_CHECK(std::strcmp(ScopedVoices[0].Id, "pack:testpack") == 0);
    FBY_CHECK(std::strcmp(ScopedVoices[0].Name, "Test Pack") == 0);
    FBY_CHECK(!ScopedPacks.SetVoiceById("testpack"));
    FBY_CHECK(ScopedPacks.SetVoiceById("pack:testpack"));
    FBY_CHECK(!ScopedPacks.SetVoiceById("pack:missing"));
    ScopedPacks.SetRate(1.5f);
    FBY_CHECK(ScopedPacks.Speak("hello world", TtsPriority::Normal));
    ScopedPacks.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    ScopedPacks.Shutdown();

    const std::string ScopedProtocol = ReadFile(ScopedRoot / "pack1" / "model.bin.log");
    FBY_CHECK(ScopedProtocol.find("VOICE testpack") != std::string::npos);
    FBY_CHECK(ScopedProtocol.find("RATE 1.500") != std::string::npos);
    FBY_CHECK(ScopedProtocol.find("SPEAK 11") != std::string::npos);
    FBY_CHECK(ScopedProtocol.find("hello world") != std::string::npos);
    FBY_CHECK(ScopedProtocol.find("STOP") != std::string::npos);
    FBY_CHECK(ScopedProtocol.find("QUIT") != std::string::npos);

    // Layered backend: a null OS backend still exposes and speaks packs.
    LayeredTts ScopedLayered;
    FBY_CHECK(ScopedLayered.Initialize(nullptr, ScopedRoot.string()));
    FBY_CHECK(ScopedLayered.Backend() != nullptr);
    FBY_CHECK(ScopedLayered.Backend()->enumerateVoices != nullptr);
    FBY_CHECK(ScopedLayered.Backend()->speak != nullptr);
    TtsVoiceInfo ScopedLayeredVoices[4]{};
    FBY_CHECK(ScopedLayered.Backend()->enumerateVoices(ScopedLayered.Backend()->user, ScopedLayeredVoices, 4) == 1);
    ScopedLayered.Backend()->setRate(ScopedLayered.Backend()->user, 1.8f);
    FBY_CHECK(ScopedLayered.Backend()->setVoiceById(ScopedLayered.Backend()->user, "pack:testpack"));
    FBY_CHECK(ScopedLayered.Backend()->speak(ScopedLayered.Backend()->user, "layered", 7, TtsPriority::Normal));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    ScopedLayered.Shutdown();
    const std::string ScopedLayeredProtocol = ReadFile(ScopedRoot / "pack1" / "model.bin.log");
    FBY_CHECK(ScopedLayeredProtocol.find("layered") != std::string::npos);
    FBY_CHECK(ScopedLayeredProtocol.find("RATE 1.800") != std::string::npos);

    // A language request reaches the pack when no OS synthesizer exists.
    ScopedPacks.SetVoice("en-GB");
    FBY_CHECK(ScopedPacks.Speak("by language", TtsPriority::Normal));

    std::filesystem::remove_all(ScopedRoot, ScopedError);

    if (GFailures == 0)
    {
        std::printf("tts_voicepack_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "tts_voicepack_test: %u failure(s)\n", GFailures);
    return 1;
}
