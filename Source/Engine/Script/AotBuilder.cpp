// FemboyEngine2D - background AOT cook service implementation.
#include "Engine/Script/AotBuilder.hpp"

#include "Engine/Script/AotGraphCompiler.hpp"
#include "Engine/Core/ProcessLaunch.hpp"

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
    if (!ScopedStream)
    {
        return {};
    }
    ScopedStream.read(ScopedText.data(), static_cast<std::streamsize>(ScopedWanted));
    if (!ScopedStream)
    {
        return {};
    }
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

[[nodiscard]] int RunProcess(std::span<const std::string> InArguments, const std::filesystem::path& InLogPath)
{
    if (InArguments.empty())
    {
        return -1;
    }
    const Fby::Process::RunResult ScopedResult =
        Fby::Process::Run(InArguments.front(), InArguments, InLogPath.string());
    return ScopedResult.launched ? ScopedResult.exitCode : -1;
}

void AppendExtraFlags(std::vector<std::string>& InArguments, std::string_view InFlags)
{
    std::string ScopedToken;
    for (const char ScopedCharacter : InFlags)
    {
        if (ScopedCharacter == ' ' || ScopedCharacter == '\t' || ScopedCharacter == '\r' ||
            ScopedCharacter == '\n')
        {
            if (!ScopedToken.empty())
            {
                InArguments.push_back(std::move(ScopedToken));
                ScopedToken.clear();
            }
            continue;
        }
        ScopedToken.push_back(ScopedCharacter);
    }
    if (!ScopedToken.empty())
    {
        InArguments.push_back(std::move(ScopedToken));
    }
}

[[nodiscard]] std::vector<std::string> BuildArguments(const AotBuildOptions& InOptions,
                                                      const std::filesystem::path& InSource,
                                                      const std::filesystem::path& InLibrary)
{
    std::vector<std::string> ScopedArguments;
    ScopedArguments.emplace_back(InOptions.compiler);
    if (IsMsvcDriver(InOptions.compiler))
    {
        ScopedArguments.push_back("/nologo");
        ScopedArguments.push_back("/std:c++latest");
        ScopedArguments.push_back("/O2");
        ScopedArguments.push_back("/LD");
        ScopedArguments.push_back("/EHsc");
        ScopedArguments.push_back("/I");
        ScopedArguments.emplace_back(InOptions.includeDirectory.string());
        if (!InOptions.forceIncludeHeader.empty())
        {
            ScopedArguments.push_back("/FI");
            ScopedArguments.emplace_back(InOptions.forceIncludeHeader.string());
        }
        ScopedArguments.emplace_back(InSource.string());
        ScopedArguments.emplace_back(std::string("/Fe:") + InLibrary.string());
        ScopedArguments.emplace_back(std::string("/Fo:") + InLibrary.string() + ".obj");
    }
    else
    {
#if defined(__APPLE__)
        ScopedArguments.push_back("-std=c++26");
        ScopedArguments.push_back("-O3");
        ScopedArguments.push_back("-dynamiclib");
#else
        ScopedArguments.push_back("-std=c++26");
        ScopedArguments.push_back("-O3");
        ScopedArguments.push_back("-shared");
#endif
#if !defined(_WIN32)
        ScopedArguments.push_back("-fPIC");
#endif
        ScopedArguments.push_back("-I");
        ScopedArguments.emplace_back(InOptions.includeDirectory.string());
        if (!InOptions.forceIncludeHeader.empty())
        {
            ScopedArguments.push_back("-include");
            ScopedArguments.emplace_back(InOptions.forceIncludeHeader.string());
        }
        ScopedArguments.emplace_back(InSource.string());
        ScopedArguments.push_back("-o");
        ScopedArguments.emplace_back(InLibrary.string());
    }
    AppendExtraFlags(ScopedArguments, InOptions.extraFlags);
    return ScopedArguments;
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
        ScopedStream.close();
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

    const std::vector<std::string> ScopedArguments =
        BuildArguments(InOptions, ScopedResult.sourcePath, ScopedResult.libraryPath);
    const int ScopedStatus = RunProcess(ScopedArguments, ScopedResult.logPath);
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
    std::error_code ScopedLibraryError;
    if (!std::filesystem::is_regular_file(ScopedResult.libraryPath, ScopedLibraryError))
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
