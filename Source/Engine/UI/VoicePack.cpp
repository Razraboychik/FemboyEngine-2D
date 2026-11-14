// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// FemboyEngine2D - own-voice packs and the process-isolated pack backend.
//
// Licence note: the engine is MPL 2.0. A voice pack may bundle a helper or a
// model under another licence (MIT, CC-BY, GPL). The engine never links either
// of them: it starts the helper as a separate process and talks to it over a
// pipe, so the pack stays an independent work. See
// Documentation/design/voice-packs.md before changing this file.
#include "Engine/UI/Tts.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/StoragePaths.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace Fby
{
namespace
{
namespace fs = std::filesystem;

std::string_view Trim(std::string_view InText) noexcept
{
    while (!InText.empty() && (InText.front() == ' ' || InText.front() == '\t' || InText.front() == '\r'))
    {
        InText.remove_prefix(1);
    }
    while (!InText.empty() && (InText.back() == ' ' || InText.back() == '\t' || InText.back() == '\r'))
    {
        InText.remove_suffix(1);
    }
    return InText;
}

void CopyText(char* OutText, std::size_t InCapacity, std::string_view InText) noexcept
{
    if (OutText == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min(InText.size(), InCapacity - 1);
    std::memcpy(OutText, InText.data(), ScopedLength);
    OutText[ScopedLength] = '\0';
}

char LowerAscii(char InChar) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(InChar)));
}

bool EqualsIgnoreCase(std::string_view InLeft, std::string_view InRight) noexcept
{
    if (InLeft.size() != InRight.size())
    {
        return false;
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < InLeft.size(); ++ScopedIndex)
    {
        if (LowerAscii(InLeft[ScopedIndex]) != LowerAscii(InRight[ScopedIndex]))
        {
            return false;
        }
    }
    return true;
}

bool StartsWith(std::string_view InText, std::string_view InPrefix) noexcept
{
    return InText.size() >= InPrefix.size() && InText.substr(0, InPrefix.size()) == InPrefix;
}

std::string_view PrimarySubtag(std::string_view InLanguage) noexcept
{
    const std::size_t ScopedSeparator = InLanguage.find_first_of("-_");
    return ScopedSeparator == std::string_view::npos ? InLanguage : InLanguage.substr(0, ScopedSeparator);
}

// Minimal voicepack.ini reader: key = value, # and ; comments, quotes optional.
bool ReadManifest(const fs::path& InPath, VoicePackInfo& OutInfo, std::string& OutHelper, std::string& OutModel,
                  std::uint32_t& SkippedCount) noexcept
{
    std::ifstream ScopedFile(InPath);
    if (!ScopedFile.is_open())
    {
        return false;
    }
    std::string ScopedLine;
    while (std::getline(ScopedFile, ScopedLine))
    {
        const std::string_view ScopedTrimmed = Trim(ScopedLine);
        if (ScopedTrimmed.empty() || ScopedTrimmed.front() == '#' || ScopedTrimmed.front() == ';' ||
            ScopedTrimmed.front() == '[')
        {
            continue;
        }
        const std::size_t ScopedEquals = ScopedTrimmed.find('=');
        if (ScopedEquals == std::string_view::npos)
        {
            continue;
        }
        const std::string_view ScopedKey = Trim(ScopedTrimmed.substr(0, ScopedEquals));
        std::string_view ScopedValue = Trim(ScopedTrimmed.substr(ScopedEquals + 1));
        if (ScopedValue.size() >= 2 && ScopedValue.front() == '"' && ScopedValue.back() == '"')
        {
            ScopedValue = ScopedValue.substr(1, ScopedValue.size() - 2);
        }
        if (ScopedKey == "id")
        {
            CopyText(OutInfo.Id, sizeof(OutInfo.Id), ScopedValue);
        }
        else if (ScopedKey == "name")
        {
            CopyText(OutInfo.Name, sizeof(OutInfo.Name), ScopedValue);
        }
        else if (ScopedKey == "language")
        {
            CopyText(OutInfo.Language, sizeof(OutInfo.Language), ScopedValue);
        }
        else if (ScopedKey == "license")
        {
            CopyText(OutInfo.License, sizeof(OutInfo.License), ScopedValue);
        }
        else if (ScopedKey == "helper")
        {
            OutHelper.assign(ScopedValue);
        }
        else if (ScopedKey == "model")
        {
            OutModel.assign(ScopedValue);
        }
    }
    return OutInfo.Id[0] != '\0' && OutInfo.Name[0] != '\0' && OutInfo.Language[0] != '\0' && !OutHelper.empty() &&
           !OutModel.empty();
}

