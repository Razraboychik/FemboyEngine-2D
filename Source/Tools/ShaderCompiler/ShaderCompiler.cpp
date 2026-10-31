// FemboyEngine2D - glslang wrapper for the in-process shader compiler core.
#include "Tools/ShaderCompiler/ShaderCompiler.hpp"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <exception>
#include <string>

namespace Fby::Shaderc
{

namespace
{

[[nodiscard]] EShLanguage ToGlslangStage(ShaderStage InStage) noexcept
{
    switch (InStage)
    {
        case ShaderStage::Vertex:
            return EShLangVertex;
        case ShaderStage::Fragment:
            return EShLangFragment;
        case ShaderStage::Compute:
            return EShLangCompute;
        case ShaderStage::Geometry:
            return EShLangGeometry;
    }
    return EShLangCount;
}

void SetError(std::string* InError, std::string_view InText)
{
    if (InError != nullptr)
    {
        InError->assign(InText);
    }
}

// glslang's process is reference counted, so an editor that compiles a material
class GlslangProcessGuard
{
public:
    GlslangProcessGuard() noexcept
    {
        glslang::InitializeProcess();
    }
    ~GlslangProcessGuard()
    {
        glslang::FinalizeProcess();
    }
    GlslangProcessGuard(const GlslangProcessGuard&) = delete;
    GlslangProcessGuard& operator=(const GlslangProcessGuard&) = delete;
};

void AppendInfoLog(std::string* InError, const char* InStage, const char* InInfoLog)
{
    if (InError == nullptr)
    {
        return;
    }
    InError->assign("fby_shaderc: ");
    InError->append(InStage);
    InError->append(" compile failed");
    if (InInfoLog != nullptr && InInfoLog[0] != '\0')
    {
        InError->append(":\n");
        InError->append(InInfoLog);
    }
}
}

bool IsSpirvModule(const std::uint32_t* InWords, std::size_t InWordCount) noexcept
{
    return InWords != nullptr && InWordCount >= kSpirvMinimumWords && InWords[0] == kSpirvMagic;
}

bool CompileGlslToSpirv(std::string_view InSource, ShaderStage InStage, std::vector<std::uint32_t>* InSpirv,
                        std::string* InError) noexcept
{
    if (InSpirv == nullptr)
    {
        SetError(InError, "fby_shaderc: null SPIR-V output");
        return false;
    }
    InSpirv->clear();
    if (InSource.empty())
    {
        SetError(InError, "fby_shaderc: empty shader source");
        return false;
    }
    if (InError != nullptr)
    {
        InError->clear();
    }

    const EShLanguage ScopedLanguage = ToGlslangStage(InStage);
    if (ScopedLanguage == EShLangCount)
    {
        SetError(InError, "fby_shaderc: unknown shader stage");
        return false;
    }

    try
    {
        GlslangProcessGuard ScopedProcess;

        glslang::TShader ScopedShader(ScopedLanguage);
        const char* ScopedText = InSource.data();
        const int ScopedLength = static_cast<int>(InSource.size());
        ScopedShader.setStringsWithLengths(&ScopedText, &ScopedLength, 1);

        // Same environment the engine's build-time shaders use: Vulkan 1.1,
        ScopedShader.setEnvInput(glslang::EShSourceGlsl, ScopedLanguage, glslang::EShClientVulkan, 110);
        ScopedShader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        ScopedShader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

        if (!ScopedShader.parse(GetDefaultResources(), 450, false, EShMsgDefault))
        {
            AppendInfoLog(InError, ShaderStageName(InStage), ScopedShader.getInfoLog());
            return false;
        }

        glslang::TProgram ScopedProgram;
        ScopedProgram.addShader(&ScopedShader);
        if (!ScopedProgram.link(EShMsgDefault))
        {
            AppendInfoLog(InError, ShaderStageName(InStage), ScopedProgram.getInfoLog());
            return false;
        }

        glslang::GlslangToSpv(*ScopedProgram.getIntermediate(ScopedLanguage), *InSpirv);
    }
    catch (const std::exception& ScopedException)
    {
        SetError(InError, std::string("fby_shaderc: exception while compiling: ") + ScopedException.what());
        InSpirv->clear();
        return false;
    }
    catch (...)
    {
        SetError(InError, "fby_shaderc: unknown exception while compiling");
        InSpirv->clear();
        return false;
    }

    if (!IsSpirvModule(InSpirv->data(), InSpirv->size()))
    {
        SetError(InError, "fby_shaderc: glslang produced no valid SPIR-V module");
        InSpirv->clear();
        return false;
    }
    return true;
}
}
