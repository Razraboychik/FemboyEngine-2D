#include "Engine/ECS/ComponentRegistry.hpp"

#include "Engine/Audio/Audio.hpp"
#include "Engine/ECS/AnimGraph.hpp"
#include "Engine/ECS/BoneAttachment.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/LightSource.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/ECS/PrefabInstance.hpp"
#include "Engine/ECS/SkeletonAssetRef.hpp"
#include "Engine/ECS/SkeletonIk.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/Navigation/Navigation.hpp"
#include "Engine/ECS/StateTree.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/Physics/Particles.hpp"
#include "Engine/Effects/ParticleScene.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/Fluid.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/AI/AIController.hpp"
#include "Engine/Gameplay/GameplayAttributes.hpp"
#include "Engine/Gameplay/GameplayEffects.hpp"
#include "Engine/Gameplay/GameplaySkills.hpp"
#include "Engine/Gameplay/GameplayTags.hpp"
#include "Engine/Physics/Gravity.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"
#include "Engine/Physics/PhysicsJointsScene.hpp"
#include "Engine/Physics/SdfCharacter.hpp"
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

bool ComponentRegistry::SetTomlCodec(std::string_view InName, TomlCapture InCapture, TomlRestore InRestore)
{
    const auto ScopedIt = MIndex.find(std::string(InName));
    if (ScopedIt == MIndex.end())
    {
        return false;
    }
    MOps[ScopedIt->second].captureToml = InCapture;
    MOps[ScopedIt->second].restoreToml = InRestore;
    return true;
}

void RegisterBuiltInComponents(ComponentRegistry& InRegistry)
{
    InRegistry.Register<Transform2D>();
    InRegistry.Register<LocalTransform2D>();
    InRegistry.Register<Parent2D>();
    InRegistry.Register<NameComponent>();
    InRegistry.Register<Velocity>();
    InRegistry.Register<TransportGroupComponent>();
    InRegistry.Register<CarrierMemberComponent>();
    InRegistry.Register<TransportMotion>();
    InRegistry.Register<TransportSurface>();
    InRegistry.Register<TransportPath>();
    InRegistry.Register<Vehicle2D>();
    InRegistry.Register<InputUserComponent>();
    InRegistry.Register<Wheel2D>();
    InRegistry.Register<Sprite2D>();
    InRegistry.Register<SpriteOverlay2D>();
    // The attachment points at a runtime skeleton entity, so scene TOML keeps
    // it out of the serialized set until entity references get stable ids.
    InRegistry.Register<BoneAttachment2D>(false);
    InRegistry.Register<DrawOrder>();
    InRegistry.Register<Camera2D>();
    InRegistry.Register<Lifetime>();
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
    InRegistry.Register<SdfCharacterComponent>(false);
    InRegistry.Register<Navigation::NavAgentSettings>();
    InRegistry.Register<Navigation::NavAgentPathSettings>();
    InRegistry.Register<LevelGravityComponent>();
    InRegistry.Register<GravityBodyComponent>();
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
    InRegistry.Register<PrefabInstanceComponent>();
    // Transient child marker: rebuilt from PrefabInstanceComponent on load.
    InRegistry.Register<PrefabInstanceMemberComponent>(false);

    // Persisted components. SkeletonComponent/SkinComponent.
    InRegistry.Register<SkeletonAssetRef>();
    InRegistry.Register<SpriteAnimationComponent>();
    InRegistry.Register<SpriteSheetComponent>();

    // Multimedia ecosystem components. Everything below is a flat POD.
    InRegistry.Register<ScriptComponent>();
    InRegistry.Register<Video::VideoStreamComponent>();
    InRegistry.Register<Ui::WidgetComponent>();
    InRegistry.Register<AudioBusComponent>();
    InRegistry.Register<AudioSourceComponent>();

    // Physics/media authoring. RigidBody2D/Collider2D stay inspector-editable
    InRegistry.Register<RigidBody2D>(false);
    InRegistry.Register<Collider2D>(false);
    // Gameplay runtime state is simulation data, not scene authoring data: it
    // travels in snapshots and is registered for the inspector but not TOML.
    InRegistry.Register<GameplayTagsComponent>(false);
    InRegistry.Register<GameplayAttributesComponent>(false);
    InRegistry.Register<GameplayEffectsComponent>(false);
    InRegistry.Register<GameplaySkillsComponent>(false);
    InRegistry.Register<AI::AIAgentSettings>();
    InRegistry.Register<AI::AIControllerComponent>(false);
    InRegistry.Register<AI::AIPerceptionSettings>();
    InRegistry.Register<AI::AIPerceptionComponent>(false);
    InRegistry.Register<JointChainComponent>();
    // Fixed arrays (Path, Sections) need their own TOML codec; reflection only
    // covers the scalar fields.
    (void)InRegistry.SetTomlCodec("JointChainComponent", &CaptureJointChainToml, &RestoreJointChainToml);
    InRegistry.Register<JointLinkComponent>(false);
    InRegistry.Register<ClothComponent>(false);
    InRegistry.Register<FluidInteractorComponent>();
    InRegistry.Register<ParticleEmitterComponent>();
    InRegistry.Register<ParticleEffectComponent>();
    InRegistry.Register<Ui::HypertextComponent>();

    // High-level gameplay authoring. The animation state graph and the State
    InRegistry.Register<AnimStateComponent>(false);
    InRegistry.Register<StateTreeComponent>(false);
    InRegistry.Register<AICommandComponent>(false);
}
}
