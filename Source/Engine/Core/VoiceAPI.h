/* FemboyEngine2D - optional voice preset API for plugins.
 *
 * The table is append-only: the host exposes it through a nullable pointer at
 * the end of FbyHostApi, so adding voice support does not change the plugin ABI
 * version. A plugin detects the service with FbyHostHasVoice() and degrades
 * when the host has no voice subsystem.
 *
 * The plugin supplies data only. Every descriptor is copied by the host before
 * the call returns, so no pointer into plugin memory survives the entry point.
 */
#ifndef FBY_VOICE_API_H
#define FBY_VOICE_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FBY_VOICE_API_VERSION 1u
#define FBY_VOICE_SLOT_COUNT 8

    /* Mirrors Fby::VoiceEffectKind. */
    typedef enum FbyVoiceEffectKind
    {
        FBY_VOICE_EFFECT_NONE = 0,
        FBY_VOICE_EFFECT_MUTE = 1,
        FBY_VOICE_EFFECT_MUFFLE = 2,
        FBY_VOICE_EFFECT_UNDERWATER = 3,
        FBY_VOICE_EFFECT_RADIO = 4,
        FBY_VOICE_EFFECT_PITCH = 5,
        FBY_VOICE_EFFECT_FORMANT = 6,
        FBY_VOICE_EFFECT_DISTORTION = 7,
        FBY_VOICE_EFFECT_RING_MODULATION = 8,
        FBY_VOICE_EFFECT_WHISPER = 9
    } FbyVoiceEffectKind;

    /* Mirrors Fby::VoicePresetCategory. */
    typedef enum FbyVoicePresetCategory
    {
        FBY_VOICE_CATEGORY_NONE = 0,
        FBY_VOICE_CATEGORY_ENVIRONMENT = 1,
        FBY_VOICE_CATEGORY_MODERATION = 2,
        FBY_VOICE_CATEGORY_COSMETIC = 3,
        FBY_VOICE_CATEGORY_CHARACTER = 4,
        FBY_VOICE_CATEGORY_SCENE = 5
    } FbyVoicePresetCategory;

#define FBY_VOICE_FLAG_COSMETIC 1u
#define FBY_VOICE_FLAG_MODERATION 2u
#define FBY_VOICE_FLAG_ALLOW_UPLINK 4u
#define FBY_VOICE_FLAG_ALLOW_DOWNLINK 8u

    typedef struct FbyVoiceMuteDesc
    {
        float fade_seconds;
    } FbyVoiceMuteDesc;

    typedef struct FbyVoiceMuffleDesc
    {
        float low_cut_hz;
        float high_cut_hz;
        float resonance;
        float drive_db;
        float wobble_hz;
        float wobble_depth;
        float breath_level;
    } FbyVoiceMuffleDesc;

    typedef struct FbyVoiceUnderwaterDesc
    {
        float low_cut_hz;
        float high_cut_hz;
        float resonance;
        float wow_hz;
        float wow_depth;
    } FbyVoiceUnderwaterDesc;

    typedef struct FbyVoiceRadioDesc
    {
        float low_cut_hz;
        float high_cut_hz;
        float static_level;
        float crackle_rate;
        float squelch;
        float drive_db;
        uint32_t mute_when_squelched;
    } FbyVoiceRadioDesc;

    typedef struct FbyVoicePitchDesc
    {
        float semitones;
        float window_ms;
    } FbyVoicePitchDesc;

    typedef struct FbyVoiceFormantDesc
    {
        float semitones;
        float strength_db;
    } FbyVoiceFormantDesc;

    typedef struct FbyVoiceDistortionDesc
    {
        float drive_db;
        float low_cut_hz;
        float high_cut_hz;
        float bit_depth;
    } FbyVoiceDistortionDesc;

    typedef struct FbyVoiceRingModulationDesc
    {
        float frequency_hz;
        float depth;
    } FbyVoiceRingModulationDesc;

    typedef struct FbyVoiceWhisperDesc
    {
        float noise_level;
        float low_cut_hz;
        float high_cut_hz;
    } FbyVoiceWhisperDesc;

    typedef struct FbyVoiceEffectSlotDesc
    {
        uint32_t kind; /* FbyVoiceEffectKind */
        float mix;
        FbyVoiceMuteDesc mute;
        FbyVoiceMuffleDesc muffle;
        FbyVoiceUnderwaterDesc underwater;
        FbyVoiceRadioDesc radio;
        FbyVoicePitchDesc pitch;
        FbyVoiceFormantDesc formant;
        FbyVoiceDistortionDesc distortion;
        FbyVoiceRingModulationDesc ring_modulation;
        FbyVoiceWhisperDesc whisper;
    } FbyVoiceEffectSlotDesc;

    typedef struct FbyVoicePresetDesc
    {
        uint32_t struct_size;
        const char* id;
        const char* name_key;
        uint32_t category;
        uint32_t flags;
        float min_intensity;
        float max_intensity;
        uint32_t enable_conditions;
        uint32_t slot_count;
        FbyVoiceEffectSlotDesc slots[FBY_VOICE_SLOT_COUNT];
    } FbyVoicePresetDesc;

    typedef struct FbyVoiceApi
    {
        uint32_t struct_size;
        uint32_t abi_version;
        /* Returns 1 on success. The host copies the descriptor; the ids are
         * owned by the caller and may be freed after the call. */
        int (*register_preset)(void* user, const FbyVoicePresetDesc* preset);
    } FbyVoiceApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
