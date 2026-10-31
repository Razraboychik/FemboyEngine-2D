# FemboyEngine2D - test suite.

function(fby_link_asan target)
    # One shared rule keeps the sanitizer flags of the tests identical to the
    # engine and the editor.
    if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
        fby_asan_apply(${target})
    endif()
endfunction()

function(fby_add_engine_test target test_name source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE FemboyEngine2D::Engine)
    if(MSVC)
        target_link_options(${target} PRIVATE /STACK:16777216)
    endif()
    fby_link_asan(${target})
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

# Rollback replay: snapshot v3 round-trip and deterministic resimulation.
add_executable(fby_rollback_replay_test RollbackReplayTest.cpp)
target_link_libraries(fby_rollback_replay_test PRIVATE FemboyEngine2D::Engine)
fby_link_asan(fby_rollback_replay_test)
target_compile_definitions(fby_rollback_replay_test PRIVATE FBY_ROLLBACK_TEST=1)
set_target_properties(fby_rollback_replay_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME rollback_replay COMMAND fby_rollback_replay_test)

# Fixed-width big integers: two's complement arithmetic, conversion and formatting.
fby_add_engine_test(fby_bigint_test bigint BigIntTest.cpp)

# Fixed-capacity big number store: stable handles, generation checks and pool reuse.
fby_add_engine_test(fby_bignumber_store_test bignumber_store BigNumberStoreTest.cpp)

# Fixed-width signed decimals: parsing, arithmetic, rounding and scientific text.
fby_add_engine_test(fby_decimal_test decimal DecimalTest.cpp)

# Long text: block storage, UTF-8 boundaries and greedy wrapping.
# Clocks, timer pools, frame timing and calendar formatting.
fby_add_engine_test(fby_time_test time TimeTest.cpp)
# Job pool: exception isolation and shutdown drain.
fby_add_engine_test(fby_job_system_test job_system JobSystemTest.cpp)
# System graph: dependency order, cycle rejection, mask validation, removal.
fby_add_engine_test(fby_system_graph_test system_graph SystemGraphTest.cpp)
# Parser mutation sweep: input packet, .femarc and the FbyLz codec.
fby_add_engine_test(fby_fuzz_parsers_test fuzz_parsers FuzzParsersTest.cpp)
# Noise handshake and record mutation sweep.
fby_add_engine_test(fby_fuzz_noise_test fuzz_noise FuzzNoiseTest.cpp)
# Compiled-in toolchain and dependency metadata.
fby_add_engine_test(fby_build_info_test build_info BuildInfoTest.cpp)
# Safe-area layout and resolved widget rects.
fby_add_engine_test(fby_pinkui_safe_area_test pinkui_safe_area PinkUISafeAreaTest.cpp)
# Vertical scroll controller over a clipped panel.
fby_add_engine_test(fby_pinkui_scrollview_test pinkui_scrollview PinkUIScrollViewTest.cpp)
# Tab strip over a Switcher.
fby_add_engine_test(fby_pinkui_tabs_test pinkui_tabs PinkUITabsTest.cpp)
# Dropdown/ComboBox over a trigger and popup.
fby_add_engine_test(fby_pinkui_dropdown_test pinkui_dropdown PinkUIDropdownTest.cpp)
# Virtualised list rows.
fby_add_engine_test(fby_pinkui_listview_test pinkui_listview PinkUIListViewTest.cpp)
# Radial progress ring painted through the custom-quad path.
fby_add_engine_test(fby_pinkui_radial_test pinkui_radial PinkUIRadialTest.cpp)
# Tooltip auto-placement and edge flipping.
fby_add_engine_test(fby_pinkui_tooltip_test pinkui_tooltip PinkUITooltipTest.cpp)
# Widget localization keys applied from a localization table.
fby_add_engine_test(fby_pinkui_localization_test pinkui_localization PinkUILocalizationTest.cpp)
# Widget layout TOML round-trip.
fby_add_engine_test(fby_pinkui_layout_asset_test pinkui_layout_asset PinkUILayoutAssetTest.cpp)
# IME-committed text insertion in a TextInput.
fby_add_engine_test(fby_pinkui_ime_test pinkui_ime PinkUIImeTest.cpp)
# Pak-backed VFS entries refuse invalidation; loose files do not.
fby_add_engine_test(fby_vfs_pak_test vfs_pak VfsPakTest.cpp)
# Font and texture hot reload while the job system runs.
fby_add_engine_test(fby_hot_reload_test hot_reload HotReloadTest.cpp)
# Opt-in parent/child transform composition.
fby_add_engine_test(fby_hierarchy_test hierarchy HierarchyTest.cpp)
# Per-instance prefab overrides.
fby_add_engine_test(fby_prefab_override_test prefab_overrides PrefabOverrideTest.cpp)

