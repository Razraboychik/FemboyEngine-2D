// FemboyEngine2D - the service context one scheduled system receives.
#pragma once

#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/FCS/Registry.hpp"

#include <cstdint>

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
class GameplayTagRegistry;
class GameplayAttributeRegistry;
class GameplayEffectSystem;
class GameplaySkillSystem;
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

using Registry = Fby::Fcs::Registry;

// Engine-wide scratch services every system may use. They are grouped apart from
// the domain bundles because they do not belong to one subsystem.
struct CoreServices
{
    CommandBuffer* commands = nullptr;
    FrameArena* frameArena = nullptr;
};

// Domain bundles. Each bundle owns the service pointers of one subsystem, so a
// caller can hand a system only the bundle it declares instead of the whole
// engine. SystemContext still inherits them flat for source compatibility.
struct RenderServices
{
    Rhi::RHI* renderer = nullptr;
    WindowManager* window = nullptr;
    CameraStackSystem* cameraStack = nullptr;
};

struct AssetServices
{
    VirtualFileSystem* assets = nullptr;
    Assets::AssetDatabase* assetDb = nullptr;
};

struct AudioServices
{
    AudioSystem* audio = nullptr;
};

struct PhysicsServices
{
    PhysicsSystem* physics = nullptr;
    PhysicsJointSystem* joints = nullptr;
    ClothSystem* cloth = nullptr;
    FluidWaveSystem* fluid = nullptr;
};

struct TextServices
{
    Ui::FontAtlasSet* fonts = nullptr;
    Text::TextService* text = nullptr;
    // Optional shaped-text path for UI text blocks and cluster-aware input.
    Text::TextShapeCache* textShapes = nullptr;
    Text::ITextShaper* textShaper = nullptr;
    LocalizationTable* localization = nullptr;
};

struct AccessibilityServices
{
    AccessibilityKernel* accessibility = nullptr;
};

// Project-owned gameplay registries. Systems that read tags take them from the
// context; a world without gameplay leaves them null.
struct GameplayServices
{
    GameplayTagRegistry* gameplayTags = nullptr;
    GameplayAttributeRegistry* gameplayAttributes = nullptr;
    GameplayEffectSystem* gameplayEffects = nullptr;
    GameplaySkillSystem* gameplaySkills = nullptr;
};

struct CoreSystemView;
struct ConstCoreSystemView;
struct PhysicsSystemView;
struct RenderSystemView;
struct ConstRenderSystemView;
struct GameplaySystemView;
struct AudioSystemView;
struct UiSystemView;
struct AccessibilitySystemView;

// What a system is allowed to touch. No engine singleton, no global registry:
// the owner fills the pointers once per frame and every system reads the ones it
// declares. The *View methods cut that set down to one domain for a system that
// does not need the whole engine.
struct SystemContext : CoreServices,
                       RenderServices,
                       AssetServices,
                       AudioServices,
                       PhysicsServices,
                       TextServices,
                       AccessibilityServices,
                       GameplayServices
{
    Registry& registry;
    JobSystem& jobs;
    float dt = 0.0f;
    float uiDt = 0.0f;
    std::uint64_t frameIndex = 0;
    float interpolationAlpha = 1.0f;

    SystemContext(Registry& InRegistry, JobSystem& InJobs, float InDeltaSeconds = 0.0f, std::uint64_t InFrameIndex = 0,
                  float InInterpolationAlpha = 1.0f, float InUiDeltaSeconds = -1.0f) noexcept
        : registry(InRegistry), jobs(InJobs), dt(InDeltaSeconds),
          uiDt(InUiDeltaSeconds >= 0.0f ? InUiDeltaSeconds : InDeltaSeconds), frameIndex(InFrameIndex),
          interpolationAlpha(InInterpolationAlpha)
    {
    }

    [[nodiscard]] CoreSystemView CoreView() noexcept;
    [[nodiscard]] ConstCoreSystemView ConstCoreView() noexcept;
    [[nodiscard]] PhysicsSystemView PhysicsView() noexcept;
    [[nodiscard]] RenderSystemView RenderView() noexcept;
    [[nodiscard]] ConstRenderSystemView ConstRenderView() noexcept;
    [[nodiscard]] GameplaySystemView GameplayView() noexcept;
    [[nodiscard]] AudioSystemView AudioView() noexcept;
    [[nodiscard]] UiSystemView UiView() noexcept;
    [[nodiscard]] AccessibilitySystemView AccessibilityView() noexcept;
};

