// FemboyEngine2D - crash diagnostics implementation.
#include "Engine/Core/CrashHandler.hpp"

#include "Engine/Core/BuildInfo.hpp"
#include "Engine/Core/Calendar.hpp"
#include "Engine/Core/StoragePaths.hpp"
#include "Engine/Core/Time.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Fby::Core
{
namespace
{
std::atomic<bool> GInstalled{false};
std::atomic<bool> GInHandler{false};
std::atomic<unsigned int> GReportSequence{1};
std::terminate_handler GPreviousTerminate = nullptr;
char GApplicationName[64] = "femboy";
char GSessionDirectory[1024] = {};

[[nodiscard]] std::uint64_t ProcessId() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

// Formats the wall clock into a fixed buffer. No allocation, so a signal
// handler can use it.
void FormatTimestamp(char* OutBuffer, std::size_t InSize, bool InFileName) noexcept
{
    const DateTime ScopedNow = DateTimeFromUnixSeconds(Clock::UnixSeconds(), LocalOffsetMinutes());
    if (InFileName)
    {
        std::snprintf(OutBuffer, InSize, "%04d%02d%02d-%02d%02d%02d", static_cast<int>(ScopedNow.Date.Year),
                      static_cast<int>(ScopedNow.Date.Month), static_cast<int>(ScopedNow.Date.Day),
                      static_cast<int>(ScopedNow.Time.Hour), static_cast<int>(ScopedNow.Time.Minute),
                      static_cast<int>(ScopedNow.Time.Second));
        return;
    }
    std::snprintf(OutBuffer, InSize, "%04d-%02d-%02d %02d:%02d:%02d.%03u", static_cast<int>(ScopedNow.Date.Year),
                  static_cast<int>(ScopedNow.Date.Month), static_cast<int>(ScopedNow.Date.Day),
                  static_cast<int>(ScopedNow.Time.Hour), static_cast<int>(ScopedNow.Time.Minute),
                  static_cast<int>(ScopedNow.Time.Second), static_cast<unsigned>(ScopedNow.Time.Millisecond));
}

// Creates the per-process crash directory once, so a handler only has to open a
// file inside it.
void CreateSessionDirectory() noexcept
{
    char ScopedStamp[32] = {};
    FormatTimestamp(ScopedStamp, sizeof(ScopedStamp), true);
    const std::string ScopedDirectory =
        (CrashRoot() / (std::string(ScopedStamp) + "-" + std::to_string(ProcessId()))).string();
    if (!EnsureDirectory(ScopedDirectory))
    {
        return;
    }
    std::snprintf(GSessionDirectory, sizeof(GSessionDirectory), "%s", ScopedDirectory.c_str());
}

void AppendText(char* InBuffer, std::size_t InCapacity, std::size_t& InOffset, std::string_view InText) noexcept
{
    const std::size_t ScopedRoom = InOffset < InCapacity ? InCapacity - InOffset : 0;
    const std::size_t ScopedCopy = InText.size() < ScopedRoom ? InText.size() : ScopedRoom;
    if (ScopedCopy > 0)
    {
        std::memcpy(InBuffer + InOffset, InText.data(), ScopedCopy);
        InOffset += ScopedCopy;
    }
}

void AppendReportLine(char* InBuffer, std::size_t InCapacity, std::size_t& InOffset, std::string_view InLabel,
                      std::string_view InValue) noexcept
{
    AppendText(InBuffer, InCapacity, InOffset, "\n");
    AppendText(InBuffer, InCapacity, InOffset, InLabel);
    AppendText(InBuffer, InCapacity, InOffset, ": ");
    AppendText(InBuffer, InCapacity, InOffset, InValue);
}

void BuildReport(char* OutBuffer, std::size_t InCapacity, std::string_view InReason, std::string_view InDetails,
                  unsigned int InSequence = 0) noexcept
{
    std::size_t ScopedOffset = 0;
    char ScopedTimestamp[40] = {};
    FormatTimestamp(ScopedTimestamp, sizeof(ScopedTimestamp), false);
    char ScopedPid[32] = {};
    std::snprintf(ScopedPid, sizeof(ScopedPid), "%llu", static_cast<unsigned long long>(ProcessId()));
    AppendText(OutBuffer, InCapacity, ScopedOffset, "FemboyEngine crash report\n");
    AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "time", ScopedTimestamp);
    AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "application", GApplicationName);
    AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "pid", ScopedPid);
    const BuildInfo& ScopedBuild = GetBuildInfo();
    AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "compiler", ScopedBuild.compiler);
    AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "build", ScopedBuild.buildType);
    AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "flags", ScopedBuild.flags);
    AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "deps", ScopedBuild.dependencies);
    AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "reason", InReason);
    if (!InDetails.empty())
    {
        AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "details", InDetails);
    }
    if (InSequence != 0)
    {
        char ScopedSequence[32] = {};
        std::snprintf(ScopedSequence, sizeof(ScopedSequence), "%u", InSequence);
        AppendReportLine(OutBuffer, InCapacity, ScopedOffset, "report", ScopedSequence);
    }
    if (ScopedOffset < InCapacity)
    {
        OutBuffer[ScopedOffset] = '\0';
    }
}

