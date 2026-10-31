// FemboyEngine2D - fby_aotc: cook-time FemboyScript AOT compiler driver.
#include "Engine/Core/BuildProfile.hpp"
#include "Engine/Script/AotTranspiler.hpp"
#include "Engine/Script/Bytecode.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace Build = Fby::Build;

namespace
{
[[nodiscard]] bool ReadFile(const char* InPath, std::vector<std::byte>* Out)
{
    std::ifstream ScopedStream(InPath, std::ios::binary | std::ios::ate);
    if (!ScopedStream)
    {
        return false;
    }
    const std::streamsize ScopedSize = ScopedStream.tellg();
    if (ScopedSize <= 0)
    {
        return false;
    }
    Out->resize(static_cast<std::size_t>(ScopedSize));
    ScopedStream.seekg(0, std::ios::beg);
    return static_cast<bool>(ScopedStream.read(reinterpret_cast<char*>(Out->data()), ScopedSize));
}

[[nodiscard]] bool WriteText(const char* InPath, const std::string& InText)
{
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.write(InText.data(), static_cast<std::streamsize>(InText.size()));
    return static_cast<bool>(ScopedStream);
}

void Usage()
{
    std::fprintf(stderr,
                 "usage: fby_aotc <program.fbsc> -o <out.cpp> [--name <name>]\n"
                 "       fby_aotc <program.fbsc> --compile <out.dll> --compiler <clang-cl|g++> --include <dir>\n"
                 "                 [--opt O1|O2|O3|Ofast|Os] [--lto] [--native]\n"
                 "       The opt flags match Project Settings -> AOT Cook / Final Build.\n");
}
}

