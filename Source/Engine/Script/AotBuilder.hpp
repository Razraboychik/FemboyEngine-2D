// FemboyEngine2D - background LogicGraph AOT cook service.
#pragma once

#include "Engine/Script/GraphCompiler.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Fby::Script
{

enum class AotBuildState : std::uint8_t
{
    Idle = 0,
    Compiling,
    Succeeded,
    Failed,
};

[[nodiscard]] constexpr const char* AotBuildStateName(AotBuildState InState) noexcept
{
    switch (InState)
    {
        case AotBuildState::Idle:
            return "idle";
        case AotBuildState::Compiling:
            return "compiling";
        case AotBuildState::Succeeded:
            return "succeeded";
        case AotBuildState::Failed:
            return "failed";
    }
    return "?";
}

struct AotBuildOptions
{
    // Required. The directory receives <moduleName>.cpp and <moduleName>.<ext>.
    std::filesystem::path outputDirectory{};
    // Required. The engine/include root, so the generated file can include
    std::filesystem::path includeDirectory{};
    // Clang/GCC driver (clang++/g++/clang-cl/cl are all recognised). On Windows
    std::string compiler = "clang++";
    std::string moduleName = "logic_graph";
    // Keep the generated .cpp next to the native library for inspection.
    bool keepSource = true;
    // Header force-included before the generated unit. ReleaseCooker uses it to
    std::filesystem::path forceIncludeHeader{};
    // Extra flags appended after the standard ones. Kept explicit so a cook can
    std::string extraFlags{};
};

struct AotBuildResult
{
    AotBuildState state = AotBuildState::Idle;
    std::filesystem::path sourcePath{};
    std::filesystem::path libraryPath{};
    std::filesystem::path logPath{};
    std::string error{};
    std::uint32_t instructions = 0;
    std::uint64_t graphHash = 0;
};

class AotBuilder
{
public:
    AotBuilder() = default;
    ~AotBuilder();

    AotBuilder(const AotBuilder&) = delete;
    AotBuilder& operator=(const AotBuilder&) = delete;

    // Starts a cook. Returns false when a build is already running, the graph
    [[nodiscard]] bool Start(const LogicGraph& InGraph, const AotBuildOptions& InOptions,
                             std::string* InError = nullptr);
    // Cooks already-compiled program bytes (a text script or a graph the editor
    // already compiled): skips the graph stage and transpiles the program.
    [[nodiscard]] bool StartProgram(std::span<const std::byte> InCooked, const AotBuildOptions& InOptions,
                                    std::string* InError = nullptr);
    // Requests cancellation and joins. A compiler already in flight finishes
    void Cancel();

    [[nodiscard]] bool Busy() const noexcept
    {
        return MState.load(std::memory_order_acquire) == AotBuildState::Compiling;
    }
    [[nodiscard]] AotBuildState State() const noexcept
    {
        return MState.load(std::memory_order_acquire);
    }
    // Monotonic counter bumped whenever the result changes. The editor compares
    [[nodiscard]] std::uint64_t Revision() const noexcept
    {
        return MRevision.load(std::memory_order_acquire);
    }
    [[nodiscard]] AotBuildResult Snapshot() const;

private:
    void Run(std::stop_token InStop, LogicGraph InGraph, AotBuildOptions InOptions);
    void RunProgram(std::stop_token InStop, std::vector<std::byte> InCooked, AotBuildOptions InOptions);
    void FinishBuild(std::stop_token InStop, AotBuildResult& InResult, const std::string& InSource,
                     const AotBuildOptions& InOptions);
    void Publish(const AotBuildResult& InResult) noexcept;

    mutable std::mutex MMutex{};
    AotBuildResult MResult{};
    std::jthread MWorker{};
    std::atomic<AotBuildState> MState{AotBuildState::Idle};
    std::atomic<std::uint64_t> MRevision{0};
};
}
