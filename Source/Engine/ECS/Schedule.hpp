// FemboyEngine2D - frame phases and the legacy flat schedule.
#pragma once

#include "Engine/Core/JobSystem.hpp"

#include <entt/entt.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby
{
class AudioSystem;
class FrameArena;
class PhysicsSystem;
class WindowManager;
class VirtualFileSystem;
class AccessibilityKernel;
class LocalizationTable;
class ClothSystem;
class FluidWaveSystem;
class PhysicsJointSystem;
class CameraStackSystem;
namespace Assets
{
class AssetDatabase;
}
namespace Rhi
{
class RHI;
}
namespace Ui
{
class FontAtlasSet;
}
namespace Text
{
class TextService;
class TextShapeCache;
class ITextShaper;
}
}

namespace Fby::Ecs
{

class CommandBuffer;
class SystemGraph;

using Registry = entt::registry;

enum class Phase : std::uint8_t
{
    PreUpdate = 0,
    FixedUpdate,
    PostUpdate,
    Render,
    Count
};

[[nodiscard]] constexpr const char* PhaseName(Phase InPhase) noexcept
{
    switch (InPhase)
    {
        case Phase::PreUpdate:
            return "PreUpdate";
        case Phase::FixedUpdate:
            return "FixedUpdate";
        case Phase::PostUpdate:
            return "PostUpdate";
        case Phase::Render:
            return "Render";
        case Phase::Count:
            break;
    }
    return "?";
}

// What a system is allowed to touch. No engine singleton, no global registry:
struct SystemContext
{
    Registry& registry;
    JobSystem& jobs;
    float dt = 0.0f;
    std::uint64_t frameIndex = 0;
    Rhi::RHI* renderer = nullptr;
    WindowManager* window = nullptr;
    VirtualFileSystem* assets = nullptr;
    Assets::AssetDatabase* assetDb = nullptr;
    AudioSystem* audio = nullptr;
    PhysicsSystem* physics = nullptr;
    float interpolationAlpha = 1.0f;
    CommandBuffer* commands = nullptr;
    FrameArena* frameArena = nullptr;

    // Extension module pointers. Appended so the original SystemContext layout
    PhysicsJointSystem* joints = nullptr;
    ClothSystem* cloth = nullptr;
    FluidWaveSystem* fluid = nullptr;
    CameraStackSystem* cameraStack = nullptr;
    AccessibilityKernel* accessibility = nullptr;
    Ui::FontAtlasSet* fonts = nullptr;
    Text::TextService* text = nullptr;
    // Optional shaped-text path for UI text blocks and cluster-aware input.
    Text::TextShapeCache* textShapes = nullptr;
    Text::ITextShaper* textShaper = nullptr;
    LocalizationTable* localization = nullptr;
};

using SystemFunction = void (*)(SystemContext&, void*);

class Schedule
{
public:
    static constexpr std::uint32_t kMaxSystems = 128;

    struct Entry
    {
        const char* name = nullptr;
        Phase phase = Phase::PreUpdate;
        SystemFunction run = nullptr;
        void* user = nullptr;
    };

    [[nodiscard]] bool Add(Phase InPhase, const char* InName, SystemFunction InRun, void* InUser = nullptr);
    template <auto Function>
        requires std::is_invocable_r_v<void, decltype(Function), SystemContext&>
    [[nodiscard]] bool Add(Phase InPhase, const char* InName)
    {
        return Add(InPhase, InName, +[](SystemContext& InContext, void*) { Function(InContext); }, nullptr);
    }
    void Clear() noexcept;
    void Run(Phase InPhase, SystemContext& InContext) const;
    [[nodiscard]] std::size_t Count(Phase InPhase) const noexcept;
    [[nodiscard]] std::span<const Entry> Systems() const noexcept
    {
        return {MSystems.data(), MCount};
    }

private:
    std::array<Entry, kMaxSystems> MSystems{};
    std::uint32_t MCount = 0;
};
}