# Plugin hot reload needs the editor-gated plugin fixtures.
if(TARGET fby_hello_mod)
    fby_add_engine_test(fby_plugin_reload_test plugin_reload PluginReloadTest.cpp)
    target_compile_definitions(fby_plugin_reload_test PRIVATE
        FBY_TEST_PLUGIN_PATH="$<TARGET_FILE:fby_hello_mod>"
        FBY_TEST_BAD_PLUGIN_PATH="$<TARGET_FILE:fby_hello_mod_bad>")
endif()
# Runtime storage layout, log file sink, rotation and crash reports.
fby_add_engine_test(fby_storage_test storage StorageTest.cpp)
fby_add_engine_test(fby_text_test text TextTest.cpp)

# Text extraction: font atlas glyphs become 48-byte render items.
fby_add_engine_test(fby_text_render_test text_render TextRenderTest.cpp)

# Text placeholders: BigInt/Decimal arguments formatted without allocation.
fby_add_engine_test(fby_text_format_test text_format TextFormatTest.cpp)

# Rich text markup: style spans compiled from a bounded fixed buffer.
fby_add_engine_test(fby_text_markup_test text_markup TextMarkupTest.cpp)

# Cooked text asset: header, block table, UTF-8 payload and corruption checks.
fby_add_engine_test(fby_text_asset_format_test text_asset_format TextAssetFormatTest.cpp)

# Cooked text asset from the VFS: zero-copy mapping and block access.
fby_add_engine_test(fby_text_asset_file_test text_asset_file TextAssetFileTest.cpp)

# Runtime TTF -> .fbyfont builder and style slots.
fby_add_engine_test(fby_font_builder_test font_builder FontBuilderTest.cpp)

# Shaping/bidi boundary: glyph clusters and conservative RTL segmentation.
fby_add_engine_test(fby_text_shaper_test text_shaper TextShaperTest.cpp)

# Fixed-capacity ECS/DOD shape cache.
fby_add_engine_test(fby_text_shape_cache_test text_shape_cache TextShapeCacheTest.cpp)

# ECS system filling the DOD shape cache from TextComponent.
fby_add_engine_test(fby_text_shape_system_test text_shape_system TextShapeSystemTest.cpp)
# CJK glyph paging: page table, on-demand pages, memory bound and eviction.
fby_add_engine_test(fby_font_paging_test font_paging FontPagingTest.cpp)
# Coverage-ranked script families: automatic face selection and fallback.
fby_add_engine_test(fby_text_coverage_test text_coverage TextCoverageTest.cpp)
# Hot path: a cache hit must not shape and must not allocate.
fby_add_engine_test(fby_text_hot_path_test text_hot_path TextHotPathTest.cpp)
# Golden shaping for every mounted script: exact glyph ids and advances.
fby_add_engine_test(fby_text_golden_test text_golden TextGoldenTest.cpp)
# Text family formatting: one family, every style slot, decorations.
fby_add_engine_test(fby_text_style_test text_style TextStyleTest.cpp)

# Script-visible big numbers: float token handles and native arithmetic.
fby_add_engine_test(fby_script_bigint_test script_bigint ScriptBigIntTest.cpp)


# PCG32 reference vectors, OS entropy and the Perlin/Simplex/Worley fields.
fby_add_engine_test(fby_rng_noise_test rng_noise RngNoiseTest.cpp)

# Network rollback: stable ids, input prediction, late-input replay and the
fby_add_engine_test(fby_net_rollback_test net_rollback NetRollbackTest.cpp)

# UDP transport: loopback datagrams, reliability, fragmentation and peers.
fby_add_engine_test(fby_transport_test transport TransportTest.cpp)

# Input packets: codec validation and the transport/session bridge on loopback.
fby_add_engine_test(fby_net_protocol_test net_protocol NetProtocolTest.cpp)

# Network primitives: BLAKE2b/HMAC/HKDF reference vectors, X25519 and EdDSA.
fby_add_engine_test(fby_crypto_primitives_test crypto_primitives CryptoPrimitivesTest.cpp)

# Canonical SHA-256, HMAC/HKDF-SHA-256 and XChaCha20-Poly1305 vectors.
fby_add_engine_test(fby_crypto_hash_test crypto_hash CryptoHashTest.cpp)

