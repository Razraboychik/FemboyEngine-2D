// FemboyEngine2D - shipping build profile and compiler optimisation policy.
#pragma once

#include <cstdint>
#include <string_view>

namespace Fby::Build
{

enum class OptimizationLevel : std::uint8_t
{
    O1 = 0,
    O2,
    O3,
    Ofast,
    Os,
    Count,
};

inline constexpr std::uint32_t kOptimizationLevelCount = static_cast<std::uint32_t>(OptimizationLevel::Count);

[[nodiscard]] constexpr std::string_view OptimizationName(OptimizationLevel InLevel) noexcept
{
    switch (InLevel)
    {
        case OptimizationLevel::O1:
            return "O1";
        case OptimizationLevel::O2:
            return "O2";
        case OptimizationLevel::O3:
            return "O3";
        case OptimizationLevel::Ofast:
            return "Ofast";
        case OptimizationLevel::Os:
            return "Os";
        case OptimizationLevel::Count:
            break;
    }
    return "O2";
}

[[nodiscard]] constexpr OptimizationLevel ParseOptimizationLevel(std::string_view InName,
                                                                OptimizationLevel InFallback = OptimizationLevel::O2) noexcept
{
    if (InName == "O1" || InName == "-O1")
    {
        return OptimizationLevel::O1;
    }
    if (InName == "O2" || InName == "-O2")
    {
        return OptimizationLevel::O2;
    }
    if (InName == "O3" || InName == "-O3")
    {
        return OptimizationLevel::O3;
    }
    if (InName == "Ofast" || InName == "-Ofast")
    {
        return OptimizationLevel::Ofast;
    }
    if (InName == "Os" || InName == "-Os")
    {
        return OptimizationLevel::Os;
    }
    return InFallback;
}

// Primary switch for Clang/GCC. Ofast is a real driver option and is kept
[[nodiscard]] constexpr std::string_view OptimizationFlag(OptimizationLevel InLevel) noexcept
{
    switch (InLevel)
    {
        case OptimizationLevel::O1:
            return "-O1";
        case OptimizationLevel::O2:
            return "-O2";
        case OptimizationLevel::O3:
            return "-O3";
        case OptimizationLevel::Ofast:
            return "-Ofast";
        case OptimizationLevel::Os:
            return "-Os";
        case OptimizationLevel::Count:
            break;
    }
    return "-O2";
}

// MSVC / clang-cl spell the same intent differently. /O1 is "favour size" and
[[nodiscard]] constexpr std::string_view OptimizationFlagMsvc(OptimizationLevel InLevel) noexcept
{
    switch (InLevel)
    {
        case OptimizationLevel::O1:
            return "/O1";
        case OptimizationLevel::O2:
            return "/O2";
        case OptimizationLevel::O3:
            return "/O2";
        case OptimizationLevel::Ofast:
            return "/O2";
        case OptimizationLevel::Os:
            return "/O1";
        case OptimizationLevel::Count:
            break;
    }
    return "/O2";
}

// Ofast is the only preset that relaxes IEEE semantics. The AOT cook and the
[[nodiscard]] constexpr bool RelaxesFloatingPoint(OptimizationLevel InLevel) noexcept
{
    return InLevel == OptimizationLevel::Ofast;
}

[[nodiscard]] constexpr std::string_view FastMathFlag(OptimizationLevel InLevel, bool InMsvcStyle) noexcept
{
    if (!RelaxesFloatingPoint(InLevel))
    {
        return "";
    }
    return InMsvcStyle ? "/fp:fast" : "-ffast-math";
}

[[nodiscard]] constexpr std::string_view LinkTimeOptimizationFlag(bool InMsvcStyle) noexcept
{
    return InMsvcStyle ? "/GL" : "-flto";
}

[[nodiscard]] constexpr std::string_view StripDebugFlag(bool InMsvcStyle) noexcept
{
    return InMsvcStyle ? "/DEBUG:NONE" : "-s";
}

struct BuildProfile
{
    // Field-wise equality so a containing settings POD can default its own
    [[nodiscard]] friend constexpr bool operator==(const BuildProfile&, const BuildProfile&) noexcept = default;

    OptimizationLevel optimization = OptimizationLevel::O2;
    // Whole-program optimisation. Off by default for iteration builds because
    bool linkTimeOptimization = false;
    // Debug info stripped from the final archive, never from a development one
    bool stripDebugInfo = true;
    // Batch translation units into one. Strongly recommended for the ~120
    bool unityBuild = true;
    // Build for the host CPU (-march=native / /arch:AVX2). Never enable for a
    bool nativeArch = false;
};

static_assert(sizeof(BuildProfile) <= 8, "BuildProfile is stored in every project file and must stay compact");

// The advice is deliberately written as three short lines, not as prose: the
struct OptimizationAdvice
{
    std::string_view headline;
    std::string_view detail;
    std::string_view warning;
    // 0..5, higher is better for the stated workload
    std::uint8_t iterationScore = 3;
    std::uint8_t shippingScore = 3;
};

[[nodiscard]] constexpr OptimizationAdvice Recommend(OptimizationLevel InLevel, bool InShipping,
                                                     bool InMobile) noexcept
{
    switch (InLevel)
    {
        case OptimizationLevel::O1:
            return {"Fastest compile, slowest game",
                    "Best for a debug probe or a CI smoke build; every hot path still works but the "
                    "job system and cloth solver run at roughly half speed.",
                    "Do not ship: measured frame time is not representative.", 5, 1};
        case OptimizationLevel::O2:
            return {"The default and the recommended shipping baseline",
                    "Keeps IEEE float semantics, enables inlining and vectorisation and compiles fast "
                    "enough for a 30-second incremental cook.",
                    "None; this is the profile the 25-test CI slate is validated against.", 4, 5};
        case OptimizationLevel::O3:
            return {"Aggressive inlining for release candidates",
                    "Worth it for a CPU cloth or particle archetype; inspect code size and instruction "
                    "cache pressure before making it the project default.",
                    "May increase binary size by 10-20 percent; measure both frame time and I-cache misses.",
                    2, 4};
        case OptimizationLevel::Ofast:
            return {"Non-IEEE fast math, opt-in only",
                    "Allows reassociation, reciprocal approximations and flush-to-zero. Useful for a "
                    "visual-only weather pass, never for the deterministic simulation.",
                    "Can change replay results and break NaN guards. Keep rollback, physics and snapshot "
                    "hashes on O2.",
                    2, 2};
        case OptimizationLevel::Os:
            return {InMobile ? "Size first, the mobile default" : "Size first, for a constrained installer",
                    "Favours compact code over loop unrolling; a 2D engine that fits the L2 cache often "
                    "runs this profile faster than O3.",
                    "Expect a slower cloth solver and slower software skinning; profile before shipping.",
                    3, static_cast<std::uint8_t>(InShipping ? (InMobile ? 5 : 3) : 2)};
        case OptimizationLevel::Count:
            break;
    }
    return {"Unknown profile", "Falling back to O2.", "Re-select the optimisation level.", 3, 3};
}

// Human-readable summary used by the panel footer and the cook log. The output
[[nodiscard]] constexpr std::string_view LtoAdvice(bool InShipping) noexcept
{
    return InShipping ? "LTO is recommended for the final archive; expect a 2-4x longer link."
                      : "LTO is off for iteration builds; the incremental link stays fast.";
}
}
