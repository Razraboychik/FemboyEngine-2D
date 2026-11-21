#include "Engine/Script/ScriptFileReloader.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{
std::uint32_t GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void WriteSource(const std::filesystem::path& InPath, const char* InText)
{
    std::ofstream ScopedFile(InPath, std::ios::binary | std::ios::trunc | std::ios::out);
    ScopedFile << InText;
}
}

int main()
{
    const std::filesystem::path ScopedPath =
        std::filesystem::current_path() / "fby_script_reload_test.fbs";
    WriteSource(ScopedPath, "event update { self.Transform2D.Rotation = 1.0; }\n");

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "runtime initializes");
    Fby::ScriptFileReloader ScopedReloader(ScopedPath, ScopedComponents);
    std::uint32_t ScopedHandle = Fby::kScriptNoProgram;
    std::string ScopedError;

    Check(ScopedReloader.Poll(ScopedRuntime, &ScopedHandle, &ScopedError) ==
              Fby::ScriptFileReloader::State::Reloaded,
          "first poll loads the script");
    Check(ScopedHandle != Fby::kScriptNoProgram && ScopedRuntime.ProgramCount() == 1, "script is live");
    Check(ScopedReloader.Poll(ScopedRuntime, &ScopedHandle, &ScopedError) ==
              Fby::ScriptFileReloader::State::NoChange,
          "unchanged poll does nothing");

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WriteSource(ScopedPath, "event update { this is not a script }\n");
    Check(ScopedReloader.Poll(ScopedRuntime, &ScopedHandle, &ScopedError) ==
              Fby::ScriptFileReloader::State::Failed,
          "invalid source fails");
    Check(ScopedRuntime.ProgramCount() == 1, "failed reload keeps the old script");

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WriteSource(ScopedPath, "event update { self.Transform2D.Rotation = 2.0; }\n");
    Check(ScopedReloader.Poll(ScopedRuntime, &ScopedHandle, &ScopedError) ==
              Fby::ScriptFileReloader::State::Reloaded,
          "fixed source reloads");
    Check(ScopedRuntime.ProgramCount() == 1, "reload still has one program");

    std::error_code ScopedCode;
    std::filesystem::remove(ScopedPath, ScopedCode);
    Check(ScopedReloader.Poll(ScopedRuntime, &ScopedHandle, &ScopedError) ==
              Fby::ScriptFileReloader::State::Failed,
          "missing file fails");

    if (GFailures == 0)
    {
        std::printf("script_file_reloader_test: all checks passed\n");
        return 0;
    }
    return 1;
}
