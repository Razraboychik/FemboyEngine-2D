// FemboyEngine2D - native text-to-speech bridge.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fby
{

enum class TtsPriority : std::uint32_t
{
    // Urgent state change, cancel current speech
    Interrupt = 0,
    Normal = 1,
    Queued = 2,
};

inline constexpr std::uint32_t kMaxTtsVoices = 64;
inline constexpr std::uint32_t kTtsVoiceIdBytes = 192;

// One installed voice, as the platform reports it. The id is backend-specific
// and stable for the same install: SAPI stores the token id, AVSpeech the
// AVSpeechSynthesisVoice identifier.
struct TtsVoiceInfo
{
    char Id[kTtsVoiceIdBytes]{};
    char Name[64]{};
    char Language[16]{};
    // 0 unknown, 1 male, 2 female; platforms without the attribute report 0.
    std::uint8_t Gender = 0;
    std::uint8_t Reserved0 = 0;
    std::uint16_t Reserved1 = 0;
};

struct TtsBackend
{
    void* user = nullptr;
    bool (*speak)(void* InUser, const char* InUtf8, std::size_t InLength, TtsPriority InPriority) noexcept = nullptr;
    void (*stop)(void* InUser) noexcept = nullptr;
    void (*setRate)(void* InUser, float InRate) noexcept = nullptr;
    // Select a voice by BCP-47 language tag ("en-US", "ru-RU").
    void (*setVoice)(void* InUser, const char* InLanguageBCP47) noexcept = nullptr;
    // Select an exact installed voice by TtsVoiceInfo::Id. Optional: a backend
    // without it falls back to the language search.
    bool (*setVoiceById)(void* InUser, const char* InId) noexcept = nullptr;
    // Returns the number of voices written, at most InCapacity. Optional.
    std::uint32_t (*enumerateVoices)(void* InUser, TtsVoiceInfo* OutVoices,
                                     std::uint32_t InCapacity) noexcept = nullptr;
};

// Windows SAPI 5 adapter. The implementation is a separate TU; this header has
class SapiTts
{
public:
    SapiTts() = default;
    ~SapiTts();

    SapiTts(const SapiTts&) = delete;
    SapiTts& operator=(const SapiTts&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
        return MVoice != nullptr;
    }
    [[nodiscard]] bool Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept;
    void Stop() noexcept;
    void SetRate(float InRate) noexcept;
    // Must be called from the OS main thread (SAPI is apartment-threaded).
    void SetVoice(const char* InLanguageBCP47) noexcept;
    // Exact token selection; false when the id is unknown or SAPI rejects it.
    [[nodiscard]] bool SetVoiceById(const char* InId) noexcept;
    // Fills installed desktop voices; returns the number written.
    [[nodiscard]] std::uint32_t EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept;
    [[nodiscard]] TtsBackend Backend() noexcept;

private:
    // ISpVoice*, hidden from this header
    void* MVoice = nullptr;
    bool MComInitialized = false;
    std::uint32_t MThreadId = 0;
};

// macOS AVSpeechSynthesizer adapter. The Objective-C++ implementation lives.
class AvSpeechTts
{
public:
    AvSpeechTts() = default;
    ~AvSpeechTts();

    AvSpeechTts(const AvSpeechTts&) = delete;
    AvSpeechTts& operator=(const AvSpeechTts&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
        return MSynthesizer != nullptr;
    }
    [[nodiscard]] bool Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept;
    void Stop() noexcept;
    void SetRate(float InRate) noexcept;
    // Must be called from the main run loop. Resolves the BCP-47 tag against
    void SetVoice(const char* InLanguageBCP47) noexcept;
    // Exact AVSpeechSynthesisVoice identifier; false when it is not installed.
    [[nodiscard]] bool SetVoiceById(const char* InId) noexcept;
    // Fills installed AVSpeech voices; returns the number written.
    [[nodiscard]] std::uint32_t EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept;
    [[nodiscard]] TtsBackend Backend() noexcept;

private:
    // AVSpeechSynthesizer*, retained through CFBridgingRetain
    void* MSynthesizer = nullptr;
    char MLanguage[16] = {};
    // Explicit voice identifier; empty means "resolve by language".
    char MVoiceId[kTtsVoiceIdBytes] = {};
    float MRate = 0.0f;
};

#if defined(__linux__)
// speech-dispatcher client, loaded at runtime. Loading it dynamically keeps the
// engine build free of a hard dependency on the daemon and on the LGPL client
// library: a machine without it simply has no system voice and the narrator
// reports the fallback once.
class SpeechDispatcherTts
{
public:
    SpeechDispatcherTts() = default;
    ~SpeechDispatcherTts();

    SpeechDispatcherTts(const SpeechDispatcherTts&) = delete;
    SpeechDispatcherTts& operator=(const SpeechDispatcherTts&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
        return MConnection != nullptr;
    }
    [[nodiscard]] bool Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept;
    void Stop() noexcept;
    void SetRate(float InRate) noexcept;
    void SetVoice(const char* InLanguageBCP47) noexcept;
    // speech-dispatcher has no stable, enumerable voice ids; the language
    // selection above is the whole voice policy on this platform.
    [[nodiscard]] bool SetVoiceById(const char* InId) noexcept;
    [[nodiscard]] std::uint32_t EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept;
    [[nodiscard]] TtsBackend Backend() noexcept;

private:
    void* MModule = nullptr;
    void* MConnection = nullptr;
    TtsBackend MBackend{};
};
#endif

#if defined(_WIN32)
// Windows one-core voices through the modern speech stack (C++/WinRT). The
// desktop SAPI voices are not visible to it and it is not visible to SAPI, so
// the engine asks both and routes by the selected voice id. Pimpl keeps the
// WinRT projection out of the engine include graph.
class WinRtTts
{
public:
    WinRtTts() = default;
    ~WinRtTts();

    WinRtTts(const WinRtTts&) = delete;
    WinRtTts& operator=(const WinRtTts&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
        return MReady;
    }
    [[nodiscard]] bool Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept;
    void Stop() noexcept;
    void SetRate(float InRate) noexcept;
    void SetVoice(const char* InLanguageBCP47) noexcept;
    [[nodiscard]] bool SetVoiceById(const char* InId) noexcept;
    [[nodiscard]] std::uint32_t EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept;
    [[nodiscard]] TtsBackend Backend() noexcept;

private:
    void* MImpl = nullptr;
    bool MReady = false;
    TtsBackend MBackend{};
};

// Desktop SAPI voices plus the WinRT one-core voices behind one backend. Ids of
// the latter carry a "winrt:" prefix so a stored narration_voice is unambiguous.
class CompositeTts
{
public:
    CompositeTts() = default;
    ~CompositeTts();

    CompositeTts(const CompositeTts&) = delete;
    CompositeTts& operator=(const CompositeTts&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept;
    void Stop() noexcept;
    void SetRate(float InRate) noexcept;
    void SetVoice(const char* InLanguageBCP47) noexcept;
    [[nodiscard]] bool SetVoiceById(const char* InId) noexcept;
    [[nodiscard]] std::uint32_t EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept;
    [[nodiscard]] TtsBackend Backend() noexcept;

private:
    enum class Route : std::uint8_t
    {
        Sapi = 0,
        WinRt = 1,
    };

    SapiTts MSapi;
    WinRtTts MWinRt;
    TtsBackend MBackend{};
    Route MRoute = Route::Sapi;
    bool MInitialized = false;
};
#endif

// One installed voice pack. A pack is a directory with voicepack.ini, a helper
// executable and a model; the engine never links the helper, it only starts it
// and speaks to it over a pipe, so a GPL helper or model never enters the
// engine's own licence terms.
inline constexpr std::uint32_t kMaxVoicePacks = 16;
inline constexpr std::uint32_t kVoicePackIdBytes = 48;
inline constexpr std::uint32_t kVoicePackNameBytes = 96;
inline constexpr std::uint32_t kVoicePackPathBytes = 512;

struct VoicePackInfo
{
    char Id[kVoicePackIdBytes]{};
    char Name[kVoicePackNameBytes]{};
    char Language[16]{};
    // Free-form licence tag of the pack and its helper ("MIT", "CC-BY-4.0",
    // "GPL-3.0-only", ...). The engine stores it so the launcher can show it and
    // the license inventory can list it; it does not interpret the value.
    char License[48]{};
    char Helper[kVoicePackPathBytes]{};
    char Model[kVoicePackPathBytes]{};
};

// Discovers voice packs. The registry is a plain scanner: it never executes a
// helper, so enumerating voices cannot run third-party code.
class VoicePackRegistry
{
public:
    VoicePackRegistry() = default;

    void Clear() noexcept;
    // Scans a directory of pack directories, or a single pack directory. A pack
    // without voicepack.ini, without an id/name/language or with a missing
    // helper/model is skipped and counted in Skipped().
    std::uint32_t Scan(std::string_view InRoot) noexcept;
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] const VoicePackInfo* At(std::uint32_t InIndex) const noexcept
    {
        return InIndex < MCount ? &MPacks[InIndex] : nullptr;
    }
    [[nodiscard]] const VoicePackInfo* Find(std::string_view InId) const noexcept;
    [[nodiscard]] std::uint32_t Skipped() const noexcept
    {
        return MSkipped;
    }

private:
    void Add(std::string_view InDirectory, std::uint32_t& SkippedCount) noexcept;

    std::array<VoicePackInfo, kMaxVoicePacks> MPacks{};
    std::uint32_t MCount = 0;
    std::uint32_t MSkipped = 0;
};

// Own-voice backend. It starts the selected pack's helper once, then streams
// newline-delimited commands to its stdin: VOICE, RATE, SPEAK with a byte
// length and the UTF-8 text, STOP and QUIT. The protocol is length-prefixed so
// speech text may contain any byte except the terminating newline.
//
// The helper owns audio output; this keeps the engine free of any audio codec
// or phonemizer licence and lets a pack update independently. See
// Documentation/design/voice-packs.md for the MPL 2.0 process-boundary rules.
class TtsVoicePack
{
public:
    TtsVoicePack() = default;
    ~TtsVoicePack();

    TtsVoicePack(const TtsVoicePack&) = delete;
    TtsVoicePack& operator=(const TtsVoicePack&) = delete;

    [[nodiscard]] bool Initialize(std::string_view InRoot) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
        return MRegistry.Count() > 0;
    }
    [[nodiscard]] std::uint32_t EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept;
    // Accepts "pack:<id>" only; a bare id is rejected so a stored OS voice id
    // can never be captured by a pack with the same spelling.
    [[nodiscard]] bool SetVoiceById(const char* InId) noexcept;
    // Chooses the first pack whose language starts with the tag's primary
    // subtag; the explicit pack id always wins.
    void SetVoice(const char* InLanguageBCP47) noexcept;
    void SetRate(float InRate) noexcept;
    [[nodiscard]] bool Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept;
    void Stop() noexcept;
    [[nodiscard]] TtsBackend Backend() noexcept;

private:
    [[nodiscard]] bool EnsureProcess() noexcept;
    void CloseProcess() noexcept;
    [[nodiscard]] bool WriteAll(std::string_view InBytes) noexcept;
    const VoicePackInfo* Selected() const noexcept;

    VoicePackRegistry MRegistry;
    // Opaque process/handle storage: HANDLE on Windows, pid_t and file
    // descriptor on POSIX. Zero means "not running".
    std::intptr_t MProcess = 0;
    std::intptr_t MStdin = 0;
    std::intptr_t MThread = 0;
    std::uint32_t MSelectedIndex = 0;
    bool MHasSelection = false;
    bool MBroken = false;
    float MRate = 1.0f;
    TtsBackend MBackend{};
};

