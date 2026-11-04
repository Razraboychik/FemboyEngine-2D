// FemboyEngine2D - plugin ABI shared by the host and the test plugins.
#pragma once

#include <cstdint>

// The host refuses a plugin whose ABI does not match, so a stale binary can never
// call into the engine with an incompatible layout.
inline constexpr std::uint32_t kFbyPluginAbiVersion = 1;

#if defined(_WIN32)
#define FBY_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define FBY_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using FbyPluginVersionFn = std::uint32_t (*)();
using FbyPluginProbeFn = std::uint32_t (*)();