# Identity record, signed tickets, expiry and invite codes.
fby_add_engine_test(fby_net_identity_test net_identity NetIdentityTest.cpp)
# Identity storage: v2 container, DPAPI/0600, legacy v1 migration.
fby_add_engine_test(fby_identity_storage_test identity_storage IdentityStorageTest.cpp)

# Noise XX handshake, cookies, replay window, rekey and record framing.
fby_add_engine_test(fby_secure_channel_test secure_channel SecureChannelTest.cpp)

# Protocol v2 record layer: epoch header, Noise REKEY chain, reorder and tamper.
fby_add_engine_test(fby_record_layer_v2_test record_layer_v2 RecordLayerV2Test.cpp)

# Official noise-c XX vector: handshake messages, hash and split keys.
fby_add_engine_test(fby_noise_conformance_test noise_conformance NoiseConformanceTest.cpp)

# Secure endpoint over the loopback UDP transport.
fby_add_engine_test(fby_secure_endpoint_test secure_endpoint SecureEndpointTest.cpp)

# Secure session transport: per-client endpoints, roster addresses and events.
fby_add_engine_test(fby_secure_session_test secure_session SecureSessionTest.cpp)
fby_add_engine_test(fby_secure_auth_test secure_auth SecureAuthTest.cpp)
# FBY XX handshake KATs: standard initial hash, FBY prologue and a deterministic
# transcript.
fby_add_engine_test(fby_noise_kats_test noise_kats NoiseKatsTest.cpp)
# Signed ticket hash bound into the handshake prologue, plus server substitution.
fby_add_engine_test(fby_ticket_binding_test ticket_binding TicketBindingTest.cpp)
# Handshake token bucket and per-speaker voice flood protection.
add_executable(fby_rate_limit_test RateLimitTest.cpp)
target_link_libraries(fby_rate_limit_test PRIVATE FemboyEngine2D::Engine)
if(TARGET Opus::opus)
    target_link_libraries(fby_rate_limit_test PRIVATE Opus::opus)