#if defined(_WIN32)
std::wstring Widen(std::string_view InText) noexcept
{
    if (InText.empty())
    {
        return std::wstring();
    }
    const int ScopedLength =
        MultiByteToWideChar(CP_UTF8, 0, InText.data(), static_cast<int>(InText.size()), nullptr, 0);
    if (ScopedLength <= 0)
    {
        return std::wstring();
    }
    std::wstring ScopedResult(static_cast<std::size_t>(ScopedLength), L'\0');
    (void)MultiByteToWideChar(CP_UTF8, 0, InText.data(), static_cast<int>(InText.size()), ScopedResult.data(),
                              ScopedLength);
    return ScopedResult;
}

std::wstring Quote(const std::string& InText) noexcept
{
    std::wstring ScopedWide = Widen(InText);
    std::wstring ScopedQuoted;
    ScopedQuoted.push_back(L'"');
    ScopedQuoted += ScopedWide;
    ScopedQuoted.push_back(L'"');
    return ScopedQuoted;
}
#endif
}

void VoicePackRegistry::Clear() noexcept
{
    MCount = 0;
    MSkipped = 0;
    std::memset(MPacks.data(), 0, sizeof(VoicePackInfo) * MPacks.size());
}

void VoicePackRegistry::Add(std::string_view InDirectory, std::uint32_t& SkippedCount) noexcept
{
    const fs::path ScopedDirectory{std::string(InDirectory)};
    VoicePackInfo ScopedInfo{};
    std::string ScopedHelper;
    std::string ScopedModel;
    if (!ReadManifest(ScopedDirectory / "voicepack.ini", ScopedInfo, ScopedHelper, ScopedModel, SkippedCount))
    {
        ++SkippedCount;
        return;
    }
    const fs::path ScopedHelperPath =
        fs::path(ScopedHelper).is_absolute() ? fs::path(ScopedHelper) : ScopedDirectory / fs::path(ScopedHelper);
    const fs::path ScopedModelPath =
        fs::path(ScopedModel).is_absolute() ? fs::path(ScopedModel) : ScopedDirectory / fs::path(ScopedModel);
    std::error_code ScopedError;
    if (!fs::is_regular_file(ScopedHelperPath, ScopedError) || !fs::is_regular_file(ScopedModelPath, ScopedError))
    {
        ++SkippedCount;
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (std::strcmp(MPacks[ScopedIndex].Id, ScopedInfo.Id) == 0)
        {
            ++SkippedCount;
            return;
        }
    }
    if (MCount >= kMaxVoicePacks)
    {
        ++SkippedCount;
        return;
    }
    CopyText(ScopedInfo.Helper, sizeof(ScopedInfo.Helper), ScopedHelperPath.string());
    CopyText(ScopedInfo.Model, sizeof(ScopedInfo.Model), ScopedModelPath.string());
    MPacks[MCount++] = ScopedInfo;
}

