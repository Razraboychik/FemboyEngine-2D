#include "Engine/ECS/ComponentRegistry.hpp"

#include "Engine/Audio/Audio.hpp"
#include "Engine/ECS/AnimGraph.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/LightSource.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/ECS/SkeletonAssetRef.hpp"
#include "Engine/ECS/SkeletonIk.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/Navigation/Navigation.hpp"
#include "Engine/ECS/StateTree.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/Physics/Particles.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/Fluid.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"
#include "Engine/Physics/SdfPrimitives.hpp"
#include "Engine/Script/ScriptSystem.hpp"
#include "Engine/UI/Hypertext.hpp"
#include "Engine/UI/PinkUI.hpp"
#include "Engine/Video/VideoStream.hpp"

namespace Fby::Scene
{

const ComponentOps* ComponentRegistry::Find(std::string_view InName) const
{
    const auto ScopedIt = MIndex.find(std::string(InName));
    return ScopedIt != MIndex.end() ? &MOps[ScopedIt->second] : nullptr;
}

std::uint64_t ComponentRegistry::LayoutHashOf(std::string_view InName) const
{
    const ComponentOps* ScopedOps = Find(InName);
    return ScopedOps != nullptr ? ScopedOps->layoutHash : 0;
}

void RegisterBuiltInComponents(ComponentRegistry& InRegistry)
{
    InRegistry.Register<Transform2D>();
    InRegistry.Register<NameComponent>();
    InRegistry.Register<Velocity>();
    InRegistry.Register<Sprite2D>();
    InRegistry.Register<DrawOrder>();
    InRegistry.Register<Camera2D>();
    InRegistry.Register<Lifetime>();
    InRegistry.Register<GameplayState>();
    InRegistry.Register<Light2DComponent>();
    InRegistry.Register<Light2DTargetComponent>();
    InRegistry.Register<Light2DOccluderComponent>();
    InRegistry.Register<SdfSunComponent>();
    InRegistry.Register<LightSourceComponent>();
    InRegistry.Register<CollisionPolicyComponent>();
    InRegistry.Register<RenderMaskComponent>();
    InRegistry.Register<TimeScaleComponent>();
    InRegistry.Register<SleepingStaticComponent>();
    InRegistry.Register<SdfShapeComponent>();
    InRegistry.Register<Navigation::NavAgentSettings>();
    InRegistry.Register<Skeletal::IkGoalComponent>();
    InRegistry.Register<TilemapAssetRefComponent>();
    InRegistry.Register<NarrativeLabelComponent>();
    InRegistry.Register<BigIntComponent>();
    InRegistry.Register<DecimalComponent>();
    InRegistry.Register<TextComponent>();
    InRegistry.Register<LocalizedTextComponent>();
    InRegistry.Register<TextShapeComponent>();
    InRegistry.Register<TextEffectComponent>();
    InRegistry.Register<TextFormatComponent>();
    InRegistry.Register<TextConditionComponent>();
    InRegistry.Register<Hidden>();

    // Persisted components. SkeletonComponent/SkinComponent are
    InRegistry.Register<SkeletonAssetRef>();
    InRegistry.Register<SpriteAnimationComponent>();
    InRegistry.Register<SpriteSheetComponent>();

    // Multimedia ecosystem components. Everything below is a flat POD with a
    InRegistry.Register<ScriptComponent>();
    InRegistry.Register<Video::VideoStreamComponent>();
    InRegistry.Register<Ui::WidgetComponent>();
    InRegistry.Register<AudioBusComponent>();
    InRegistry.Register<AudioSourceComponent>();

    // Physics/media authoring. RigidBody2D/Collider2D stay inspector-editable
    InRegistry.Register<RigidBody2D>(false);
    InRegistry.Register<Collider2D>(false);
    InRegistry.Register<JointChainComponent>();
    InRegistry.Register<JointLinkComponent>(false);
    InRegistry.Register<ClothComponent>(false);
    InRegistry.Register<FluidInteractorComponent>();
    InRegistry.Register<ParticleEmitterComponent>();
    InRegistry.Register<Ui::HypertextComponent>();

    // High-level gameplay authoring. The animation state graph and the State
    InRegistry.Register<AnimStateComponent>(false);
    InRegistry.Register<StateTreeComponent>(false);
    InRegistry.Register<AICommandComponent>(false);
}
}