endif()
set_target_properties(fby_rate_limit_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
# The transport fixtures put several large channels on the stack; sanitizer
# builds inflate those frames past the default 1 MiB, so give the test room.
if(MSVC)
    target_link_options(fby_rate_limit_test PRIVATE "/STACK:16777216")
endif()
fby_link_asan(fby_rate_limit_test)
add_test(NAME rate_limit COMMAND fby_rate_limit_test)
# Unified session: role resolution, slots, time sync, ticks and events.
fby_add_engine_test(fby_net_session_test net_session NetSessionTest.cpp)
# Snapshot replication: baselines, chunks, checksums, budget and interpolation.
fby_add_engine_test(fby_net_replication_test net_replication NetReplicationTest.cpp)
# ECS replication glue: baseline, delta, restore and relevance filtering.
fby_add_engine_test(fby_net_replication_ecs_test net_replication_ecs NetReplicationEcsTest.cpp)

# Sandbox PIE client: argument parsing, loopback snapshot receive and timeout exit.
add_executable(fby_sandbox_client_test SandboxClientTest.cpp
    "${FBY_SOURCE_ROOT}/Apps/Sandbox/PieClient.cpp"
    "${FBY_SOURCE_ROOT}/Apps/Sandbox/SandboxOptions.cpp")
target_include_directories(fby_sandbox_client_test PRIVATE
    "${FBY_SOURCE_ROOT}/Apps/Sandbox")
target_link_libraries(fby_sandbox_client_test PRIVATE FemboyEngine2D::Engine)
if(TARGET fby_sandbox)
    add_dependencies(fby_sandbox_client_test fby_sandbox)
    target_compile_definitions(fby_sandbox_client_test PRIVATE FBY_SANDBOX_AVAILABLE=1)
endif()
set_target_properties(fby_sandbox_client_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
fby_link_asan(fby_sandbox_client_test)
add_test(NAME sandbox_client COMMAND fby_sandbox_client_test)

# Voice budget: codec bitrate, DSP and AEC CPU on a ten-second workload.
add_executable(fby_voice_budget_test VoiceBudgetTest.cpp)
target_link_libraries(fby_voice_budget_test PRIVATE FemboyEngine2D::Engine)
if(TARGET Opus::opus)
    target_link_libraries(fby_voice_budget_test PRIVATE Opus::opus)
endif()
if(TARGET fby::rnnoise)
    target_link_libraries(fby_voice_budget_test PRIVATE fby::rnnoise)
endif()
set_target_properties(fby_voice_budget_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
fby_link_asan(fby_voice_budget_test)
add_test(NAME voice_budget COMMAND fby_voice_budget_test)
# Lobby: rooms, ticket issuance and match start.
fby_add_engine_test(fby_net_lobby_test net_lobby NetLobbyTest.cpp)
# Audio devices: fallback policy, enumeration and the capture meter.
fby_add_engine_test(fby_audio_devices_test audio_devices AudioDevicesTest.cpp)
# Device ids: persisted hash, full raw id and collision resolution.
fby_add_engine_test(fby_device_id_test device_id DeviceIdTest.cpp)
# Voice pipeline: sample ring, live stream voices and session policy.
fby_add_engine_test(fby_voice_pipeline_test voice_pipeline VoicePipelineTest.cpp)
# Sound cues: registry, cooldowns, DeafMode captions and BlindMode narrative.
fby_add_engine_test(fby_sound_cues_test sound_cues SoundCuesTest.cpp)

# Voice DSP: DC/HPF, gate, spectral denoise, AGC, limiter and presets.
add_executable(fby_voice_dsp_test VoiceDspTest.cpp)
target_link_libraries(fby_voice_dsp_test PRIVATE FemboyEngine2D::Engine)
if(TARGET fby::rnnoise)
    target_link_libraries(fby_voice_dsp_test PRIVATE fby::rnnoise)
endif()
set_target_properties(fby_voice_dsp_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME voice_dsp COMMAND fby_voice_dsp_test)

# Echo canceller: delay lock, NLMS convergence and double-talk protection.
fby_add_engine_test(fby_voice_aec_test voice_aec VoiceAecTest.cpp)

# Voice runtime: capture, DSP, channel and mixer binding.
add_executable(fby_voice_system_test VoiceSystemTest.cpp)
target_link_libraries(fby_voice_system_test PRIVATE FemboyEngine2D::Engine)
if(TARGET Opus::opus)
    target_link_libraries(fby_voice_system_test PRIVATE Opus::opus)
endif()
if(TARGET fby::rnnoise)
    target_link_libraries(fby_voice_system_test PRIVATE fby::rnnoise)
endif()
set_target_properties(fby_voice_system_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME voice_system COMMAND fby_voice_system_test)

# Encrypted relay voice: server + two clients with VoiceNetworkBridge.
add_executable(fby_voice_bridge_test VoiceBridgeTest.cpp)
target_link_libraries(fby_voice_bridge_test PRIVATE FemboyEngine2D::Engine)
if(TARGET Opus::opus)
    target_link_libraries(fby_voice_bridge_test PRIVATE Opus::opus)
endif()
if(TARGET fby::rnnoise)
    target_link_libraries(fby_voice_bridge_test PRIVATE fby::rnnoise)
endif()
set_target_properties(fby_voice_bridge_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME voice_bridge COMMAND fby_voice_bridge_test)

# Four-player call over UDP relay plus live device and tier switching.
add_executable(fby_voice_call_test VoiceCallTest.cpp)
target_link_libraries(fby_voice_call_test PRIVATE FemboyEngine2D::Engine)
if(TARGET Opus::opus)
    target_link_libraries(fby_voice_call_test PRIVATE Opus::opus)
endif()
if(TARGET fby::rnnoise)
    target_link_libraries(fby_voice_call_test PRIVATE fby::rnnoise)
endif()
set_target_properties(fby_voice_call_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME voice_call COMMAND fby_voice_call_test)

# Voice network: Opus codec, packets, jitter, relay/mesh and ducking.
add_executable(fby_voice_network_test VoiceNetworkTest.cpp)
target_link_libraries(fby_voice_network_test PRIVATE FemboyEngine2D::Engine)
if(TARGET Opus::opus)
    target_link_libraries(fby_voice_network_test PRIVATE Opus::opus)
endif()
set_target_properties(fby_voice_network_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME voice_network COMMAND fby_voice_network_test)

# RNNoise wet/dry, Opus in-band FEC and the first-packet jitter policy.
add_executable(fby_voice_quality_test VoiceQualityTest.cpp)
target_link_libraries(fby_voice_quality_test PRIVATE FemboyEngine2D::Engine)
if(TARGET Opus::opus)
    target_link_libraries(fby_voice_quality_test PRIVATE Opus::opus)
endif()
if(TARGET fby::rnnoise)
    target_link_libraries(fby_voice_quality_test PRIVATE fby::rnnoise)
endif()
set_target_properties(fby_voice_quality_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME rnnoise_mix COMMAND fby_voice_quality_test rnnoise_mix)
add_test(NAME voice_fec COMMAND fby_voice_quality_test voice_fec)
add_test(NAME jitter_init COMMAND fby_voice_quality_test jitter_init)

# Shipped attribution: the generated Fby::Notices table and its license texts.
fby_add_engine_test(fby_notices_test notices NoticesTest.cpp)

# Nativized programs: AOT manifest, cook gate and the VM-free dispatch path.
fby_add_engine_test(fby_aot_host_test aot_host AotHostTest.cpp)

# Virtual texture address contract: page table, slot grid, tile math, feedback.
fby_add_engine_test(fby_vt_format_test vt_format VirtualTextureFormatTest.cpp)

# Virtual texture GPU bridge: tile mapping and the Null backend lifecycle.
fby_add_engine_test(fby_vt_gpu_test vt_gpu VirtualTextureGpuTest.cpp)

# Virtual texture streaming: budgeted uploads, eviction and page-table batches.
fby_add_engine_test(fby_vt_streaming_test vt_streaming VirtualTextureStreamingTest.cpp)

# Vulkan-only; returns 0 when no Vulkan device exists so headless CI stays green.
fby_add_engine_test(fby_mesh_indirect_smoke_test mesh_indirect_smoke MeshIndirectSmoke.cpp)

# FemboyScript 2.0: bytecode validation, the VM, the ECS bridge, the graph
fby_add_engine_test(fby_script_vm_test script_vm ScriptVmTest.cpp)

# FemboyScript debug data: source spans, locals, breakpoints, stepping, fields.
fby_add_engine_test(fby_script_debug_test script_debug ScriptDebugTest.cpp)

# AOT backend: bytecode -> C++26 source, LayoutHash validation and the
add_executable(fby_script_aot_test ScriptAotTest.cpp)
target_link_libraries(fby_script_aot_test PRIVATE FemboyEngine2D::Engine)
target_compile_definitions(fby_script_aot_test PRIVATE
    FBY_ENGINE_INCLUDE_DIR="${FBY_SOURCE_ROOT}")
set_target_properties(fby_script_aot_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME script_aot COMMAND fby_script_aot_test)

# Pink UI: mdspan grid layout, anchors, hit testing, one-batch packing and the
fby_add_engine_test(fby_pink_ui_test pink_ui PinkUITest.cpp)

# DSP mixer graph: routing, ducking, reverb, lock-free command ring and zero
add_executable(fby_dsp_mixer_test DspMixerTest.cpp)
target_link_libraries(fby_dsp_mixer_test PRIVATE FemboyEngine2D::Engine)
if(TARGET Opus::opus)
    target_link_libraries(fby_dsp_mixer_test PRIVATE Opus::opus)
endif()
set_target_properties(fby_dsp_mixer_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME dsp_mixer COMMAND fby_dsp_mixer_test)

# Spatial audio: live gain/pan ramps on a playing voice.
fby_add_engine_test(fby_audio_spatial_test audio_spatial AudioSpatialTest.cpp)

# Audio cooking: WAV 16/24-bit -> .fpcm, Ogg demuxing, MP3 rejection.
add_executable(fby_audio_cook_test AudioCookTest.cpp
    "${FBY_SOURCE_ROOT}/Tools/FempakCooker/AudioCook.cpp")
target_include_directories(fby_audio_cook_test PRIVATE
    "${FBY_SOURCE_ROOT}/Tools/FempakCooker")
target_link_libraries(fby_audio_cook_test PRIVATE FemboyEngine2D::Engine)
if(TARGET Opus::opus)
    target_link_libraries(fby_audio_cook_test PRIVATE Opus::opus)
endif()
set_target_properties(fby_audio_cook_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME audio_cook COMMAND fby_audio_cook_test)

# MaterialGraph: DAG validation, deterministic GLSL and (when glslang is
add_executable(fby_material_graph_test MaterialGraphTest.cpp)
target_link_libraries(fby_material_graph_test PRIVATE FemboyEngine2D::Engine)
if(TARGET fby_shaderc_core AND NOT (FBY_WITH_ASAN OR FBY_WITH_UBSAN))
    target_link_libraries(fby_material_graph_test PRIVATE fby_shaderc_core)
    target_compile_definitions(fby_material_graph_test PRIVATE FBY_TEST_HAVE_SHADERC=1)
endif()
set_target_properties(fby_material_graph_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME material_graph COMMAND fby_material_graph_test)

# Engine-side material assets: the wire formats a shipped build loads, and the
# pipeline cache that turns a cooked payload into a backend pipeline.
fby_add_engine_test(fby_material_asset_test material_asset MaterialAssetTest.cpp)
fby_add_engine_test(fby_material_pipeline_cache_test material_pipeline_cache MaterialPipelineCacheTest.cpp)
fby_add_engine_test(fby_material_mesh_path_test material_mesh_path MaterialMeshPathTest.cpp)
fby_add_engine_test(fby_sprite_material_test sprite_material SpriteMaterialTest.cpp)

# GPU accessibility composite: offscreen target, CVD matrix and flash gain are
# verified by capturing pixels. Skips on a machine without a Vulkan device.
add_executable(fby_accessibility_composite_test AccessibilityCompositeTest.cpp)
target_link_libraries(fby_accessibility_composite_test PRIVATE FemboyEngine2D::Engine)
fby_link_asan(fby_accessibility_composite_test)
set_target_properties(fby_accessibility_composite_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME accessibility_composite COMMAND fby_accessibility_composite_test)

# End-to-end cook: fby_matc turns an authored graph into a .femmat payload and
# the test decodes it with the engine alone, so no editor library is involved.
if(TARGET fby_matc)
    add_executable(fby_material_cook_test MaterialCookTest.cpp)
    target_link_libraries(fby_material_cook_test PRIVATE FemboyEngine2D::Engine)
    target_compile_definitions(fby_material_cook_test PRIVATE FBY_MATC_PATH="$<TARGET_FILE:fby_matc>")
    add_dependencies(fby_material_cook_test fby_matc)
    fby_link_asan(fby_material_cook_test)
    set_target_properties(fby_material_cook_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME material_cook COMMAND fby_material_cook_test)
endif()

# Video: WebM/EBML + VP9-alpha acceptance, MP4/H.264/H.265/VP8 rejection and the
fby_add_engine_test(fby_video_test video VideoTest.cpp)

# Texture cooking: PNG/TGA -> BC7/ASTC mips, .ftex round-trip, BC7 mode-6 decode
add_executable(fby_texture_cook_test TextureCookTest.cpp)
target_include_directories(fby_texture_cook_test SYSTEM PRIVATE
    "${FBY_ENGINE_SOURCE_ROOT}/third_party/stb")
target_link_libraries(fby_texture_cook_test PRIVATE FemboyEngine2D::Engine FemboyEngine2D::AssetCook)
set_target_properties(fby_texture_cook_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
add_test(NAME texture_cook COMMAND fby_texture_cook_test)

# P1 authoring: .femeta metadata/transactions, animation timeline model and
fby_add_engine_test(fby_asset_meta_test asset_meta AssetMetaTest.cpp)
fby_add_engine_test(fby_animation_timeline_test animation_timeline AnimationTimelineTest.cpp)

# Isometric grid math, tilemaps, skeletal IK/retargeting and clip compression.
fby_add_engine_test(fby_grid_skeletal_test grid_skeletal GridSkeletalTest.cpp)

# Navigation: A* over rect and hex maps, tilemap cost maps and agent steering.
fby_add_engine_test(fby_navigation_test navigation NavigationTest.cpp)

# 2D lighting: falloff, additive lights, ambient, emissive and tonemap.
fby_add_engine_test(fby_lighting_test lighting LightingTest.cpp)

# Viewport modes: metadata, help text, heat ramp and diagnostic tints.
fby_add_engine_test(fby_viewport_mode_test viewport_modes ViewportModeTest.cpp)

# Presentation policy: collision modes, render masks, time scale and static lights.
fby_add_engine_test(fby_presentation_test presentation PresentationTest.cpp)

# SDF primitive palette: every shape family, gradients and the reflected component.
fby_add_engine_test(fby_sdf_primitives_test sdf_primitives SdfPrimitivesTest.cpp)

if(TARGET fby_editor)
    add_executable(fby_editor_command_stack_test EditorCommandStackTest.cpp)
    target_link_libraries(fby_editor_command_stack_test PRIVATE FemboyEngine2D::Editor)
    set_target_properties(fby_editor_command_stack_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME editor_command_stack COMMAND fby_editor_command_stack_test)

    add_executable(fby_pie_multiplayer_test PieMultiplayerTest.cpp)
    target_link_libraries(fby_pie_multiplayer_test PRIVATE FemboyEngine2D::Editor)
    set_target_properties(fby_pie_multiplayer_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME pie_multiplayer COMMAND fby_pie_multiplayer_test)

    # PIE client viewport: second target lifetime, client frame and release.
    add_executable(fby_pie_viewport_test PieViewportTest.cpp)
    target_link_libraries(fby_pie_viewport_test PRIVATE FemboyEngine2D::Editor)
    # FrameComposer owns the diagnostic tint tables, so this editor test needs
    # the same stack reserve as the engine tests.
    if(MSVC)
        target_link_options(fby_pie_viewport_test PRIVATE /STACK:16777216)
    endif()
    set_target_properties(fby_pie_viewport_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME pie_viewport COMMAND fby_pie_viewport_test)

    # Voice Devices panel: live output apply and its failure path.
    add_executable(fby_voice_device_panel_test VoiceDevicePanelTest.cpp)
    target_link_libraries(fby_voice_device_panel_test PRIVATE FemboyEngine2D::Editor)
    set_target_properties(fby_voice_device_panel_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME voice_device_panel COMMAND fby_voice_device_panel_test)

    # PIE voice: client packets reach the host through the loopback voice link.
    add_executable(fby_voice_pie_test VoicePieTest.cpp)
    target_link_libraries(fby_voice_pie_test PRIVATE FemboyEngine2D::Editor)
    if(TARGET Opus::opus)
        target_link_libraries(fby_voice_pie_test PRIVATE Opus::opus)
    endif()
    if(TARGET fby::rnnoise)
        target_link_libraries(fby_voice_pie_test PRIVATE fby::rnnoise)
    endif()
    set_target_properties(fby_voice_pie_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME voice_pie COMMAND fby_voice_pie_test)

    # Step 1 infrastructure: project creation/config, .femeta auto-import,
    add_executable(fby_editor_infrastructure_test EditorInfrastructureTest.cpp)
    target_include_directories(fby_editor_infrastructure_test SYSTEM PRIVATE
        "${FBY_ENGINE_SOURCE_ROOT}/third_party/stb")
    target_link_libraries(fby_editor_infrastructure_test PRIVATE
        FemboyEngine2D::Editor
        FemboyEngine2D::AssetCook)
    set_target_properties(fby_editor_infrastructure_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME editor_infrastructure COMMAND fby_editor_infrastructure_test)

# Step 2: MaterialGraph Tilt25D/Distortion + SPIR-V, LogicGraph field nodes
    add_executable(fby_graph_editor_test GraphEditorTest.cpp)
    target_link_libraries(fby_graph_editor_test PRIVATE FemboyEngine2D::Editor)
    target_compile_definitions(fby_graph_editor_test PRIVATE
        FBY_ENGINE_INCLUDE_DIR="${FBY_SOURCE_ROOT}")
    if(TARGET fby_shaderc_core)
        target_link_libraries(fby_graph_editor_test PRIVATE fby_shaderc_core)
        target_compile_definitions(fby_graph_editor_test PRIVATE FBY_TEST_HAVE_SHADERC=1)
    endif()
    set_target_properties(fby_graph_editor_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME graph_editor COMMAND fby_graph_editor_test)

# AI State Tree canvas: allocation-free flat compiler, depth/cycle/dead-loop
    add_executable(fby_ai_state_tree_panel_test AIStateTreeCanvasPanelTest.cpp)
    target_link_libraries(fby_ai_state_tree_panel_test PRIVATE FemboyEngine2D::Editor)
    set_target_properties(fby_ai_state_tree_panel_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME ai_state_tree_panel COMMAND fby_ai_state_tree_panel_test)

# Animation Graph canvas: flat AnimStateComponent compiler, blend topology,
    add_executable(fby_animation_graph_panel_test AnimationGraphCanvasPanelTest.cpp)
    target_link_libraries(fby_animation_graph_panel_test PRIVATE FemboyEngine2D::Editor)
    set_target_properties(fby_animation_graph_panel_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME animation_graph_panel COMMAND fby_animation_graph_panel_test)

    # Script debugger panel: compile, line breakpoints, resume and fault texts.
    add_executable(fby_script_debugger_panel_test ScriptDebuggerPanelTest.cpp)
    target_link_libraries(fby_script_debugger_panel_test PRIVATE FemboyEngine2D::Editor)
    set_target_properties(fby_script_debugger_panel_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME script_debugger_panel COMMAND fby_script_debugger_panel_test)

# Release key baking: seed determinism, masked C ABI header and the fixed
    add_executable(fby_release_cooker_test ReleaseCookerTest.cpp)
    target_link_libraries(fby_release_cooker_test PRIVATE FemboyEngine2D::Editor)
    set_target_properties(fby_release_cooker_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
    add_test(NAME release_cooker COMMAND fby_release_cooker_test)
endif()

# Physics extensions: Box2D v3 rope/chain construction, Verlet cloth with
fby_add_engine_test(fby_physics_fx_test physics_fx PhysicsFxTest.cpp)

# Narrative Tree: semantic, localized BlindMode descriptions (EN/RU templates,
fby_add_engine_test(fby_narrative_tree_test narrative_tree NarrativeTreeTest.cpp)

# Pink UI native font array: FontAtlasSet::MountToTextureArray + BuildFrame
fby_add_engine_test(fby_pinkui_font_mount_test pinkui_font_mount PinkUIFontMountTest.cpp)

# TTS: FlushSpeech is main-thread-only; setVoice follows the narrative locale.
fby_add_engine_test(fby_tts_thread_test tts_thread TtsThreadTest.cpp)

# Cloth render bridge: ClothRenderSystem emits a CPU-deformed MeshRenderItem and
fby_add_engine_test(fby_cloth_render_test cloth_render ClothRenderTest.cpp)

# Pink UI OLED shifter, hypertext/NoLocalize, blind/deaf policy, subtitles,
fby_add_engine_test(fby_ui_accessibility_test ui_accessibility UIAccessibilityTest.cpp)

# P3 integration: Snapshot v3 world extension, b2 remap, VFS font atlas and
fby_add_engine_test(fby_p3_integration_test p3_integration P3IntegrationTest.cpp)

# FemAnimGraph: tag/float transition conditions, the previous -> current bone
fby_add_engine_test(fby_anim_graph_test anim_graph AnimGraphTest.cpp)

# FemStateTree: dense ActiveStates path, flat distance/tag evaluators,
fby_add_engine_test(fby_state_tree_test state_tree StateTreeTest.cpp)

# Advanced AAA DOD pipelines: virtual texturing through VFS mmap, frame change
fby_add_engine_test(fby_advanced_pipelines_test advanced_pipelines AdvancedPipelinesTest.cpp)

# Distribution SDK: build-profile advice, AES/ChaCha AEAD vectors, LZ codec and
fby_add_engine_test(fby_sdk_crypto_test sdk_crypto SdkCryptoTest.cpp)

# Storefront bridge: C ABI adoption, lock-free command ring, deterministic
fby_add_engine_test(fby_platform_bridge_test platform_bridge PlatformBridgeTest.cpp)
if(TARGET fby_platform_gog)
    add_dependencies(fby_platform_bridge_test fby_platform_gog)
    target_compile_definitions(fby_platform_bridge_test PRIVATE
        FBY_GOG_PROVIDER_PATH="$<TARGET_FILE:fby_platform_gog>")
endif()

# Hybrid SDF physics: mdspan bilinear sampling, gradient normals and Box2D v3
fby_add_engine_test(fby_sdf_physics_test sdf_physics SdfPhysicsTest.cpp)

# Render graph, SDF shadow, camera post FX, GPU weather dispatch and card boards.
fby_add_engine_test(fby_render_weather_test render_weather RenderWeatherTest.cpp)

# Data-driven descriptors, transactional prefab spawn, Units DPI and theme palette.
fby_add_engine_test(fby_data_driven_test data_driven DataDrivenTest.cpp)

# Definitions.hpp: stable type ids, FemCast and checked Cast without RTTI.
fby_add_engine_test(fby_definitions_test definitions DefinitionsTest.cpp)

# LogBoy: FNV-1a system hashes, bounded SPSC ring, truncation and the
fby_add_engine_test(fby_logboy_test logboy LogBoyTest.cpp)

# Project template generator: writes an isolated game module into Projects/
if(TARGET fby_template_gen)
    add_test(NAME template_gen
        COMMAND ${CMAKE_COMMAND}
            -DTOOL=$<TARGET_FILE:fby_template_gen>
            -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/template_smoke
            -DENGINE_ROOT=${FBY_ENGINE_SOURCE_ROOT}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/TemplateGenSmoke.cmake)
endif()

# Every test executable is instrumented in a sanitizer build, including the
# manually registered ones that do not go through fby_add_engine_test.
if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    get_property(FbyAllTargets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(FbyTestTarget ${FbyAllTargets})
        get_target_property(FbyTestTargetType ${FbyTestTarget} TYPE)
        if(FbyTestTargetType STREQUAL "EXECUTABLE")
            fby_link_asan(${FbyTestTarget})
        endif()
    endforeach()
endif()

# Tests read repository-relative assets such as assets/fonts, so the working
# directory is the repository root instead of the test binary directory.
get_property(FbyTestNames DIRECTORY PROPERTY TESTS)
if(FbyTestNames)
    set_tests_properties(${FbyTestNames} PROPERTIES
        WORKING_DIRECTORY "${FBY_ENGINE_SOURCE_ROOT}"
        ENVIRONMENT "FBY_STORAGE_DIR=${CMAKE_BINARY_DIR}/tests-saved")
endif()