std::uint32_t VoicePackRegistry::Scan(std::string_view InRoot) noexcept
{
    Clear();
    if (InRoot.empty())
    {
        return 0;
    }
    std::error_code ScopedError;
    const fs::path ScopedRoot{std::string(InRoot)};
    if (!fs::is_directory(ScopedRoot, ScopedError))
    {
        return 0;
    }
    if (fs::is_regular_file(ScopedRoot / "voicepack.ini", ScopedError))
    {
        Add(ScopedRoot.string(), MSkipped);
        return MCount;
    }
    for (const fs::directory_entry& ScopedEntry : fs::directory_iterator(ScopedRoot, ScopedError))
    {
        if (ScopedEntry.is_directory(ScopedError))
        {
            Add(ScopedEntry.path().string(), MSkipped);
        }
    }
    return MCount;
}

const VoicePackInfo* VoicePackRegistry::Find(std::string_view InId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (InId == MPacks[ScopedIndex].Id)
        {
            return &MPacks[ScopedIndex];
        }
    }
    return nullptr;
}

TtsVoicePack::~TtsVoicePack()
{
    Shutdown();
}

bool TtsVoicePack::Initialize(std::string_view InRoot) noexcept
{
    const std::uint32_t ScopedCount = MRegistry.Scan(InRoot);
    MBackend.user = this;
    MBackend.speak = +[](void* InUser, const char* InUtf8, std::size_t InLength, TtsPriority InPriority) noexcept
    {
        return static_cast<TtsVoicePack*>(InUser)->Speak(std::string_view{InUtf8, InLength}, InPriority);
    };
    MBackend.stop = +[](void* InUser) noexcept
    {
        static_cast<TtsVoicePack*>(InUser)->Stop();
    };
    MBackend.setRate = +[](void* InUser, float InRate) noexcept
    {
        static_cast<TtsVoicePack*>(InUser)->SetRate(InRate);
    };
    MBackend.setVoice = +[](void* InUser, const char* InLanguage) noexcept
    {
        static_cast<TtsVoicePack*>(InUser)->SetVoice(InLanguage);
    };
    MBackend.setVoiceById = +[](void* InUser, const char* InId) noexcept
    {
        return static_cast<TtsVoicePack*>(InUser)->SetVoiceById(InId);
    };
    MBackend.enumerateVoices = +[](void* InUser, TtsVoiceInfo* InVoices, std::uint32_t InCapacity) noexcept
    {
        return static_cast<TtsVoicePack*>(InUser)->EnumerateVoices(InVoices, InCapacity);
    };
    if (ScopedCount > 0)
    {
        LogInfo("tts", "voice packs: {} installed, {} skipped", ScopedCount, MRegistry.Skipped());
    }
    return ScopedCount > 0;
}

void TtsVoicePack::Shutdown() noexcept
{
    if (MProcess != 0 || MStdin != 0)
    {
        (void)WriteAll("QUIT\n");
        CloseProcess();
    }
    MProcess = 0;
    MStdin = 0;
    MThread = 0;
}

std::uint32_t TtsVoicePack::EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept
{
    if (OutVoices == nullptr || InCapacity == 0)
    {
        return 0;
    }
    const std::uint32_t ScopedCount = std::min(InCapacity, MRegistry.Count());
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        const VoicePackInfo* ScopedPack = MRegistry.At(ScopedIndex);
        if (ScopedPack == nullptr)
        {
            break;
        }
        std::snprintf(OutVoices[ScopedIndex].Id, sizeof(OutVoices[ScopedIndex].Id), "pack:%s", ScopedPack->Id);
        std::snprintf(OutVoices[ScopedIndex].Name, sizeof(OutVoices[ScopedIndex].Name), "%s", ScopedPack->Name);
        std::snprintf(OutVoices[ScopedIndex].Language, sizeof(OutVoices[ScopedIndex].Language), "%s",
                      ScopedPack->Language);
        OutVoices[ScopedIndex].Gender = 0;
    }
    return ScopedCount;
}