void WriteReportFile(std::string_view InReason, std::string_view InDetails,
                      unsigned int InSequence = 0) noexcept
{
    if (GSessionDirectory[0] == '\0')
    {
        return;
    }
    char ScopedPath[1200] = {};
    std::snprintf(ScopedPath, sizeof(ScopedPath), "%s/report.txt", GSessionDirectory);
    char ScopedReport[4096] = {};
    BuildReport(ScopedReport, sizeof(ScopedReport), InReason, InDetails, InSequence);
    const std::size_t ScopedLength = std::strlen(ScopedReport);
#if defined(_WIN32)
    HANDLE ScopedHandle = ::CreateFileA(ScopedPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (ScopedHandle == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD ScopedWritten = 0;
    ::WriteFile(ScopedHandle, ScopedReport, static_cast<DWORD>(ScopedLength), &ScopedWritten, nullptr);
    ::FlushFileBuffers(ScopedHandle);
    ::CloseHandle(ScopedHandle);
#else
    const int ScopedHandle = ::open(ScopedPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (ScopedHandle < 0)
    {
        return;
    }
    std::size_t ScopedOffset = 0;
    while (ScopedOffset < ScopedLength)
    {
        const ssize_t ScopedWritten = ::write(ScopedHandle, ScopedReport + ScopedOffset, ScopedLength - ScopedOffset);
        if (ScopedWritten <= 0)
        {
            break;
        }
        ScopedOffset += static_cast<std::size_t>(ScopedWritten);
    }
    ::fsync(ScopedHandle);
    ::close(ScopedHandle);
#endif
}

void HandleTerminate() noexcept
{
    if (!GInHandler.exchange(true))
    {
        WriteReportFile("std::terminate", "an exception escaped or a noexcept boundary was violated");
    }
    std::abort();
}

#if defined(_WIN32)
void WriteMiniDump(EXCEPTION_POINTERS* InExceptionInfo) noexcept
{
    if (GSessionDirectory[0] == '\0')
    {
        return;
    }
    char ScopedPath[1200] = {};
    std::snprintf(ScopedPath, sizeof(ScopedPath), "%s/crash.dmp", GSessionDirectory);
    const HANDLE ScopedFile = ::CreateFileA(ScopedPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (ScopedFile == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const HMODULE ScopedModule = ::LoadLibraryA("dbghelp.dll");
    if (ScopedModule == nullptr)
    {
        ::CloseHandle(ScopedFile);
        return;
    }
    using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                              PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                              PMINIDUMP_CALLBACK_INFORMATION);
    const auto ScopedWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(
        ::GetProcAddress(ScopedModule, "MiniDumpWriteDump"));
    if (ScopedWriteDump != nullptr)
    {
        MINIDUMP_EXCEPTION_INFORMATION ScopedInfo{};
        ScopedInfo.ThreadId = ::GetCurrentThreadId();
        ScopedInfo.ExceptionPointers = InExceptionInfo;
        ScopedInfo.ClientPointers = FALSE;
        (void)ScopedWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), ScopedFile,
                              static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithDataSegs), &ScopedInfo, nullptr,
                              nullptr);
    }
    ::CloseHandle(ScopedFile);
}

LONG WINAPI UnhandledException(EXCEPTION_POINTERS* InExceptionInfo) noexcept
{
    char ScopedDetails[160] = {};
    if (InExceptionInfo != nullptr && InExceptionInfo->ExceptionRecord != nullptr)
    {
        std::snprintf(ScopedDetails, sizeof(ScopedDetails), "exception 0x%08lX at %p",
                      static_cast<unsigned long>(InExceptionInfo->ExceptionRecord->ExceptionCode),
                      InExceptionInfo->ExceptionRecord->ExceptionAddress);
    }
    // The filter must not run twice for the same fault.
    (void)GInHandler.exchange(true);
    WriteReportFile("unhandled exception", ScopedDetails);
    WriteMiniDump(InExceptionInfo);
    ::TerminateProcess(::GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
void HandleFatalSignal(int InSignal, siginfo_t* InInfo, void*) noexcept
{
    if (!GInHandler.exchange(true))
    {
        char ScopedDetails[192] = {};
        std::snprintf(ScopedDetails, sizeof(ScopedDetails), "signal %d at address %p", InSignal,
                      InInfo != nullptr ? InInfo->si_addr : nullptr);
        WriteReportFile("fatal signal", ScopedDetails);
        if (GSessionDirectory[0] != '\0')
        {
            char ScopedPath[1200] = {};
            std::snprintf(ScopedPath, sizeof(ScopedPath), "%s/report.txt", GSessionDirectory);
            const int ScopedHandle = ::open(ScopedPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (ScopedHandle >= 0)
            {
                void* ScopedFrames[32] = {};
                const int ScopedCount = ::backtrace(ScopedFrames, 32);
                ::backtrace_symbols_fd(ScopedFrames, ScopedCount, ScopedHandle);
                ::close(ScopedHandle);
            }
        }
    }
    std::signal(InSignal, SIG_DFL);
    std::raise(InSignal);
}
#endif
}

void InstallCrashHandler(std::string_view InApplicationName) noexcept
{
    if (GInstalled.load(std::memory_order_relaxed))
    {
        return;
    }
    std::snprintf(GApplicationName, sizeof(GApplicationName), "%.*s", static_cast<int>(InApplicationName.size()),
                  InApplicationName.data());
    CreateSessionDirectory();
    GPreviousTerminate = std::set_terminate(&HandleTerminate);
#if defined(_WIN32)
    ::SetUnhandledExceptionFilter(&UnhandledException);
#else
    struct sigaction ScopedAction
    {
    };
    std::memset(&ScopedAction, 0, sizeof ScopedAction);
    ScopedAction.sa_sigaction = &HandleFatalSignal;
    ScopedAction.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&ScopedAction.sa_mask);
    for (const int ScopedSignal : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
    {
        ::sigaction(ScopedSignal, &ScopedAction, nullptr);
    }
#endif
    GInstalled.store(true, std::memory_order_release);
}

void UninstallCrashHandler() noexcept
{
    if (!GInstalled.load(std::memory_order_relaxed))
    {
        return;
    }
#if defined(_WIN32)
    ::SetUnhandledExceptionFilter(nullptr);
#else
    for (const int ScopedSignal : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
    {
        std::signal(ScopedSignal, SIG_DFL);
    }
#endif
    std::set_terminate(GPreviousTerminate);
    GInstalled.store(false, std::memory_order_release);
}

bool CrashHandlerInstalled() noexcept
{
    return GInstalled.load(std::memory_order_relaxed);
}

bool WriteCrashReport(std::string_view InApplicationName, std::string_view InReason,
                      std::string_view InDetails) noexcept
{
    std::snprintf(GApplicationName, sizeof(GApplicationName), "%.*s", static_cast<int>(InApplicationName.size()),
                  InApplicationName.data());
    if (GSessionDirectory[0] == '\0')
    {
        CreateSessionDirectory();
    }
    if (GSessionDirectory[0] == '\0')
    {
        return false;
    }
    const unsigned int ScopedSequence = GReportSequence.fetch_add(1, std::memory_order_relaxed);
    WriteReportFile(InReason, InDetails, ScopedSequence);
    return true;
}

std::string_view LastCrashDirectory() noexcept
{
    return std::string_view(GSessionDirectory);
}
}