using SystemFunction = void (*)(SystemContext&, void*);

// Core view: registry, job pool and frame services for systems with no domain
// dependency beyond the frame itself.
struct CoreSystemView
{
    Registry& registry;
    JobSystem& jobs;
    CoreServices& core;
    float dt = 0.0f;
    std::uint64_t frameIndex = 0;
    float interpolationAlpha = 1.0f;
};

using CoreSystemFunction = void (*)(const CoreSystemView&, void*);

template <CoreSystemFunction InFunction> [[nodiscard]] SystemFunction BindCoreSystem() noexcept
{
    return +[](SystemContext& InContext, void* InUser)
    {
        CoreSystemView ScopedView = InContext.CoreView();
        InFunction(ScopedView, InUser);
    };
}

// Read-only core view: the registry is const, so every component view yields
// const references. A system that only inspects the world takes this view and
// cannot create, destroy or write through the ECS.
struct ConstCoreSystemView
{
    const Registry& registry;
    JobSystem& jobs;
    const CoreServices& core;
    float dt = 0.0f;
    std::uint64_t frameIndex = 0;
    float interpolationAlpha = 1.0f;
};

using ConstCoreSystemFunction = void (*)(const ConstCoreSystemView&, void*);

template <ConstCoreSystemFunction InFunction> [[nodiscard]] SystemFunction BindConstCoreSystem() noexcept
{
    return +[](SystemContext& InContext, void* InUser)
    {
        ConstCoreSystemView ScopedView = InContext.ConstCoreView();
        InFunction(ScopedView, InUser);
    };
}

// Narrow view for FixedUpdate systems that only advance physics. The system never
// sees the render, asset or gameplay bundles; BindPhysicsSystem keeps the graph's
// SystemFunction contract at the registration site.
struct PhysicsSystemView
{
    Registry& registry;
    PhysicsServices& services;
    float dt = 0.0f;
    std::uint64_t frameIndex = 0;
    float interpolationAlpha = 1.0f;
};

using PhysicsSystemFunction = void (*)(PhysicsSystemView&, void*);

template <PhysicsSystemFunction InFunction> [[nodiscard]] SystemFunction BindPhysicsSystem() noexcept
{
    return +[](SystemContext& InContext, void* InUser)
    {
        PhysicsSystemView ScopedView = InContext.PhysicsView();
        InFunction(ScopedView, InUser);
    };
}

// Narrow view for Render-phase systems: the registry, the job pool, the render
// bundle and the accessibility profile that modulates presentation, without
// physics, text or gameplay pointers.
struct RenderSystemView
{
    Registry& registry;
    JobSystem& jobs;
    RenderServices& services;
    AccessibilityServices& accessibilityServices;
    float dt = 0.0f;
    float interpolationAlpha = 1.0f;
};

using RenderSystemFunction = void (*)(RenderSystemView&, void*);

template <RenderSystemFunction InFunction> [[nodiscard]] SystemFunction BindRenderSystem() noexcept
{
    return +[](SystemContext& InContext, void* InUser)
    {
        RenderSystemView ScopedView = InContext.RenderView();
        InFunction(ScopedView, InUser);
    };
}

// Read-only render view for extraction passes that must not touch ECS state.
struct ConstRenderSystemView
{
    const Registry& registry;
    JobSystem& jobs;
    const RenderServices& services;
    const AccessibilityServices& accessibilityServices;
    float dt = 0.0f;
    float interpolationAlpha = 1.0f;
};

using ConstRenderSystemFunction = void (*)(const ConstRenderSystemView&, void*);

template <ConstRenderSystemFunction InFunction> [[nodiscard]] SystemFunction BindConstRenderSystem() noexcept
{
    return +[](SystemContext& InContext, void* InUser)
    {
        ConstRenderSystemView ScopedView = InContext.ConstRenderView();
        InFunction(ScopedView, InUser);
    };
}