bool TtsVoicePack::SetVoiceById(const char* InId) noexcept
{
    if (InId == nullptr)
    {
        return false;
    }
    const std::string_view ScopedId(InId);
    if (!StartsWith(ScopedId, "pack:"))
    {
        return false;
    }
    const VoicePackInfo* ScopedPack = MRegistry.Find(ScopedId.substr(5));
    if (ScopedPack == nullptr)
    {
        return false;
    }
    MSelectedIndex = static_cast<std::uint32_t>(ScopedPack - MRegistry.At(0));
    MHasSelection = true;
    // A different pack needs a different helper, so the running one is retired.
    CloseProcess();
    return true;
}

void TtsVoicePack::SetVoice(const char* InLanguageBCP47) noexcept
{
    if (InLanguageBCP47 == nullptr || InLanguageBCP47[0] == '\0')
    {
        return;
    }
    const std::string_view ScopedPrimary = PrimarySubtag(InLanguageBCP47);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MRegistry.Count(); ++ScopedIndex)
    {
        const VoicePackInfo* ScopedPack = MRegistry.At(ScopedIndex);
        if (ScopedPack != nullptr && EqualsIgnoreCase(PrimarySubtag(ScopedPack->Language), ScopedPrimary))
        {
            MSelectedIndex = ScopedIndex;
            MHasSelection = true;
            CloseProcess();
            return;
        }
    }
}

void TtsVoicePack::SetRate(float InRate) noexcept
{
    MRate = std::clamp(InRate, 0.5f, 2.0f);
    if (MProcess != 0 || MStdin != 0)
    {
        char ScopedLine[64]{};
        std::snprintf(ScopedLine, sizeof(ScopedLine), "RATE %.3f\n", static_cast<double>(MRate));
        (void)WriteAll(ScopedLine);
    }
}

const VoicePackInfo* TtsVoicePack::Selected() const noexcept
{
    if (MRegistry.Count() == 0)
    {
        return nullptr;
    }
    if (MHasSelection)
    {
        return MRegistry.At(MSelectedIndex);
    }
    return MRegistry.At(0);
}

