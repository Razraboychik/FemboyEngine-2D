// FemboyEngine2D - Voice FX Pack: ready-made voice presets as data.
#include "Engine/Core/PluginAPI.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace
{
FbyVoiceEffectSlotDesc MakePitchSlot(float InSemitones, float InWindowMs, float InMix)
{
    FbyVoiceEffectSlotDesc ScopedSlot{};
    ScopedSlot.kind = FBY_VOICE_EFFECT_PITCH;
    ScopedSlot.mix = InMix;
    ScopedSlot.pitch.semitones = InSemitones;
    ScopedSlot.pitch.window_ms = InWindowMs;
    return ScopedSlot;
}

FbyVoiceEffectSlotDesc MakeFormantSlot(float InSemitones, float InStrengthDb, float InMix)
{
    FbyVoiceEffectSlotDesc ScopedSlot{};
    ScopedSlot.kind = FBY_VOICE_EFFECT_FORMANT;
    ScopedSlot.mix = InMix;
    ScopedSlot.formant.semitones = InSemitones;
    ScopedSlot.formant.strength_db = InStrengthDb;
    return ScopedSlot;
}

FbyVoiceEffectSlotDesc MakeUnderwaterSlot(float InMix)
{
    FbyVoiceEffectSlotDesc ScopedSlot{};
    ScopedSlot.kind = FBY_VOICE_EFFECT_UNDERWATER;
    ScopedSlot.mix = InMix;
    ScopedSlot.underwater.low_cut_hz = 160.0f;
    ScopedSlot.underwater.high_cut_hz = 560.0f;
    ScopedSlot.underwater.resonance = 1.2f;
    ScopedSlot.underwater.wow_hz = 0.8f;
    ScopedSlot.underwater.wow_depth = 0.25f;
    return ScopedSlot;
}

FbyVoiceEffectSlotDesc MakeMuffleSlot(float InMix)
{
    FbyVoiceEffectSlotDesc ScopedSlot{};
    ScopedSlot.kind = FBY_VOICE_EFFECT_MUFFLE;
    ScopedSlot.mix = InMix;
    ScopedSlot.muffle.low_cut_hz = 150.0f;
    ScopedSlot.muffle.high_cut_hz = 800.0f;
    ScopedSlot.muffle.resonance = 1.0f;
    ScopedSlot.muffle.drive_db = 4.0f;
    ScopedSlot.muffle.wobble_hz = 2.5f;
    ScopedSlot.muffle.wobble_depth = 0.2f;
    ScopedSlot.muffle.breath_level = 0.06f;
    return ScopedSlot;
}

FbyVoiceEffectSlotDesc MakeDistortionSlot(float InDriveDb, float InBitDepth, float InMix)
{
    FbyVoiceEffectSlotDesc ScopedSlot{};
    ScopedSlot.kind = FBY_VOICE_EFFECT_DISTORTION;
    ScopedSlot.mix = InMix;
    ScopedSlot.distortion.drive_db = InDriveDb;
    ScopedSlot.distortion.low_cut_hz = 180.0f;
    ScopedSlot.distortion.high_cut_hz = 5000.0f;
    ScopedSlot.distortion.bit_depth = InBitDepth;
    return ScopedSlot;
}

FbyVoicePresetDesc MakePreset(const char* InId, const char* InNameKey, std::uint32_t InCategory, std::uint32_t InFlags,
                              const FbyVoiceEffectSlotDesc* InSlots, std::uint32_t InSlotCount)
{
    FbyVoicePresetDesc ScopedPreset{};
    ScopedPreset.struct_size = static_cast<std::uint32_t>(sizeof(FbyVoicePresetDesc));
    ScopedPreset.id = InId;
    ScopedPreset.name_key = InNameKey;
    ScopedPreset.category = InCategory;
    ScopedPreset.flags = InFlags;
    ScopedPreset.min_intensity = 0.0f;
    ScopedPreset.max_intensity = 1.0f;
    ScopedPreset.enable_conditions = 1;
    ScopedPreset.slot_count = InSlotCount;
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < InSlotCount; ++ScopedSlot)
    {
        ScopedPreset.slots[ScopedSlot] = InSlots[ScopedSlot];
    }
    return ScopedPreset;
}
}

