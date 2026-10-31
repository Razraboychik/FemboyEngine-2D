// FemboyEngine2D - background AOT cook service implementation.
#include "Engine/Script/AotBuilder.hpp"

#include "Engine/Script/AotGraphCompiler.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Fby::Script
{
namespace
{

[[nodiscard]] std::string Quote(const std::string& InValue)
{
    std::string ScopedQuoted = "\"";
    for (const char ScopedCharacter : InValue)
    {
        if (ScopedCharacter == '"')
        {
            ScopedQuoted += "\\\"";
        }
        else
        {
            ScopedQuoted += ScopedCharacter;
        }
    }
    ScopedQuoted += '"';
    return ScopedQuoted;
}

[[nodiscard]] bool IsMsvcDriver(const std::string& InCompiler)
{
    return InCompiler.find("clang-cl") != std::string::npos || InCompiler.find("cl.exe") != std::string::npos ||
           InCompiler == "cl";
}

[[nodiscard]] std::string ReadLogTail(const std::filesystem::path& InPath, std::size_t InLimit = 2048)
{
    std::ifstream ScopedStream(InPath, std::ios::binary | std::ios::ate);
    if (!ScopedStream)
    {
        return {};
    }
    const std::streamsize ScopedSize = ScopedStream.tellg();
    if (ScopedSize <= 0)
    {
        return {};
    }
    const std::size_t ScopedWanted = std::min<std::size_t>(static_cast<std::size_t>(ScopedSize), InLimit);
    std::string ScopedText(ScopedWanted, '\0');
    ScopedStream.seekg(static_cast<std::streamoff>(ScopedSize - static_cast<std::streamsize>(ScopedWanted)),
                       std::ios::beg);
    ScopedStream.read(ScopedText.data(), static_cast<std::streamsize>(ScopedWanted));
    return ScopedText;
}

[[nodiscard]] const char* LibrarySuffix() noexcept
{
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

#if defined(_WIN32)
[[nodiscard]] std::wstring Wide(const std::string& InText)
{
    const int ScopedLength =
        MultiByteToWideChar(CP_UTF8, 0, InText.c_str(), static_cast<int>(InText.size()), nullptr, 0);
    if (ScopedLength <= 0)
    {
        return {};
    }
    std::wstring ScopedWide(static_cast<std::size_t>(ScopedLength), L'\0');
    (void)MultiByteToWideChar(CP_UTF8, 0, InText.c_str(), static_cast<int>(InText.size()), ScopedWide.data(),
                              ScopedLength);
    return ScopedWide;
}
#endif

// Launches the compiler without a shell on Windows. std::system() goes through
#if defined(_WIN32)
[[nodiscard]] std::wstring ReadEnvironment(const wchar_t* InName)
{
    const DWORD ScopedSize = GetEnvironmentVariableW(InName, nullptr, 0);
    if (ScopedSize == 0)
    {
        return {};
    }
    std::wstring ScopedValue(static_cast<std::size_t>(ScopedSize), L'\0');
    const DWORD ScopedCopied = GetEnvironmentVariableW(InName, ScopedValue.data(), ScopedSize);
    ScopedValue.resize(static_cast<std::size_t>(ScopedCopied));
    return ScopedValue;
}
#endif

[[nodiscard]] int RunProcess(const std::string& InCommand, const std::filesystem::path& InLogPath)
{
#if defined(_WIN32)
    std::wstring ScopedWideCommand = Wide(InCommand);
    if (ScopedWideCommand.empty())
    {
        return -1;
    }

    // A process started from a POSIX shell on Windows can inherit TMP=/tmp,
    static std::mutex ScopedEnvironmentMutex;
    const std::filesystem::path ScopedTempDirectory = InLogPath.parent_path() / ".tmp";
    std::error_code ScopedDirectoryError;
    std::filesystem::create_directories(ScopedTempDirectory, ScopedDirectoryError);
    const std::wstring ScopedTemp = (ScopedDirectoryError ? InLogPath.parent_path() : ScopedTempDirectory).wstring();

    std::lock_guard ScopedLock(ScopedEnvironmentMutex);
    const std::wstring ScopedPreviousTmp = ReadEnvironment(L"TMP");
    const std::wstring ScopedPreviousTemp = ReadEnvironment(L"TEMP");
    const bool ScopedHadTmp = !ScopedPreviousTmp.empty();
    const bool ScopedHadTemp = !ScopedPreviousTemp.empty();
    (void)SetEnvironmentVariableW(L"TMP", ScopedTemp.c_str());
    (void)SetEnvironmentVariableW(L"TEMP", ScopedTemp.c_str());

    SECURITY_ATTRIBUTES ScopedAttributes{};
    ScopedAttributes.nLength = sizeof(ScopedAttributes);
    ScopedAttributes.bInheritHandle = TRUE;
    const HANDLE ScopedLog = CreateFileW(InLogPath.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &ScopedAttributes,
                                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW ScopedStartup{};
    ScopedStartup.cb = sizeof(ScopedStartup);
    if (ScopedLog != INVALID_HANDLE_VALUE)
    {
        ScopedStartup.dwFlags |= STARTF_USESTDHANDLES;
        ScopedStartup.hStdOutput = ScopedLog;
        ScopedStartup.hStdError = ScopedLog;
        ScopedStartup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION ScopedProcess{};
    const BOOL ScopedCreated =
        CreateProcessW(nullptr, ScopedWideCommand.data(), nullptr, nullptr, ScopedLog != INVALID_HANDLE_VALUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &ScopedStartup, &ScopedProcess);
    if (ScopedLog != INVALID_HANDLE_VALUE)
    {
        CloseHandle(ScopedLog);
    }
    (void)SetEnvironmentVariableW(L"TMP", ScopedHadTmp ? ScopedPreviousTmp.c_str() : nullptr);
    (void)SetEnvironmentVariableW(L"TEMP", ScopedHadTemp ? ScopedPreviousTemp.c_str() : nullptr);
    if (!ScopedCreated)
    {
        return -1;
    }
    WaitForSingleObject(ScopedProcess.hProcess, INFINITE);
    DWORD ScopedExitCode = 1;
    (void)GetExitCodeProcess(ScopedProcess.hProcess, &ScopedExitCode);
    CloseHandle(ScopedProcess.hThread);
    CloseHandle(ScopedProcess.hProcess);
    return static_cast<int>(ScopedExitCode);
#else
    const std::string redirected = command + " > " + Quote(logPath.string()) + " 2>&1";
    return std::system(redirected.c_str());
#endif
}

[[nodiscard]] std::string BuildCommand(const AotBuildOptions& InOptions, const std::filesystem::path& InSource,
                                       const std::filesystem::path& InLibrary)
{
    const std::string ScopedCompiler = InOptions.compiler;
    std::string ScopedCommand = Quote(ScopedCompiler);
    if (IsMsvcDriver(ScopedCompiler))
    {
        ScopedCommand += " /nologo /std:c++latest /O2 /LD /EHsc /I";
        ScopedCommand += Quote(InOptions.includeDirectory.string());
        ScopedCommand += ' ';
        if (!InOptions.forceIncludeHeader.empty())
        {
            ScopedCommand += "/FI";
            ScopedCommand += Quote(InOptions.forceIncludeHeader.string());
            ScopedCommand += ' ';
        }
        ScopedCommand += Quote(InSource.string());
        ScopedCommand += " /Fe:";
        ScopedCommand += Quote(InLibrary.string());
        ScopedCommand += " /Fo:";
        ScopedCommand += Quote((InLibrary.string() + ".obj"));
    }
    else
    {
        ScopedCommand += " -std=c++26 -O3 -shared";
#if !defined(_WIN32)
        command += " -fPIC";
#endif
        ScopedCommand += " -I";
        ScopedCommand += Quote(InOptions.includeDirectory.string());
        ScopedCommand += ' ';
        if (!InOptions.forceIncludeHeader.empty())
        {
            ScopedCommand += "-include ";
            ScopedCommand += Quote(InOptions.forceIncludeHeader.string());
            ScopedCommand += ' ';
        }
        ScopedCommand += Quote(InSource.string());
        ScopedCommand += " -o ";
        ScopedCommand += Quote(InLibrary.string());
    }
    if (!InOptions.extraFlags.empty())
    {
        ScopedCommand += ' ';
        ScopedCommand += InOptions.extraFlags;
    }
    return ScopedCommand;
}
}

AotBuilder::~AotBuilder()
{
    Cancel();
}

bool AotBuilder::Start(const LogicGraph& InGraph, const AotBuildOptions& InOptions, std::string* InError)
{
    if (Busy())
    {
        if (InError != nullptr)
        {
            *InError = "an AOT build is already running";
        }
        return false;
    }
    if (InGraph.nodeCount == 0 || InGraph.nodeCount > kMaxLogicNodes || InGraph.linkCount > kMaxLogicLinks ||
        InGraph.fieldCount > kMaxLogicFields)
    {
        if (InError != nullptr)
        {
            *InError = "LogicGraph is empty or exceeds its fixed capacity";
        }
        return false;
    }
    if (InOptions.outputDirectory.empty() || InOptions.includeDirectory.empty() || InOptions.compiler.empty())
    {
        if (InError != nullptr)
        {
            *InError = "AotBuildOptions needs outputDirectory, includeDirectory and compiler";
        }
        return false;
    }

    // Validate the graph before a thread is even created: a broken graph should
    AotGraphTranspileOptions ScopedTranspileOptions;
    ScopedTranspileOptions.compile.graphHash = HashLogicGraph(InGraph);
    ScopedTranspileOptions.transpile.programName = InOptions.moduleName;
    std::string ScopedSource;
    LogicCompileResult ScopedCompiled;
    std::string ScopedCompileError;
    if (!TranspileLogicGraphToCpp(InGraph, ScopedTranspileOptions, &ScopedSource, &ScopedCompiled, &ScopedCompileError))
    {
        if (InError != nullptr)
        {
            *InError = ScopedCompileError;
        }
        return false;
    }

    std::error_code ScopedCode;
    std::filesystem::create_directories(InOptions.outputDirectory, ScopedCode);
    if (ScopedCode)
    {
        if (InError != nullptr)
        {
            *InError = "cannot create AOT output directory: " + ScopedCode.message();
        }
        return false;
    }

    if (MWorker.joinable())
    {
        MWorker.join();
    }

    AotBuildResult ScopedInitial;
    ScopedInitial.state = AotBuildState::Compiling;
    ScopedInitial.graphHash = ScopedTranspileOptions.compile.graphHash;
    ScopedInitial.instructions = ScopedCompiled.instructionCount;
    {
        std::lock_guard ScopedLock(MMutex);
        MResult = ScopedInitial;
    }
    MState.store(AotBuildState::Compiling, std::memory_order_release);
    MRevision.fetch_add(1, std::memory_order_release);

    MWorker = std::jthread([this](std::stop_token InStop, LogicGraph InWorkerGraph, AotBuildOptions InWorkerOptions)
                           { Run(InStop, std::move(InWorkerGraph), std::move(InWorkerOptions)); }, InGraph, InOptions);
    if (InError != nullptr)
    {
        InError->clear();
    }
    return true;
}

void AotBuilder::Run(std::stop_token InStop, LogicGraph InGraph, AotBuildOptions InOptions)
{
    AotBuildResult ScopedResult;
    ScopedResult.state = AotBuildState::Compiling;
    ScopedResult.graphHash = HashLogicGraph(InGraph);
    const std::string ScopedSuffix = LibrarySuffix();
    ScopedResult.sourcePath = InOptions.outputDirectory / (InOptions.moduleName + ".cpp");
    ScopedResult.libraryPath = InOptions.outputDirectory / (InOptions.moduleName + ScopedSuffix);
    ScopedResult.logPath = InOptions.outputDirectory / (InOptions.moduleName + ".log");

    AotGraphTranspileOptions ScopedTranspileOptions;
    ScopedTranspileOptions.compile.graphHash = ScopedResult.graphHash;
    ScopedTranspileOptions.transpile.programName = InOptions.moduleName;
    std::string ScopedSource;
    LogicCompileResult ScopedCompiled;
    std::string ScopedError;
    if (!TranspileLogicGraphToCpp(InGraph, ScopedTranspileOptions, &ScopedSource, &ScopedCompiled, &ScopedError))
    {
        ScopedResult.state = AotBuildState::Failed;
        ScopedResult.error = ScopedError;
        Publish(ScopedResult);
        return;
    }
    ScopedResult.instructions = ScopedCompiled.instructionCount;
    FinishBuild(InStop, ScopedResult, ScopedSource, InOptions);
}

void AotBuilder::FinishBuild(std::stop_token InStop, AotBuildResult& ScopedResult, const std::string& ScopedSource,
                             const AotBuildOptions& InOptions)
{
    if (InStop.stop_requested())
    {
        ScopedResult.state = AotBuildState::Idle;
        ScopedResult.error = "cancelled before the compiler started";
        Publish(ScopedResult);
        return;
    }

    {
        std::ofstream ScopedStream(ScopedResult.sourcePath, std::ios::binary | std::ios::trunc);
        if (!ScopedStream)
        {
            ScopedResult.state = AotBuildState::Failed;
            ScopedResult.error = "cannot write generated C++: " + ScopedResult.sourcePath.string();
            Publish(ScopedResult);
            return;
        }
        ScopedStream.write(ScopedSource.data(), static_cast<std::streamsize>(ScopedSource.size()));
        if (!ScopedStream)
        {
            ScopedResult.state = AotBuildState::Failed;
            ScopedResult.error = "failed while writing generated C++";
            Publish(ScopedResult);
            return;
        }
    }

    if (InStop.stop_requested())
    {
        ScopedResult.state = AotBuildState::Idle;
        ScopedResult.error = "cancelled before the compiler started";
        Publish(ScopedResult);
        return;
    }

    const std::string ScopedCommand = BuildCommand(InOptions, ScopedResult.sourcePath, ScopedResult.libraryPath);
    const int ScopedStatus = RunProcess(ScopedCommand, ScopedResult.logPath);
    if (InStop.stop_requested())
    {
        ScopedResult.state = AotBuildState::Idle;
        ScopedResult.error = "cancelled";
        Publish(ScopedResult);
        return;
    }
    if (ScopedStatus != 0)
    {
        ScopedResult.state = AotBuildState::Failed;
        ScopedResult.error = ScopedStatus < 0 ? "could not launch the native compiler: " + InOptions.compiler
                                              : "native compiler failed with exit code " + std::to_string(ScopedStatus);
        const std::string ScopedLog = ReadLogTail(ScopedResult.logPath);
        if (!ScopedLog.empty())
        {
            ScopedResult.error += "\n";
            ScopedResult.error += ScopedLog;
        }
        Publish(ScopedResult);
        return;
    }
    if (!std::filesystem::exists(ScopedResult.libraryPath))
    {
        ScopedResult.state = AotBuildState::Failed;
        ScopedResult.error = "compiler reported success but produced no library: " + ScopedResult.libraryPath.string();
        Publish(ScopedResult);
        return;
    }

    if (!InOptions.keepSource)
    {
        std::error_code ScopedRemoveError;
        std::filesystem::remove(ScopedResult.sourcePath, ScopedRemoveError);
    }
    ScopedResult.state = AotBuildState::Succeeded;
    Publish(ScopedResult);
}

bool AotBuilder::StartProgram(std::span<const std::byte> InCooked, const AotBuildOptions& InOptions,
                              std::string* InError)
{
    if (Busy())
    {
        if (InError != nullptr)
        {
            *InError = "an AOT build is already running";
        }
        return false;
    }
    if (InCooked.empty())
    {
        if (InError != nullptr)
        {
            *InError = "the compiled program is empty";
        }
        return false;
    }
    if (InOptions.outputDirectory.empty() || InOptions.includeDirectory.empty() || InOptions.compiler.empty())
    {
        if (InError != nullptr)
        {
            *InError = "AotBuildOptions needs outputDirectory, includeDirectory and compiler";
        }
        return false;
    }

    // Validate before a thread is created so a broken program reports synchronously.
    Script::CompiledProgram ScopedProgram;
    std::string ScopedProgramError;
    if (!Script::ParseProgram(InCooked, &ScopedProgram, &ScopedProgramError))
    {
        if (InError != nullptr)
        {
            *InError = "program does not parse: " + ScopedProgramError;
        }
        return false;
    }
    std::string ScopedSource;
    std::string ScopedTranspileError;
    if (!Script::TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedTranspileError))
    {
        if (InError != nullptr)
        {
            *InError = ScopedTranspileError;
        }
        return false;
    }

    std::vector<std::byte> ScopedBytes(InCooked.begin(), InCooked.end());
    MState.store(AotBuildState::Compiling, std::memory_order_release);
    MRevision.fetch_add(1, std::memory_order_release);
    MWorker = std::jthread(
        [this](std::stop_token InStop, std::vector<std::byte> InWorkerBytes, AotBuildOptions InWorkerOptions)
        { RunProgram(InStop, std::move(InWorkerBytes), std::move(InWorkerOptions)); },
        std::move(ScopedBytes), InOptions);
    if (InError != nullptr)
    {
        InError->clear();
    }
    return true;
}

void AotBuilder::RunProgram(std::stop_token InStop, std::vector<std::byte> InCooked, AotBuildOptions InOptions)
{
    AotBuildResult ScopedResult;
    ScopedResult.state = AotBuildState::Compiling;
    ScopedResult.graphHash = Script::HashProgramBytes(InCooked);
    ScopedResult.sourcePath = InOptions.outputDirectory / (InOptions.moduleName + ".cpp");
    ScopedResult.libraryPath = InOptions.outputDirectory / (InOptions.moduleName + LibrarySuffix());
    ScopedResult.logPath = InOptions.outputDirectory / (InOptions.moduleName + ".log");

    Script::CompiledProgram ScopedProgram;
    std::string ScopedError;
    if (!Script::ParseProgram(InCooked, &ScopedProgram, &ScopedError))
    {
        ScopedResult.state = AotBuildState::Failed;
        ScopedResult.error = ScopedError;
        Publish(ScopedResult);
        return;
    }
    std::string ScopedSource;
    if (!Script::TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError))
    {
        ScopedResult.state = AotBuildState::Failed;
        ScopedResult.error = ScopedError;
        Publish(ScopedResult);
        return;
    }
    ScopedResult.instructions = ScopedProgram.header.codeCount;
    FinishBuild(InStop, ScopedResult, ScopedSource, InOptions);
}

void AotBuilder::Publish(const AotBuildResult& InResult) noexcept
{
    {
        std::lock_guard ScopedLock(MMutex);
        MResult = InResult;
    }
    MState.store(InResult.state, std::memory_order_release);
    MRevision.fetch_add(1, std::memory_order_release);
}

AotBuildResult AotBuilder::Snapshot() const
{
    std::lock_guard ScopedLock(MMutex);
    return MResult;
}

void AotBuilder::Cancel()
{
    if (!MWorker.joinable())
    {
        return;
    }
    MWorker.request_stop();
    MWorker.join();
    if (MState.load(std::memory_order_acquire) == AotBuildState::Compiling)
    {
        MState.store(AotBuildState::Idle, std::memory_order_release);
        MRevision.fetch_add(1, std::memory_order_release);
    }
}
}