bool TtsVoicePack::WriteAll(std::string_view InBytes) noexcept
{
#if defined(_WIN32)
    if (MStdin == 0)
    {
        return false;
    }
    HANDLE ScopedHandle = reinterpret_cast<HANDLE>(MStdin);
    std::size_t ScopedWritten = 0;
    while (ScopedWritten < InBytes.size())
    {
        DWORD ScopedChunk = 0;
        const DWORD ScopedRequest = static_cast<DWORD>(std::min<std::size_t>(InBytes.size() - ScopedWritten, 1u << 20));
        if (::WriteFile(ScopedHandle, InBytes.data() + ScopedWritten, ScopedRequest, &ScopedChunk, nullptr) == 0 ||
            ScopedChunk == 0)
        {
            return false;
        }
        ScopedWritten += ScopedChunk;
    }
    return true;
#else
    if (MStdin == 0)
    {
        return false;
    }
    std::size_t ScopedWritten = 0;
    while (ScopedWritten < InBytes.size())
    {
        const ssize_t ScopedResult =
            ::write(static_cast<int>(MStdin), InBytes.data() + ScopedWritten, InBytes.size() - ScopedWritten);
        if (ScopedResult < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        ScopedWritten += static_cast<std::size_t>(ScopedResult);
    }
    return true;
#endif
}

bool TtsVoicePack::EnsureProcess() noexcept
{
    if (MProcess != 0 && MStdin != 0 && !MBroken)
    {
        return true;
    }
    CloseProcess();
    const VoicePackInfo* ScopedPack = Selected();
    if (ScopedPack == nullptr)
    {
        return false;
    }
    MBroken = false;

#if defined(_WIN32)
    SECURITY_ATTRIBUTES ScopedAttributes{};
    ScopedAttributes.nLength = sizeof(ScopedAttributes);
    ScopedAttributes.bInheritHandle = TRUE;
    HANDLE ScopedRead = nullptr;
    HANDLE ScopedWrite = nullptr;
    if (::CreatePipe(&ScopedRead, &ScopedWrite, &ScopedAttributes, 0) == 0)
    {
        LogWarn("tts", "voice pack '{}': stdin pipe creation failed", ScopedPack->Id);
        MBroken = true;
        return false;
    }
    (void)::SetHandleInformation(ScopedWrite, HANDLE_FLAG_INHERIT, 0);
    HANDLE ScopedNull = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &ScopedAttributes,
                                      OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW ScopedStartup{};
    ScopedStartup.cb = sizeof(ScopedStartup);
    ScopedStartup.dwFlags = STARTF_USESTDHANDLES;
    ScopedStartup.hStdInput = ScopedRead;
    ScopedStartup.hStdOutput = ScopedNull;
    ScopedStartup.hStdError = ScopedNull;
    std::wstring ScopedCommand =
        Quote(ScopedPack->Helper) + L" --voice " + Quote(ScopedPack->Id) + L" --model " + Quote(ScopedPack->Model);
    PROCESS_INFORMATION ScopedProcess{};
    const BOOL ScopedCreated = ::CreateProcessW(nullptr, ScopedCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                                nullptr, nullptr, &ScopedStartup, &ScopedProcess);
    (void)::CloseHandle(ScopedRead);
    if (ScopedNull != nullptr)
    {
        (void)::CloseHandle(ScopedNull);
    }
    if (ScopedCreated == 0)
    {
        (void)::CloseHandle(ScopedWrite);
        LogWarn("tts", "voice pack '{}': helper '{}' could not be started", ScopedPack->Id, ScopedPack->Helper);
        MBroken = true;
        return false;
    }
    MProcess = reinterpret_cast<std::intptr_t>(ScopedProcess.hProcess);
    MThread = reinterpret_cast<std::intptr_t>(ScopedProcess.hThread);
    MStdin = reinterpret_cast<std::intptr_t>(ScopedWrite);
#else
    int ScopedPipe[2] = {-1, -1};
    if (::pipe(ScopedPipe) != 0)
    {
        LogWarn("tts", "voice pack '{}': stdin pipe creation failed", ScopedPack->Id);
        MBroken = true;
        return false;
    }
    posix_spawn_file_actions_t ScopedActions;
    (void)posix_spawn_file_actions_init(&ScopedActions);
    (void)posix_spawn_file_actions_adddup2(&ScopedActions, ScopedPipe[0], STDIN_FILENO);
    (void)posix_spawn_file_actions_addclose(&ScopedActions, ScopedPipe[0]);
    (void)posix_spawn_file_actions_addclose(&ScopedActions, ScopedPipe[1]);
    (void)posix_spawn_file_actions_addopen(&ScopedActions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    (void)posix_spawn_file_actions_addopen(&ScopedActions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    std::string ScopedHelperText(ScopedPack->Helper);
    std::string ScopedIdText(ScopedPack->Id);
    std::string ScopedModelText(ScopedPack->Model);
    char* ScopedArguments[] = {ScopedHelperText.data(),      const_cast<char*>("--voice"), ScopedIdText.data(),
                               const_cast<char*>("--model"), ScopedModelText.data(),       nullptr};
    pid_t ScopedPid = 0;
    const int ScopedResult =
        ::posix_spawn(&ScopedPid, ScopedPack->Helper, &ScopedActions, nullptr, ScopedArguments, environ);
    (void)posix_spawn_file_actions_destroy(&ScopedActions);
    (void)::close(ScopedPipe[0]);
    if (ScopedResult != 0)
    {
        (void)::close(ScopedPipe[1]);
        LogWarn("tts", "voice pack '{}': helper '{}' could not be started", ScopedPack->Id, ScopedPack->Helper);
        MBroken = true;
        return false;
    }
    MProcess = static_cast<std::intptr_t>(ScopedPid);
    MStdin = ScopedPipe[1];
#endif

    char ScopedLine[128]{};
    std::snprintf(ScopedLine, sizeof(ScopedLine), "VOICE %s\n", ScopedPack->Id);
    if (!WriteAll(ScopedLine))
    {
        CloseProcess();
        MBroken = true;
        return false;
    }
    std::snprintf(ScopedLine, sizeof(ScopedLine), "RATE %.3f\n", static_cast<double>(MRate));
    (void)WriteAll(ScopedLine);
    return true;
}

void TtsVoicePack::CloseProcess() noexcept
{
#if defined(_WIN32)
    if (MStdin != 0)
    {
        (void)::CloseHandle(reinterpret_cast<HANDLE>(MStdin));
    }
    MStdin = 0;
    if (MProcess != 0)
    {
        HANDLE ScopedProcess = reinterpret_cast<HANDLE>(MProcess);
        (void)::WaitForSingleObject(ScopedProcess, 500);
        (void)::TerminateProcess(ScopedProcess, 0);
        (void)::CloseHandle(ScopedProcess);
    }
    MProcess = 0;
    if (MThread != 0)
    {
        (void)::CloseHandle(reinterpret_cast<HANDLE>(MThread));
        MThread = 0;
    }
#else
    if (MStdin != 0)
    {
        (void)::close(static_cast<int>(MStdin));
        MStdin = 0;
    }
    if (MProcess != 0)
    {
        const pid_t ScopedPid = static_cast<pid_t>(MProcess);
        (void)::kill(ScopedPid, SIGTERM);
        int ScopedStatus = 0;
        (void)::waitpid(ScopedPid, &ScopedStatus, 0);
    }
    MProcess = 0;
#endif
}

bool TtsVoicePack::Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept
{
    if (InUtf8.empty() || Selected() == nullptr)
    {
        return false;
    }
    if (!EnsureProcess())
    {
        return false;
    }
    const bool ScopedInterrupt = InPriority == TtsPriority::Interrupt;
    if (ScopedInterrupt)
    {
        (void)WriteAll("STOP\n");
    }
    char ScopedHeader[64]{};
    std::snprintf(ScopedHeader, sizeof(ScopedHeader), "SPEAK %lld\n", static_cast<long long>(InUtf8.size()));
    if (!WriteAll(ScopedHeader) || !WriteAll(InUtf8) || !WriteAll("\n"))
    {
        CloseProcess();
        MBroken = true;
        return false;
    }
    return true;
}

void TtsVoicePack::Stop() noexcept
{
    if (MProcess != 0 || MStdin != 0)
    {
        (void)WriteAll("STOP\n");
    }
}

TtsBackend TtsVoicePack::Backend() noexcept
{
    return MBackend;
}

LayeredTts::~LayeredTts()
{
    Shutdown();
}

bool LayeredTts::Initialize(const TtsBackend* InOs, std::string_view InPackRoot) noexcept
{
    if (MInitialized)
    {
        return MOs != nullptr || MPacks.IsReady();
    }
    MOs = InOs;
    (void)MPacks.Initialize(InPackRoot);
    MPackBackend = MPacks.Backend();
    MSelected = MOs;
    MBackend.user = this;
    MBackend.speak = +[](void* InUser, const char* InUtf8, std::size_t InLength, TtsPriority InPriority) noexcept
    {
        auto* ScopedSelf = static_cast<LayeredTts*>(InUser);
        const TtsBackend* ScopedBackend = ScopedSelf->Selected();
        return ScopedBackend != nullptr && ScopedBackend->speak != nullptr &&
               ScopedBackend->speak(ScopedBackend->user, InUtf8, InLength, InPriority);
    };
    MBackend.stop = +[](void* InUser) noexcept
    {
        auto* ScopedSelf = static_cast<LayeredTts*>(InUser);
        const TtsBackend* ScopedBackend = ScopedSelf->Selected();
        if (ScopedBackend != nullptr && ScopedBackend->stop != nullptr)
        {
            ScopedBackend->stop(ScopedBackend->user);
        }
    };
    MBackend.setRate = +[](void* InUser, float InRate) noexcept
    {
        auto* ScopedSelf = static_cast<LayeredTts*>(InUser);
        ScopedSelf->MLastRate = InRate;
        const TtsBackend* ScopedBackend = ScopedSelf->Selected();
        if (ScopedBackend != nullptr && ScopedBackend->setRate != nullptr)
        {
            ScopedBackend->setRate(ScopedBackend->user, InRate);
        }
    };
    MBackend.setVoice = +[](void* InUser, const char* InLanguage) noexcept
    {
        auto* ScopedSelf = static_cast<LayeredTts*>(InUser);
        if (ScopedSelf->MOs != nullptr && ScopedSelf->MOs->setVoice != nullptr)
        {
            ScopedSelf->MOs->setVoice(ScopedSelf->MOs->user, InLanguage);
            ScopedSelf->UseOs();
            if (ScopedSelf->MOs->setRate != nullptr)
            {
                ScopedSelf->MOs->setRate(ScopedSelf->MOs->user, ScopedSelf->MLastRate);
            }
            return;
        }
        ScopedSelf->MPacks.SetVoice(InLanguage);
        ScopedSelf->MSelected = &ScopedSelf->MPackBackend;
        if (ScopedSelf->MPackBackend.setRate != nullptr)
        {
            ScopedSelf->MPackBackend.setRate(ScopedSelf->MPackBackend.user, ScopedSelf->MLastRate);
        }
    };
    MBackend.setVoiceById = +[](void* InUser, const char* InId) noexcept
    {
        auto* ScopedSelf = static_cast<LayeredTts*>(InUser);
        if (InId != nullptr && StartsWith(std::string_view(InId), "pack:"))
        {
            if (ScopedSelf->MPacks.SetVoiceById(InId))
            {
                ScopedSelf->MSelected = &ScopedSelf->MPackBackend;
                if (ScopedSelf->MPackBackend.setRate != nullptr)
                {
                    ScopedSelf->MPackBackend.setRate(ScopedSelf->MPackBackend.user, ScopedSelf->MLastRate);
                }
                return true;
            }
            return false;
        }
        if (ScopedSelf->MOs != nullptr && ScopedSelf->MOs->setVoiceById != nullptr &&
            ScopedSelf->MOs->setVoiceById(ScopedSelf->MOs->user, InId))
        {
            ScopedSelf->UseOs();
            if (ScopedSelf->MOs->setRate != nullptr)
            {
                ScopedSelf->MOs->setRate(ScopedSelf->MOs->user, ScopedSelf->MLastRate);
            }
            return true;
        }
        return false;
    };
    MBackend.enumerateVoices = +[](void* InUser, TtsVoiceInfo* InVoices, std::uint32_t InCapacity) noexcept
    {
        auto* ScopedSelf = static_cast<LayeredTts*>(InUser);
        if (InVoices == nullptr || InCapacity == 0)
        {
            return 0u;
        }
        std::uint32_t ScopedWritten = 0;
        if (ScopedSelf->MOs != nullptr && ScopedSelf->MOs->enumerateVoices != nullptr)
        {
            ScopedWritten = ScopedSelf->MOs->enumerateVoices(ScopedSelf->MOs->user, InVoices, InCapacity);
        }
        if (ScopedWritten < InCapacity)
        {
            ScopedWritten += ScopedSelf->MPacks.EnumerateVoices(InVoices + ScopedWritten, InCapacity - ScopedWritten);
        }
        return ScopedWritten;
    };
    MInitialized = true;
    return MOs != nullptr || MPacks.IsReady();
}

void LayeredTts::Shutdown() noexcept
{
    MPacks.Shutdown();
    MInitialized = false;
    MSelected = nullptr;
    MOs = nullptr;
}

const TtsBackend* LayeredTts::Selected() const noexcept
{
    return MSelected;
}

const TtsBackend* LayeredTts::Backend() noexcept
{
    return MInitialized ? &MBackend : nullptr;
}
}