// OS voices plus voice packs behind one backend. "pack:<id>" selects a pack;
// every other id and every language request goes to the OS backend. A machine
// without packs behaves exactly like the OS backend alone.
class LayeredTts
{
public:
    LayeredTts() = default;
    ~LayeredTts();

    LayeredTts(const LayeredTts&) = delete;
    LayeredTts& operator=(const LayeredTts&) = delete;

    // InOs is owned by the caller and may be null. The pack root is scanned
    // once; a missing directory is not an error.
    [[nodiscard]] bool Initialize(const TtsBackend* InOs, std::string_view InPackRoot) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] const TtsBackend* Backend() noexcept;
    [[nodiscard]] TtsVoicePack& Packs() noexcept
    {
        return MPacks;
    }

private:
    const TtsBackend* Selected() const noexcept;
    void UseOs() noexcept
    {
        MSelected = MOs;
    }

    TtsVoicePack MPacks;
    const TtsBackend* MOs = nullptr;
    const TtsBackend* MSelected = nullptr;
    TtsBackend MPackBackend{};
    TtsBackend MBackend{};
    float MLastRate = 1.0f;
    bool MInitialized = false;
};

// Process-wide backend for the current platform.
[[nodiscard]] const TtsBackend* NativeTtsBackend() noexcept;
// Installed voices of the process backend. Main thread only; returns the number
// of entries written, or 0 when the platform has no synthesizer.
[[nodiscard]] std::uint32_t EnumerateNativeTtsVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept;
void ShutdownNativeTts() noexcept;
}
