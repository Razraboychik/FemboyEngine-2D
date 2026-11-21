#include "Engine/Script/ScriptFileReloader.hpp"

#include "Engine/Script/TextScript.hpp"

#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace Fby
{
namespace
{
constexpr std::uintmax_t kMaxScriptBytes = 1024u * 1024u;

void SetError(std::string* InError, std::string_view InMessage)
{
    if (InError != nullptr)
    {
        InError->assign(InMessage);
    }
}
}

ScriptFileReloader::ScriptFileReloader(std::filesystem::path InPath, const Scene::ComponentRegistry& InComponents)
    : MPath(std::move(InPath)), MComponents(InComponents)
{
}

bool ScriptFileReloader::LoadSource(std::string* OutSource, std::string* InError) const
{
    std::error_code ScopedError;
    if (!std::filesystem::is_regular_file(MPath, ScopedError))
    {
        SetError(InError, "script file is missing");
        return false;
    }
    const std::uintmax_t ScopedSize = std::filesystem::file_size(MPath, ScopedError);
    if (ScopedError || ScopedSize > kMaxScriptBytes)
    {
        SetError(InError, "script file is too large or unreadable");
        return false;
    }
    std::ifstream ScopedFile(MPath, std::ios::binary);
    if (!ScopedFile)
    {
        SetError(InError, "script file cannot be opened");
        return false;
    }
    OutSource->assign(std::istreambuf_iterator<char>(ScopedFile), std::istreambuf_iterator<char>());
    if (!ScopedFile.good() && !ScopedFile.eof())
    {
        SetError(InError, "script file cannot be read");
        return false;
    }
    return true;
}

ScriptFileReloader::State ScriptFileReloader::Poll(ScriptRuntime& InRuntime, std::uint32_t* InHandle,
                                                   std::string* InError)
{
    if (InHandle == nullptr)
    {
        SetError(InError, "script reload handle is null");
        return State::Failed;
    }
    std::error_code ScopedError;
    const std::filesystem::file_time_type ScopedWrite = std::filesystem::last_write_time(MPath, ScopedError);
    const bool ScopedHasTimestamp = !ScopedError;
    if (ScopedHasTimestamp && MHasTimestamp && ScopedWrite == MLastWrite)
    {
        return State::NoChange;
    }
    std::string ScopedSource;
    if (!LoadSource(&ScopedSource, InError))
    {
        return State::Failed;
    }
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (const char ScopedCharacter : ScopedSource)
    {
        ScopedHash ^= static_cast<std::uint8_t>(ScopedCharacter);
        ScopedHash *= 1099511628211ull;
    }
    if (!ScopedHasTimestamp && MHasHash && ScopedHash == MSourceHash)
    {
        return State::NoChange;
    }
    std::vector<std::byte> ScopedProgram;
    if (!Script::CompileTextScript(ScopedSource, MComponents, {}, &ScopedProgram, InError))
    {
        return State::Failed;
    }
    if (!InRuntime.ReloadProgram(InHandle, ScopedProgram, InError))
    {
        return State::Failed;
    }
    if (ScopedHasTimestamp)
    {
        MLastWrite = ScopedWrite;
        MHasTimestamp = true;
    }
    MSourceHash = ScopedHash;
    MHasHash = true;
    return State::Reloaded;
}
}
