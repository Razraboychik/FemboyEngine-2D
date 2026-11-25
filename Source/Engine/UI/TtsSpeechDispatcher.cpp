// FemboyEngine2D - Linux speech-dispatcher adapter.
//
// The client library is LGPL, so the engine loads it at runtime instead of
// linking it. The symbols used are the long-stable ones from libspeechd.h; the
// prototypes are declared here to keep the build independent of the development
// headers (a runtime-only installation is enough).
#include "Engine/UI/Tts.hpp"

#include "Engine/Core/Log.hpp"

#if defined(__linux__)
#include <dlfcn.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Fby
{
namespace
{
// Opaque client connection; the layout belongs to speech-dispatcher.
struct SpdConnection;

// Priorities and modes from libspeechd.h. The values are part of the ABI.
constexpr int SpdImportant = 1;
constexpr int SpdText = 3;
constexpr int SpdModeSingle = 0;

using SpdOpenFn = SpdConnection* (*)(const char*, const char*, const char*, int);
using SpdCloseFn = void (*)(SpdConnection*);
using SpdSayFn = int (*)(SpdConnection*, int, const char*);
using SpdStopFn = int (*)(SpdConnection*);
using SpdSetRateFn = int (*)(SpdConnection*, int);
using SpdSetLanguageFn = int (*)(SpdConnection*, const char*);

struct SpdApi
{
    void* Module = nullptr;
    SpdOpenFn Open = nullptr;
    SpdCloseFn Close = nullptr;
    SpdSayFn Say = nullptr;
    SpdStopFn Stop = nullptr;
    SpdSetRateFn SetRate = nullptr;
    SpdSetLanguageFn SetLanguage = nullptr;
};

void CloseApi(SpdApi& InApi) noexcept
{
    if (InApi.Module != nullptr)
    {
        (void)::dlclose(InApi.Module);
    }
    InApi = SpdApi{};
}

[[nodiscard]] bool LoadApi(SpdApi& OutApi) noexcept
{
    // The versioned soname first, the development symlink second.
    OutApi.Module = ::dlopen("libspeechd.so.2", RTLD_NOW | RTLD_LOCAL);
    if (OutApi.Module == nullptr)
    {
        OutApi.Module = ::dlopen("libspeechd.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (OutApi.Module == nullptr)
    {
        return false;
    }
    OutApi.Open = reinterpret_cast<SpdOpenFn>(::dlsym(OutApi.Module, "spd_open"));
    OutApi.Close = reinterpret_cast<SpdCloseFn>(::dlsym(OutApi.Module, "spd_close"));
    OutApi.Say = reinterpret_cast<SpdSayFn>(::dlsym(OutApi.Module, "spd_say"));
    OutApi.Stop = reinterpret_cast<SpdStopFn>(::dlsym(OutApi.Module, "spd_stop"));
    OutApi.SetRate = reinterpret_cast<SpdSetRateFn>(::dlsym(OutApi.Module, "spd_set_voice_rate"));
    OutApi.SetLanguage = reinterpret_cast<SpdSetLanguageFn>(::dlsym(OutApi.Module, "spd_set_language"));
    if (OutApi.Open == nullptr || OutApi.Close == nullptr || OutApi.Say == nullptr || OutApi.Stop == nullptr)
    {
        CloseApi(OutApi);
        return false;
    }
    return true;
}

// Primary BCP-47 subtag ("ru-RU" -> "ru"): speech-dispatcher selects the
// synthesizer voice by language, not by a full locale.
void PrimaryLanguage(const char* InTag, char* OutCode, std::size_t InBytes) noexcept
{
    if (OutCode == nullptr || InBytes == 0)
    {
        return;
    }
    std::size_t ScopedIndex = 0;
    while (InTag != nullptr && InTag[ScopedIndex] != '\0' && InTag[ScopedIndex] != '-' && InTag[ScopedIndex] != '_' &&
           ScopedIndex + 1 < InBytes)
    {
        OutCode[ScopedIndex] = InTag[ScopedIndex];
        ++ScopedIndex;
    }
    OutCode[ScopedIndex] = '\0';
}
}

SpeechDispatcherTts::~SpeechDispatcherTts()
{
    Shutdown();
}

bool SpeechDispatcherTts::Initialize() noexcept
{
    if (MConnection != nullptr)
    {
        return true;
    }
    auto* ScopedApi = new (std::nothrow) SpdApi();
    if (ScopedApi == nullptr)
    {
        return false;
    }
    if (!LoadApi(*ScopedApi))
    {
        delete ScopedApi;
        LogInfo("tts", "speech-dispatcher client is not installed; the narrator stays silent on this machine");
        return false;
    }
    // A single-threaded connection serializes the calls; the narrator is
    // main-thread only, so no locking is needed above this layer.
    SpdConnection* ScopedConnection = ScopedApi->Open("FemboyEngine2D", "main", nullptr, SpdModeSingle);
    if (ScopedConnection == nullptr)
    {
        LogWarn("tts", "speech-dispatcher is installed but the daemon is not reachable; the narrator stays silent");
        CloseApi(*ScopedApi);
        delete ScopedApi;
        return false;
    }
    MModule = ScopedApi;
    MConnection = ScopedConnection;
    MBackend.user = this;
    MBackend.speak = +[](void* InUser, const char* InUtf8, std::size_t InLength, TtsPriority InPriority) noexcept
    {
        return static_cast<SpeechDispatcherTts*>(InUser)->Speak(std::string_view{InUtf8, InLength}, InPriority);
    };
    MBackend.stop = +[](void* InUser) noexcept
    {
        static_cast<SpeechDispatcherTts*>(InUser)->Stop();
    };
    MBackend.setRate = +[](void* InUser, float InRate) noexcept
    {
        static_cast<SpeechDispatcherTts*>(InUser)->SetRate(InRate);
    };
    MBackend.setVoice = +[](void* InUser, const char* InLanguage) noexcept
    {
        static_cast<SpeechDispatcherTts*>(InUser)->SetVoice(InLanguage);
    };
    MBackend.setVoiceById = +[](void* InUser, const char* InId) noexcept
    {
        return static_cast<SpeechDispatcherTts*>(InUser)->SetVoiceById(InId);
    };
    MBackend.enumerateVoices = +[](void* InUser, TtsVoiceInfo* InVoices, std::uint32_t InCapacity) noexcept
    {
        return static_cast<SpeechDispatcherTts*>(InUser)->EnumerateVoices(InVoices, InCapacity);
    };
    LogInfo("tts", "speech-dispatcher client ready");
    return true;
}

void SpeechDispatcherTts::Shutdown() noexcept
{
    if (MConnection != nullptr && MModule != nullptr)
    {
        auto* ScopedApi = static_cast<SpdApi*>(MModule);
        if (ScopedApi->Close != nullptr)
        {
            ScopedApi->Close(static_cast<SpdConnection*>(MConnection));
        }
        CloseApi(*ScopedApi);
        delete ScopedApi;
    }
    MConnection = nullptr;
    MModule = nullptr;
}

bool SpeechDispatcherTts::Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept
{
    if (MConnection == nullptr || MModule == nullptr || InUtf8.empty())
    {
        return false;
    }
    auto* ScopedApi = static_cast<SpdApi*>(MModule);
    // spd_say copies the text into the protocol message on the call, so a stack
    // buffer is enough. Utterances are capped by the narrator.
    char ScopedText[512]{};
    const std::size_t ScopedLength = std::min(InUtf8.size(), sizeof(ScopedText) - 1);
    std::memcpy(ScopedText, InUtf8.data(), ScopedLength);
    if (InPriority == TtsPriority::Interrupt && ScopedApi->Stop != nullptr)
    {
        (void)ScopedApi->Stop(static_cast<SpdConnection*>(MConnection));
    }
    const int ScopedPriority = InPriority == TtsPriority::Interrupt ? SpdImportant : SpdText;
    return ScopedApi->Say(static_cast<SpdConnection*>(MConnection), ScopedPriority, ScopedText) >= 0;
}

void SpeechDispatcherTts::Stop() noexcept
{
    if (MConnection == nullptr || MModule == nullptr)
    {
        return;
    }
    auto* ScopedApi = static_cast<SpdApi*>(MModule);
    if (ScopedApi->Stop != nullptr)
    {
        (void)ScopedApi->Stop(static_cast<SpdConnection*>(MConnection));
    }
}

void SpeechDispatcherTts::SetRate(float InRate) noexcept
{
    if (MConnection == nullptr || MModule == nullptr)
    {
        return;
    }
    auto* ScopedApi = static_cast<SpdApi*>(MModule);
    if (ScopedApi->SetRate == nullptr)
    {
        return;
    }
    // TtsBackend uses the SAPI convention (-10..10); speech-dispatcher uses
    // -100..100 with 0 as normal.
    const int ScopedRate = static_cast<int>(std::clamp(InRate, -10.0f, 10.0f) * 10.0f);
    (void)ScopedApi->SetRate(static_cast<SpdConnection*>(MConnection), ScopedRate);
}

void SpeechDispatcherTts::SetVoice(const char* InLanguageBCP47) noexcept
{
    if (MConnection == nullptr || MModule == nullptr || InLanguageBCP47 == nullptr || InLanguageBCP47[0] == '\0')
    {
        return;
    }
    auto* ScopedApi = static_cast<SpdApi*>(MModule);
    if (ScopedApi->SetLanguage == nullptr)
    {
        return;
    }
    char ScopedLanguage[16]{};
    PrimaryLanguage(InLanguageBCP47, ScopedLanguage, sizeof(ScopedLanguage));
    if (ScopedLanguage[0] == '\0')
    {
        return;
    }
    (void)ScopedApi->SetLanguage(static_cast<SpdConnection*>(MConnection), ScopedLanguage);
}

bool SpeechDispatcherTts::SetVoiceById(const char* InId) noexcept
{
    (void)InId;
    return false;
}

std::uint32_t SpeechDispatcherTts::EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept
{
    (void)OutVoices;
    (void)InCapacity;
    return 0;
}

TtsBackend SpeechDispatcherTts::Backend() noexcept
{
    return MBackend;
}
}
#endif
