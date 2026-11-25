// FemboyEngine2D - source-file driven script hot reload.
#pragma once

#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/Script/ScriptSystem.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Fby
{
class ScriptFileReloader
{
public:
    enum class State : std::uint8_t
    {
        NoChange,
        Reloaded,
        Failed
    };

    ScriptFileReloader(std::filesystem::path InPath, const Scene::ComponentRegistry& InComponents);

    // Compiles the file and swaps the program only after a successful compile.
    // A failed attempt keeps the previous program and timestamp, so the next
    // poll retries after the file is fixed.
    [[nodiscard]] State Poll(ScriptRuntime& InRuntime, std::uint32_t* InHandle, std::string* InError = nullptr);

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return MPath;
    }

private:
    [[nodiscard]] bool LoadSource(std::string* OutSource, std::string* InError) const;

    std::filesystem::path MPath;
    const Scene::ComponentRegistry& MComponents;
    std::filesystem::file_time_type MLastWrite{};
    std::uint64_t MSourceHash = 0;
    bool MHasTimestamp = false;
    bool MHasHash = false;
};
}
