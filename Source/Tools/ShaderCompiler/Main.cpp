// FemboyEngine2D - build-time GLSL -> SPIR-V compiler.
#include "ShaderCompiler.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

[[nodiscard]] bool StageFromPath(const std::string& InPath, Fby::Shaderc::ShaderStage* InStage)
{
    const auto ScopedEndsWith = [&InPath](const char* InSuffix)
    {
        const std::size_t ScopedLength = std::strlen(InSuffix);
        return InPath.size() >= ScopedLength &&
               InPath.compare(InPath.size() - ScopedLength, ScopedLength, InSuffix) == 0;
    };

    if (ScopedEndsWith(".vert"))
    {
        *InStage = Fby::Shaderc::ShaderStage::Vertex;
        return true;
    }
    if (ScopedEndsWith(".frag"))
    {
        *InStage = Fby::Shaderc::ShaderStage::Fragment;
        return true;
    }
    if (ScopedEndsWith(".comp"))
    {
        *InStage = Fby::Shaderc::ShaderStage::Compute;
        return true;
    }
    if (ScopedEndsWith(".geom"))
    {
        *InStage = Fby::Shaderc::ShaderStage::Geometry;
        return true;
    }
    return false;
}

[[nodiscard]] std::string ReadFile(const std::string& InPath)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return {};
    }
    std::ostringstream ScopedBuffer;
    ScopedBuffer << ScopedStream.rdbuf();
    return ScopedBuffer.str();
}
}

int main(int argc, char** argv)
{
    std::string ScopedInput;
    std::string ScopedOutput;

    for (int ScopedI = 1; ScopedI < argc; ++ScopedI)
    {
        if (std::strcmp(argv[ScopedI], "-o") == 0 && ScopedI + 1 < argc)
        {
            ScopedOutput = argv[++ScopedI];
        }
        else
        {
            ScopedInput = argv[ScopedI];
        }
    }

    if (ScopedInput.empty())
    {
        std::fprintf(stderr, "fby_shaderc: no input file\n");
        return 1;
    }
    if (ScopedOutput.empty())
    {
        ScopedOutput = ScopedInput.substr(0, ScopedInput.find_last_of('.')) + ".spv";
    }

    Fby::Shaderc::ShaderStage ScopedStage = Fby::Shaderc::ShaderStage::Vertex;
    if (!StageFromPath(ScopedInput, &ScopedStage))
    {
        std::fprintf(stderr, "fby_shaderc: unknown shader stage for '%s'\n", ScopedInput.c_str());
        return 1;
    }

    const std::string ScopedSource = ReadFile(ScopedInput);
    if (ScopedSource.empty())
    {
        std::fprintf(stderr, "fby_shaderc: cannot read '%s'\n", ScopedInput.c_str());
        return 1;
    }

    std::vector<std::uint32_t> ScopedSpirv;
    std::string ScopedError;
    if (!Fby::Shaderc::CompileGlslToSpirv(ScopedSource, ScopedStage, &ScopedSpirv, &ScopedError))
    {
        std::fprintf(stderr, "%s\n", ScopedError.c_str());
        return 1;
    }

    std::ofstream ScopedStream(ScopedOutput, std::ios::binary);
    ScopedStream.write(reinterpret_cast<const char*>(ScopedSpirv.data()),
                       static_cast<std::streamsize>(ScopedSpirv.size() * sizeof(std::uint32_t)));
    if (!ScopedStream)
    {
        std::fprintf(stderr, "fby_shaderc: cannot write '%s'\n", ScopedOutput.c_str());
        return 1;
    }

    std::printf("fby_shaderc: %s -> %s (%zu words)\n", ScopedInput.c_str(), ScopedOutput.c_str(), ScopedSpirv.size());
    return 0;
}