int main(int argc, char** argv)
{
    const char* ScopedInput = nullptr;
    const char* ScopedOutput = nullptr;
    const char* ScopedCompiledOutput = nullptr;
    const char* ScopedCompiler = nullptr;
    const char* ScopedIncludeDir = nullptr;
    std::string ScopedName = "visual_graph";
    Build::OptimizationLevel ScopedOptimization = Build::OptimizationLevel::O2;
    bool ScopedLto = false;
    bool ScopedNative = false;

    for (int ScopedI = 1; ScopedI < argc; ++ScopedI)
    {
        const auto ScopedMatch = [&](const char* InFlag) { return std::strcmp(argv[ScopedI], InFlag) == 0; };
        if (ScopedMatch("-o") && ScopedI + 1 < argc)
        {
            ScopedOutput = argv[++ScopedI];
        }
        else if (ScopedMatch("--name") && ScopedI + 1 < argc)
        {
            ScopedName = argv[++ScopedI];
        }
        else if (ScopedMatch("--compile") && ScopedI + 1 < argc)
        {
            ScopedCompiledOutput = argv[++ScopedI];
        }
        else if (ScopedMatch("--compiler") && ScopedI + 1 < argc)
        {
            ScopedCompiler = argv[++ScopedI];
        }
        else if (ScopedMatch("--include") && ScopedI + 1 < argc)
        {
            ScopedIncludeDir = argv[++ScopedI];
        }
        else if (ScopedMatch("--opt") && ScopedI + 1 < argc)
        {
            ScopedOptimization = Build::ParseOptimizationLevel(argv[++ScopedI], Build::OptimizationLevel::O2);
            if (Build::OptimizationName(ScopedOptimization) != std::string_view(argv[ScopedI]))
            {
                std::fprintf(stderr, "fby_aotc: unknown optimisation '%s' (O1, O2, O3, Ofast, Os)\n", argv[ScopedI]);
                return 1;
            }
        }
        else if (ScopedMatch("--lto"))
        {
            ScopedLto = true;
        }
        else if (ScopedMatch("--native"))
        {
            ScopedNative = true;
        }
        else if (argv[ScopedI][0] != '-')
        {
            ScopedInput = argv[ScopedI];
        }
        else
        {
            Usage();
            return 1;
        }
    }

    if (ScopedInput == nullptr || (ScopedOutput == nullptr && ScopedCompiledOutput == nullptr))
    {
        Usage();
        return 1;
    }

    std::vector<std::byte> ScopedBytes;
    if (!ReadFile(ScopedInput, &ScopedBytes))
    {
        std::fprintf(stderr, "fby_aotc: cannot read '%s'\n", ScopedInput);
        return 1;
    }

    Fby::Script::CompiledProgram ScopedProgram;
    std::string ScopedError;
    if (!Fby::Script::ParseProgram(ScopedBytes, &ScopedProgram, &ScopedError))
    {
        std::fprintf(stderr, "fby_aotc: invalid bytecode '%s': %s\n", ScopedInput, ScopedError.c_str());
        return 1;
    }

    Fby::Script::AotTranspileOptions ScopedOptions;
    ScopedOptions.programName = ScopedName;
    std::string ScopedSource;
    if (!Fby::Script::TranspileProgramToCpp(ScopedProgram, ScopedOptions, &ScopedSource, &ScopedError))
    {
        std::fprintf(stderr, "fby_aotc: transpile failed: %s\n", ScopedError.c_str());
        return 1;
    }

    const char* ScopedSourcePath = ScopedOutput;
    std::string ScopedTemporarySource;
    if (ScopedSourcePath == nullptr)
    {
        // --compile without -o: write the C++ next to the requested binary.
        ScopedTemporarySource = std::string(ScopedCompiledOutput) + ".cpp";
        ScopedSourcePath = ScopedTemporarySource.c_str();
    }
    if (!WriteText(ScopedSourcePath, ScopedSource))
    {
        std::fprintf(stderr, "fby_aotc: cannot write '%s'\n", ScopedSourcePath);
        return 1;
    }
    std::printf("fby_aotc: %s -> %s (%zu bytes, %u instructions)\n", ScopedInput, ScopedSourcePath, ScopedSource.size(),
                ScopedProgram.header.codeCount);

    if (ScopedCompiledOutput != nullptr)
    {
        if (ScopedCompiler == nullptr || ScopedIncludeDir == nullptr)
        {
            std::fprintf(stderr, "fby_aotc: --compile needs --compiler and --include\n");
            return 1;
        }
        std::printf("fby_aotc: profile %.*s - %.*s\n", static_cast<int>(Build::OptimizationName(ScopedOptimization).size()),
                    Build::OptimizationName(ScopedOptimization).data(),
                    static_cast<int>(Build::Recommend(ScopedOptimization, true, false).headline.size()),
                    Build::Recommend(ScopedOptimization, true, false).headline.data());
        std::string ScopedCommand = "\"";
        ScopedCommand += ScopedCompiler;
        ScopedCommand += "\"";
        if (std::strstr(ScopedCompiler, "clang-cl") != nullptr || std::strstr(ScopedCompiler, "cl.exe") != nullptr)
        {
            ScopedCommand += " /nologo /std:c++latest ";
            ScopedCommand += std::string(Build::OptimizationFlagMsvc(ScopedOptimization));
            if (Build::RelaxesFloatingPoint(ScopedOptimization))
            {
                ScopedCommand += " /fp:fast";
            }
            if (ScopedLto)
            {
                ScopedCommand += " /GL";
            }
            if (ScopedNative)
            {
                ScopedCommand += " /arch:AVX2";
            }
            ScopedCommand += " /LD /EHsc /I\"";
            ScopedCommand += ScopedIncludeDir;
            ScopedCommand += "\" \"";
            ScopedCommand += ScopedSourcePath;
            ScopedCommand += "\" /Fe:\"";
            ScopedCommand += ScopedCompiledOutput;
            ScopedCommand += "\" /Fo:\"";
            ScopedCommand += std::string(ScopedCompiledOutput) + ".obj\"";
        }
        else
        {
            ScopedCommand += " -std=c++26 ";
            ScopedCommand += std::string(Build::OptimizationFlag(ScopedOptimization));
            if (Build::RelaxesFloatingPoint(ScopedOptimization))
            {
                ScopedCommand += " -ffast-math";
            }
            if (ScopedLto)
            {
                ScopedCommand += " -flto";
            }
            if (ScopedNative)
            {
                ScopedCommand += " -march=native";
            }
            ScopedCommand += " -shared -fPIC -I\"";
            ScopedCommand += ScopedIncludeDir;
            ScopedCommand += "\" \"";
            ScopedCommand += ScopedSourcePath;
            ScopedCommand += "\" -o \"";
            ScopedCommand += ScopedCompiledOutput;
            ScopedCommand += "\"";
        }
        std::printf("fby_aotc: compiling: %s\n", ScopedCommand.c_str());
        const int ScopedResult = std::system(ScopedCommand.c_str());
        if (ScopedResult != 0)
        {
            std::fprintf(stderr, "fby_aotc: native compile failed (%d)\n", ScopedResult);
            return 1;
        }
        std::printf("fby_aotc: native plugin -> %s\n", ScopedCompiledOutput);
    }

    return 0;
}
