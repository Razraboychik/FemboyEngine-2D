/* FemboyEngine2D - safe mod data ABI.
 *
 * This is deliberately a C ABI with opaque handles and deferred commands. It
 * does not expose Registry, C++ component types or pointers into FCS chunks.
 * The trusted plugin ABI in PluginAPI.h remains separate.
 */
#ifndef FBY_MOD_API_H
#define FBY_MOD_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FBY_MOD_API_VERSION 1u
#define FBY_MOD_MAX_QUERY_TYPES 8u
#define FBY_MOD_MAX_EVENT_BYTES 256u
#define FBY_MOD_MAX_COMMAND_BYTES 256u

typedef uint32_t FbyModEntity;
typedef uint32_t FbyModComponentId;
typedef uint32_t FbyModEventId;

enum FbyModResult
{
    FBY_MOD_OK = 0,
    FBY_MOD_INVALID_ARGUMENT = -1,
    FBY_MOD_NOT_FOUND = -2,
    FBY_MOD_WRONG_SIZE = -3,
    FBY_MOD_STALE_ENTITY = -4,
    FBY_MOD_STALE_QUERY = -5,
    FBY_MOD_FULL = -6,
    FBY_MOD_FORBIDDEN = -7
};

typedef struct FbyModQuery
{
    FbyModComponentId types[FBY_MOD_MAX_QUERY_TYPES];
    uint32_t type_count;
    uint64_t cursor;
    uint64_t structural_revision;
    uint32_t flags;
} FbyModQuery;

typedef void (*FbyModEventFn)(void* user, FbyModEventId event, const void* data, size_t size);

typedef struct FbyModAttributeModifier
{
    uint32_t attribute;
    uint8_t op;
    uint8_t reserved[3];
    float magnitude;
} FbyModAttributeModifier;

typedef struct FbyModEffectDef
{
    uint32_t id;
    uint32_t flags;
    float duration;
    float period;
    uint32_t max_stacks;
    uint8_t stack_policy;
    uint8_t reserved[3];
    uint32_t modifier_count;
    FbyModAttributeModifier modifiers[4];
    uint32_t granted_tag_count;
    uint32_t granted_tags[4];
} FbyModEffectDef;

typedef struct FbyModSkillDef
{
    uint32_t id;
    float cooldown;
    float cast_time;
    uint32_t cost_attribute;
    float cost_amount;
    uint32_t self_effect_count;
    uint32_t self_effects[3];
    uint32_t target_effect_count;
    uint32_t target_effects[3];
    uint8_t target_kind;
    uint8_t reserved[3];
    uint32_t animation_tag;
    uint32_t cue_id;
} FbyModSkillDef;

typedef struct FbyModApi
{
    uint32_t struct_size;
    uint32_t abi_version;
    void* user;

    int (*entity_alive)(void* user, FbyModEntity entity);
    FbyModEntity (*entity_create)(void* user);
    int (*entity_destroy)(void* user, FbyModEntity entity);

    int (*component_find)(void* user, const char* name, FbyModComponentId* out_id);
    int (*component_size)(void* user, FbyModComponentId id, uint32_t* out_size);
    int (*component_layout_hash)(void* user, FbyModComponentId id, uint64_t* out_hash);

    /* Copies the value into the caller's buffer; never returns a live pointer. */
    int (*component_read)(void* user, FbyModEntity entity, FbyModComponentId id, void* out_data, size_t capacity,
                          size_t* out_size);
    /* Queues a write; applied only by flush_commands at a safe point. */
    int (*command_write)(void* user, FbyModEntity entity, FbyModComponentId id, const void* data, size_t size);
    int (*command_remove)(void* user, FbyModEntity entity, FbyModComponentId id);
    int (*flush_commands)(void* user);

    int (*query_begin)(void* user, const FbyModComponentId* types, uint32_t type_count, FbyModQuery* out_query);
    int (*query_next)(void* user, FbyModQuery* query, FbyModEntity* out_entity);
    void (*query_end)(void* user, FbyModQuery* query);

    int (*event_register)(void* user, const char* name, FbyModEventId* out_event);
    int (*event_publish)(void* user, FbyModEventId event, const void* data, size_t size);
    int (*event_subscribe)(void* user, FbyModEventId event, FbyModEventFn callback, void* callback_user);

    void (*log)(void* user, int level, const char* message);

    /* Appended data-registration calls; probe with struct_size. */
    int (*register_tag)(void* user, const char* namespaced_path, uint32_t* out_tag_id);
    int (*register_effect)(void* user, const FbyModEffectDef* def);
    int (*register_skill)(void* user, const FbyModSkillDef* def);
} FbyModApi;

/* Capability probe for hosts that append fields in a later ABI revision. */
static inline int FbyModApiHas(const FbyModApi* api, size_t needed_size)
{
    return api != NULL && api->struct_size >= needed_size;
}

#ifdef __cplusplus
}
#endif

#endif