// Gameplay view adds the tag/attribute/effect/skill bundle and the command
// buffer, which FixedUpdate gameplay systems consume.
struct GameplaySystemView
{
    Registry& registry;
    JobSystem& jobs;
    CoreServices& core;
    GameplayServices& gameplay;
    float dt = 0.0f;
    std::uint64_t frameIndex = 0;
};

using GameplaySystemFunction = void (*)(const GameplaySystemView&, void*);

template <GameplaySystemFunction InFunction> [[nodiscard]] SystemFunction BindGameplaySystem() noexcept
{
    return +[](SystemContext& InContext, void* InUser)
    {
        GameplaySystemView ScopedView = InContext.GameplayView();
        InFunction(ScopedView, InUser);
    };
}

// Audio view: registry plus the audio system; no physics, render or gameplay.
struct AudioSystemView
{
    Registry& registry;
    AudioServices& audio;
    float dt = 0.0f;
};

using AudioSystemFunction = void (*)(const AudioSystemView&, void*);

template <AudioSystemFunction InFunction> [[nodiscard]] SystemFunction BindAudioSystem() noexcept
{
    return +[](SystemContext& InContext, void* InUser)
    {
        AudioSystemView ScopedView = InContext.AudioView();
        InFunction(ScopedView, InUser);
    };
}

// UI view: registry, render and text bundles plus the accessibility profile that
// affects captions and motion. Gameplay and physics stay out.
struct UiSystemView
{
    Registry& registry;
    JobSystem& jobs;
    RenderServices& render;
    AccessibilityServices& accessibilityServices;
    TextServices& text;
    float dt = 0.0f;
    float uiDt = 0.0f;
    std::uint64_t frameIndex = 0;
};

using UiSystemFunction = void (*)(UiSystemView&, void*);

template <UiSystemFunction InFunction> [[nodiscard]] SystemFunction BindUiSystem() noexcept
{
    return +[](SystemContext& InContext, void* InUser)
    {
        UiSystemView ScopedView = InContext.UiView();
        InFunction(ScopedView, InUser);
    };
}

// Accessibility view: the narrative/caption kernel reads physics contacts, the
// audio mute state and the gameplay tags, so those three bundles are explicit.
struct AccessibilitySystemView
{
    Registry& registry;
    PhysicsServices& physics;
    AudioServices& audio;
    GameplayServices& gameplay;
    float dt = 0.0f;
};

using AccessibilitySystemFunction = void (*)(const AccessibilitySystemView&, void*);

template <AccessibilitySystemFunction InFunction> [[nodiscard]] SystemFunction BindAccessibilitySystem() noexcept
{
    return +[](SystemContext& InContext, void* InUser)
    {
        AccessibilitySystemView ScopedView = InContext.AccessibilityView();
        InFunction(ScopedView, InUser);
    };
}

inline CoreSystemView SystemContext::CoreView() noexcept
{
    return {registry, jobs, *this, dt, frameIndex, interpolationAlpha};
}

inline ConstCoreSystemView SystemContext::ConstCoreView() noexcept
{
    return {registry, jobs, *this, dt, frameIndex, interpolationAlpha};
}

inline PhysicsSystemView SystemContext::PhysicsView() noexcept
{
    return {registry, *this, dt, frameIndex, interpolationAlpha};
}

inline RenderSystemView SystemContext::RenderView() noexcept
{
    return {registry, jobs, *this, *this, dt, interpolationAlpha};
}

inline ConstRenderSystemView SystemContext::ConstRenderView() noexcept
{
    return {registry, jobs, *this, *this, dt, interpolationAlpha};
}

inline GameplaySystemView SystemContext::GameplayView() noexcept
{
    return {registry, jobs, *this, *this, dt, frameIndex};
}

inline AudioSystemView SystemContext::AudioView() noexcept
{
    return {registry, *this, dt};
}

inline UiSystemView SystemContext::UiView() noexcept
{
    return {registry, jobs, *this, *this, *this, dt, uiDt, frameIndex};
}

inline AccessibilitySystemView SystemContext::AccessibilityView() noexcept
{
    return {registry, *this, *this, *this, dt};
}
}
