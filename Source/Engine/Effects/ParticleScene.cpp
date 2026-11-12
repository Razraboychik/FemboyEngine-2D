// FemboyEngine2D - scene particle component and emitter binding system.
#include "Engine/Effects/ParticleScene.hpp"

namespace Fby
{
ParticleEffectSettings ParticleEffectComponent::ToSettings() const noexcept
{
    ParticleEffectSettings ScopedSettings{};
    ScopedSettings.MaxParticles = MaxParticles;
    ScopedSettings.Seed = Seed;
    ScopedSettings.Shape = static_cast<ParticleShape>(Shape);
    ScopedSettings.EmissionShape = static_cast<ParticleEmissionShape>(EmissionShape);
    ScopedSettings.EmissionExtents = EmissionExtents;
    ScopedSettings.Direction = Direction;
    ScopedSettings.Gravity = Gravity;
    ScopedSettings.Wind = Wind;
    ScopedSettings.SpreadRadians = SpreadRadians;
    ScopedSettings.SpeedMin = SpeedMin;
    ScopedSettings.SpeedMax = SpeedMax;
    ScopedSettings.LifetimeMin = LifetimeMin;
    ScopedSettings.LifetimeMax = LifetimeMax;
    ScopedSettings.StartSize = StartSize;
    ScopedSettings.EndSize = EndSize;
    ScopedSettings.StartRotation = StartRotation;
    ScopedSettings.RotationSpeed = RotationSpeed;
    ScopedSettings.Drag = Drag;
    ScopedSettings.Turbulence = Turbulence;
    ScopedSettings.Swirl = Swirl;
    ScopedSettings.EmissionRate = EmissionRate;
    ScopedSettings.BurstCount = BurstCount;
    ScopedSettings.BurstInterval = BurstInterval;
    ScopedSettings.Duration = Duration;
    ScopedSettings.Loop = Loop;
    ScopedSettings.Additive = Additive;
    ScopedSettings.StartColor = StartColor;
    ScopedSettings.EndColor = EndColor;
    return ScopedSettings;
}

void ParticleEffectComponent::FromSettings(const ParticleEffectSettings& InSettings) noexcept
{
    MaxParticles = InSettings.MaxParticles;
    Seed = InSettings.Seed;
    Shape = static_cast<std::uint32_t>(InSettings.Shape);
    EmissionShape = static_cast<std::uint32_t>(InSettings.EmissionShape);
    EmissionExtents = InSettings.EmissionExtents;
    Direction = InSettings.Direction;
    Gravity = InSettings.Gravity;
    Wind = InSettings.Wind;
    SpreadRadians = InSettings.SpreadRadians;
    SpeedMin = InSettings.SpeedMin;
    SpeedMax = InSettings.SpeedMax;
    LifetimeMin = InSettings.LifetimeMin;
    LifetimeMax = InSettings.LifetimeMax;
    StartSize = InSettings.StartSize;
    EndSize = InSettings.EndSize;
    StartRotation = InSettings.StartRotation;
    RotationSpeed = InSettings.RotationSpeed;
    Drag = InSettings.Drag;
    Turbulence = InSettings.Turbulence;
    Swirl = InSettings.Swirl;
    EmissionRate = InSettings.EmissionRate;
    BurstCount = InSettings.BurstCount;
    BurstInterval = InSettings.BurstInterval;
    Duration = InSettings.Duration;
    Loop = InSettings.Loop;
    Additive = InSettings.Additive;
    StartColor = InSettings.StartColor;
    EndColor = InSettings.EndColor;
}

bool FillParticleEffectComponent(ParticleEffectComponent& OutComponent, std::string_view InPresetId) noexcept
{
    const ParticleEffectPreset* ScopedPreset = FindParticleEffectPreset(InPresetId);
    if (ScopedPreset == nullptr)
    {
        return false;
    }
    OutComponent.FromSettings(ScopedPreset->Settings);
    return true;
}

ParticleSceneSystem::ParticleSceneSystem() noexcept
{
    for (Binding& ScopedBinding : MBindings)
    {
        ScopedBinding = Binding{};
    }
}

ParticleSceneSystem::Binding* ParticleSceneSystem::FindBinding(Entity InEntity) noexcept
{
    for (Binding& ScopedBinding : MBindings)
    {
        if (ScopedBinding.Active && ScopedBinding.Entity == InEntity)
        {
            return &ScopedBinding;
        }
    }
    return nullptr;
}

ParticleSceneSystem::Binding* ParticleSceneSystem::AcquireBinding() noexcept
{
    for (Binding& ScopedBinding : MBindings)
    {
        if (!ScopedBinding.Active)
        {
            ScopedBinding = Binding{};
            ScopedBinding.Seen = true;
            return &ScopedBinding;
        }
    }
    return nullptr;
}

void ParticleSceneSystem::StopBinding(Binding& InBinding) noexcept
{
    MParticles.StopEmitter(InBinding.Emitter);
    InBinding = Binding{};
}

void ParticleSceneSystem::Update(Fcs::Registry& InRegistry, float InDeltaSeconds) noexcept
{
    for (Binding& ScopedBinding : MBindings)
    {
        ScopedBinding.Seen = false;
    }

    auto ScopedView = InRegistry.view<ParticleEffectComponent, Transform2D>();
    for (auto [ScopedEntity, ScopedComponent, ScopedTransform] : ScopedView.each())
    {
        if (!ScopedComponent.Playing())
        {
            if (Binding* ScopedBinding = FindBinding(ScopedEntity))
            {
                StopBinding(*ScopedBinding);
            }
            continue;
        }

        Binding* ScopedBinding = FindBinding(ScopedEntity);
        const ParticleEffectSettings ScopedSettings = ScopedComponent.ToSettings();
        if (ScopedBinding == nullptr)
        {
            ScopedBinding = AcquireBinding();
            if (ScopedBinding == nullptr)
            {
                continue;
            }
            ScopedBinding->Entity = ScopedEntity;
            ScopedBinding->Emitter =
                MParticles.Spawn(ScopedSettings, ScopedTransform.Position, ScopedTransform.Rotation);
            if (ScopedBinding->Emitter == kInvalidParticleEmitter)
            {
                ScopedBinding->Entity = kNullEntity;
                ScopedBinding->Active = false;
                continue;
            }
            ScopedBinding->RestartCounter = ScopedComponent.RestartCounter;
            ScopedBinding->Active = true;
            continue;
        }

        ScopedBinding->Seen = true;
        if (ScopedBinding->RestartCounter != ScopedComponent.RestartCounter)
        {
            if (!MParticles.RestartEmitter(ScopedBinding->Emitter, ScopedSettings, ScopedTransform.Position,
                                           ScopedTransform.Rotation))
            {
                ScopedBinding->Emitter =
                    MParticles.Spawn(ScopedSettings, ScopedTransform.Position, ScopedTransform.Rotation);
            }
            ScopedBinding->RestartCounter = ScopedComponent.RestartCounter;
        }
        else
        {
            (void)MParticles.SetEmitterTransform(ScopedBinding->Emitter, ScopedTransform.Position,
                                                 ScopedTransform.Rotation);
        }
    }

    for (Binding& ScopedBinding : MBindings)
    {
        if (ScopedBinding.Active && !ScopedBinding.Seen)
        {
            StopBinding(ScopedBinding);
        }
    }
    MParticles.Update(InDeltaSeconds);
}

std::uint32_t ParticleSceneSystem::BuildRenderItems(std::span<Rhi::RenderItem> Out,
                                                    const ParticleShapeAtlas& InAtlas) const noexcept
{
    return MParticles.BuildRenderItems(Out, InAtlas);
}
}