FBY_PLUGIN_ENTRY const FbyPluginInfo* fby_plugin_info()
{
    static const FbyPluginInfo ScopedInfo{static_cast<std::uint32_t>(sizeof(FbyPluginInfo)),
                                          FBY_PLUGIN_ABI_VERSION,
                                          "Voice FX Pack",
                                          FBY_ENGINE_VERSION,
                                          "FemboyEngine2D contributors",
                                          0,
                                          nullptr,
                                          "Ready-made voice presets registered through FbyVoiceApi"};
    return &ScopedInfo;
}

FBY_PLUGIN_ENTRY int fby_plugin_load(FbyHostApi* InHost)
{
    if (InHost == nullptr || InHost->abi_version != FBY_PLUGIN_ABI_VERSION || InHost->struct_size < sizeof(FbyHostApi))
    {
        return 1;
    }
    if (!FbyHostHasVoice(InHost) || InHost->voice->register_preset == nullptr)
    {
        if (InHost->log != nullptr)
        {
            InHost->log(InHost->user, FBY_LOG_INFO, "voice-pack", "host has no voice preset registry; pack inactive");
        }
        return 0;
    }

    const std::array<FbyVoiceEffectSlotDesc, 2> ScopedHelium = {MakePitchSlot(12.0f, 18.0f, 1.0f),
                                                                MakeFormantSlot(12.0f, 9.0f, 0.9f)};
    const std::array<FbyVoiceEffectSlotDesc, 2> ScopedSwamp = {MakeUnderwaterSlot(1.0f), MakeMuffleSlot(0.35f)};
    const std::array<FbyVoiceEffectSlotDesc, 2> ScopedCursed = {MakeMuffleSlot(1.0f),
                                                                MakeDistortionSlot(18.0f, 6.0f, 0.35f)};
    const std::array<FbyVoicePresetDesc, 3> ScopedPresets = {
        MakePreset("Pack.Voice.Helium", "voice.pack.helium", FBY_VOICE_CATEGORY_CHARACTER,
                   FBY_VOICE_FLAG_COSMETIC | FBY_VOICE_FLAG_ALLOW_UPLINK | FBY_VOICE_FLAG_ALLOW_DOWNLINK,
                   ScopedHelium.data(), static_cast<std::uint32_t>(ScopedHelium.size())),
        MakePreset("Pack.Voice.Swamp", "voice.pack.swamp", FBY_VOICE_CATEGORY_ENVIRONMENT,
                   FBY_VOICE_FLAG_COSMETIC | FBY_VOICE_FLAG_ALLOW_UPLINK | FBY_VOICE_FLAG_ALLOW_DOWNLINK,
                   ScopedSwamp.data(), static_cast<std::uint32_t>(ScopedSwamp.size())),
        MakePreset("Pack.Voice.CursedMumble", "voice.pack.cursed-mumble", FBY_VOICE_CATEGORY_COSMETIC,
                   FBY_VOICE_FLAG_COSMETIC | FBY_VOICE_FLAG_ALLOW_UPLINK | FBY_VOICE_FLAG_ALLOW_DOWNLINK,
                   ScopedCursed.data(), static_cast<std::uint32_t>(ScopedCursed.size())),
    };
    for (const FbyVoicePresetDesc& ScopedPreset : ScopedPresets)
    {
        if (InHost->voice->register_preset(InHost->user, &ScopedPreset) == 0 && InHost->log != nullptr)
        {
            InHost->log(InHost->user, FBY_LOG_WARN, "voice-pack", "a preset was rejected by the host");
        }
    }
    return 0;
}

FBY_PLUGIN_ENTRY void fby_plugin_unload()
{
}
