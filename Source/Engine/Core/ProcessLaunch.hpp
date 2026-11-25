// FemboyEngine2D - shell-free child process launch.
#pragma once

#include <cerrno>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
extern char** environ;
#endif

namespace Fby::Process
{

// Empty arguments and embedded NUL/CR/LF are invalid. The launcher passes every
// argument as one array element; shell metacharacters have no special meaning.
[[nodiscard]] inline bool IsSafeArgument(std::string_view InArgument) noexcept
{
    if (InArgument.empty())
    {
        return false;
    }
    for (const char ScopedCharacter : InArgument)
    {
        if (ScopedCharacter == '\0' || ScopedCharacter == '\r' || ScopedCharacter == '\n')
        {
            return false;
        }
    }
    return true;
}

// Microsoft CRT-compatible quoting for one argument.
[[nodiscard]] inline std::wstring QuoteWindowsArgument(std::wstring_view InArgument)
{
    const bool ScopedNeedsQuotes =
        InArgument.empty() || InArgument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
    if (!ScopedNeedsQuotes)
    {
        return std::wstring(InArgument);
    }
    std::wstring ScopedQuoted;
    ScopedQuoted.reserve(InArgument.size() + 2);
    ScopedQuoted.push_back(L'"');
    std::size_t ScopedBackslashes = 0;
    for (const wchar_t ScopedCharacter : InArgument)
    {
        if (ScopedCharacter == L'\\')
        {
            ++ScopedBackslashes;
            continue;
        }
        if (ScopedCharacter == L'"')
        {
            ScopedQuoted.append(ScopedBackslashes * 2 + 1, L'\\');
            ScopedQuoted.push_back(L'"');
            ScopedBackslashes = 0;
            continue;
        }
        ScopedQuoted.append(ScopedBackslashes, L'\\');
        ScopedBackslashes = 0;
        ScopedQuoted.push_back(ScopedCharacter);
    }
    ScopedQuoted.append(ScopedBackslashes * 2, L'\\');
    ScopedQuoted.push_back(L'"');
    return ScopedQuoted;
}

struct RunResult
{
    bool launched = false;
    int exitCode = -1;
};

#if defined(_WIN32)
[[nodiscard]] inline std::wstring ToWide(std::string_view InText)
{
    if (InText.empty())
    {
        return {};
    }
    const int ScopedLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, InText.data(),
                                                 static_cast<int>(InText.size()), nullptr, 0);
    if (ScopedLength <= 0)
    {
        return {};
    }
    std::wstring ScopedWide(static_cast<std::size_t>(ScopedLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, InText.data(), static_cast<int>(InText.size()),
                            ScopedWide.data(), ScopedLength) != ScopedLength)
    {
        return {};
    }
    return ScopedWide;
}
#endif

// Launches `executable` with `arguments` (element zero must be argv[0]) and
// waits for it. A non-empty logPath redirects stdout/stderr into that file.
// Returns launched=false only when the process could not be created.
[[nodiscard]] inline RunResult Run(std::string_view InExecutable, std::span<const std::string> InArguments,
                                   std::string_view InLogPath = {})
{
    if (InExecutable.empty() || InArguments.empty() || !IsSafeArgument(InExecutable) ||
        (!InLogPath.empty() && !IsSafeArgument(InLogPath)))
    {
        return {};
    }
    for (const std::string& ScopedArgument : InArguments)
    {
        if (!IsSafeArgument(ScopedArgument))
        {
            return {};
        }
    }
#if defined(_WIN32)
    std::wstring ScopedCommandLine;
    for (std::size_t ScopedIndex = 0; ScopedIndex < InArguments.size(); ++ScopedIndex)
    {
        if (ScopedIndex != 0)
        {
            ScopedCommandLine.push_back(L' ');
        }
        const std::wstring ScopedWide = ToWide(InArguments[ScopedIndex]);
        if (ScopedWide.empty())
        {
            return {};
        }
        ScopedCommandLine += QuoteWindowsArgument(ScopedWide);
    }
    const std::wstring ScopedWideExecutable = ToWide(InExecutable);
    if (ScopedWideExecutable.empty())
    {
        return {};
    }
    HANDLE ScopedLog = INVALID_HANDLE_VALUE;
    if (!InLogPath.empty())
    {
        const std::wstring ScopedWideLog = ToWide(InLogPath);
        if (ScopedWideLog.empty())
        {
            return {};
        }
        SECURITY_ATTRIBUTES ScopedAttributes{};
        ScopedAttributes.nLength = sizeof(ScopedAttributes);
        ScopedAttributes.bInheritHandle = TRUE;
        ScopedLog = CreateFileW(ScopedWideLog.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &ScopedAttributes,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (ScopedLog == INVALID_HANDLE_VALUE)
        {
            return {};
        }
    }
    STARTUPINFOW ScopedStartup{};
    ScopedStartup.cb = sizeof(ScopedStartup);
    if (ScopedLog != INVALID_HANDLE_VALUE)
    {
        ScopedStartup.dwFlags = STARTF_USESTDHANDLES;
        ScopedStartup.hStdOutput = ScopedLog;
        ScopedStartup.hStdError = ScopedLog;
        ScopedStartup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION ScopedProcess{};
    const wchar_t* ScopedApplication = InExecutable.find('/') != std::string_view::npos ||
                                              InExecutable.find('\\') != std::string_view::npos
                                          ? ScopedWideExecutable.c_str()
                                          : nullptr;
    const BOOL ScopedCreated =
        CreateProcessW(ScopedApplication, ScopedCommandLine.data(), nullptr, nullptr,
                       ScopedLog != INVALID_HANDLE_VALUE ? TRUE : FALSE, 0, nullptr, nullptr, &ScopedStartup,
                       &ScopedProcess);
    if (ScopedLog != INVALID_HANDLE_VALUE)
    {
        CloseHandle(ScopedLog);
    }
    if (!ScopedCreated)
    {
        return {};
    }
    const DWORD ScopedWait = WaitForSingleObject(ScopedProcess.hProcess, INFINITE);
    DWORD ScopedExit = 1;
    if (ScopedWait != WAIT_OBJECT_0 || GetExitCodeProcess(ScopedProcess.hProcess, &ScopedExit) == FALSE)
    {
        ScopedExit = 1;
    }
    CloseHandle(ScopedProcess.hThread);
    CloseHandle(ScopedProcess.hProcess);
    return RunResult{true, static_cast<int>(ScopedExit)};
#else
    std::vector<char*> ScopedArgv;
    ScopedArgv.reserve(InArguments.size() + 1);
    for (const std::string& ScopedArgument : InArguments)
    {
        ScopedArgv.push_back(const_cast<char*>(ScopedArgument.c_str()));
    }
    ScopedArgv.push_back(nullptr);
    posix_spawn_file_actions_t ScopedActions;
    posix_spawn_file_actions_t* ScopedActionsPointer = nullptr;
    if (!InLogPath.empty())
    {
        if (posix_spawn_file_actions_init(&ScopedActions) != 0)
        {
            return {};
        }
        ScopedActionsPointer = &ScopedActions;
        const std::string ScopedLog(InLogPath);
        // Open the log once and duplicate stdout onto stderr. Opening the same
        // path twice with O_TRUNC gives the two streams separate offsets and
        // lets later writes overwrite earlier ones.
        if (posix_spawn_file_actions_addopen(&ScopedActions, STDOUT_FILENO, ScopedLog.c_str(),
                                             O_WRONLY | O_CREAT | O_TRUNC, 0666) != 0 ||
            posix_spawn_file_actions_adddup2(&ScopedActions, STDOUT_FILENO, STDERR_FILENO) != 0)
        {
            (void)posix_spawn_file_actions_destroy(&ScopedActions);
            return {};
        }
    }
    pid_t ScopedPid = 0;
    const std::string ScopedExecutable(InExecutable);
    const int ScopedSpawn =
        posix_spawnp(&ScopedPid, ScopedExecutable.c_str(), ScopedActionsPointer, nullptr, ScopedArgv.data(),
                     environ);
    if (ScopedActionsPointer != nullptr)
    {
        (void)posix_spawn_file_actions_destroy(&ScopedActions);
    }
    if (ScopedSpawn != 0)
    {
        return {};
    }
    int ScopedStatus = 0;
    pid_t ScopedWaited = 0;
    do
    {
        ScopedWaited = waitpid(ScopedPid, &ScopedStatus, 0);
    } while (ScopedWaited < 0 && errno == EINTR);
    if (ScopedWaited != ScopedPid)
    {
        return {};
    }
    if (WIFEXITED(ScopedStatus))
    {
        return RunResult{true, WEXITSTATUS(ScopedStatus)};
    }
    if (WIFSIGNALED(ScopedStatus))
    {
        return RunResult{true, 128 + WTERMSIG(ScopedStatus)};
    }
    return {};
#endif
}
}
