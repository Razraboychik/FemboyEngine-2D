// FemboyEngine2D - deterministic simulation math accuracy contract.
#include "Engine/Core/SimMath.hpp"

#include <cmath>
#include <cstdio>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

[[nodiscard]] bool Near(float InA, float InB, float InTolerance)
{
    return std::fabs(InA - InB) <= InTolerance;
}
}

int main()
{
    // The polynomial keeps float accuracy across several turns, so a frame that
    // accumulates rotation does not drift away from the reference implementation.
    bool ScopedTrigOk = true;
    for (int ScopedIndex = -2000; ScopedIndex <= 2000; ++ScopedIndex)
    {
        const float ScopedAngle = static_cast<float>(ScopedIndex) * 0.031f;
        const float ScopedSin = Fby::Sim::Sin(ScopedAngle);
        const float ScopedCos = Fby::Sim::Cos(ScopedAngle);
        float ScopedBothSin = 0.0f;
        float ScopedBothCos = 0.0f;
        Fby::Sim::SinCos(ScopedAngle, &ScopedBothSin, &ScopedBothCos);
        const float ScopedReferenceSin = static_cast<float>(std::sin(static_cast<double>(ScopedAngle)));
        const float ScopedReferenceCos = static_cast<float>(std::cos(static_cast<double>(ScopedAngle)));
        ScopedTrigOk = ScopedTrigOk && Near(ScopedSin, ScopedReferenceSin, 2.0e-6f) &&
                       Near(ScopedCos, ScopedReferenceCos, 2.0e-6f) && ScopedSin == ScopedBothSin &&
                       ScopedCos == ScopedBothCos;
    }
    Check(ScopedTrigOk, "sin, cos and sin-cos agree with the reference within float accuracy");

    bool ScopedAtanOk = true;
    for (int ScopedY = -40; ScopedY <= 40; ++ScopedY)
    {
        for (int ScopedX = -40; ScopedX <= 40; ++ScopedX)
        {
            const float ScopedInputY = static_cast<float>(ScopedY) * 0.73f;
            const float ScopedInputX = static_cast<float>(ScopedX) * 0.61f;
            const float ScopedReference =
                static_cast<float>(std::atan2(static_cast<double>(ScopedInputY), static_cast<double>(ScopedInputX)));
            ScopedAtanOk = ScopedAtanOk && Near(Fby::Sim::Atan2(ScopedInputY, ScopedInputX), ScopedReference, 2.0e-6f);
        }
    }
    Check(ScopedAtanOk, "atan2 agrees with the reference within float accuracy");
    Check(Fby::Sim::Atan2(0.0f, 0.0f) == 0.0f, "atan2 of the origin is zero");

    bool ScopedAcosOk = true;
    for (int ScopedIndex = -1000; ScopedIndex <= 1000; ++ScopedIndex)
    {
        const float ScopedValue = static_cast<float>(ScopedIndex) * 0.001f;
        const float ScopedReference = static_cast<float>(std::acos(static_cast<double>(ScopedValue)));
        ScopedAcosOk = ScopedAcosOk && Near(Fby::Sim::Acos(ScopedValue), ScopedReference, 2.0e-6f);
    }
    Check(ScopedAcosOk, "acos agrees with the reference within float accuracy");

    bool ScopedCbrtOk = true;
    for (int ScopedIndex = -500; ScopedIndex <= 500; ++ScopedIndex)
    {
        const float ScopedValue = static_cast<float>(ScopedIndex) * 0.37f;
        const float ScopedReference = static_cast<float>(std::cbrt(static_cast<double>(ScopedValue)));
        const float ScopedScale = std::fmax(1.0f, std::fabs(ScopedReference));
        ScopedCbrtOk = ScopedCbrtOk && Near(Fby::Sim::Cbrt(ScopedValue), ScopedReference, 2.0e-6f * ScopedScale);
    }
    Check(ScopedCbrtOk, "cbrt agrees with the reference within float accuracy");

    bool ScopedExpOk = true;
    for (int ScopedIndex = -4000; ScopedIndex <= 4000; ++ScopedIndex)
    {
        const float ScopedValue = static_cast<float>(ScopedIndex) * 0.02f;
        const double ScopedReference = std::exp(static_cast<double>(ScopedValue));
        ScopedExpOk = ScopedExpOk &&
                      std::fabs(static_cast<double>(Fby::Sim::Exp(ScopedValue)) - ScopedReference) <=
                          2.0e-6 * ScopedReference;
    }
    Check(ScopedExpOk, "exp agrees with the reference within float accuracy");

    bool ScopedLogOk = true;
    for (int ScopedIndex = 1; ScopedIndex <= 4000; ++ScopedIndex)
    {
        const float ScopedValue = static_cast<float>(ScopedIndex) * 0.25f;
        const float ScopedReference = static_cast<float>(std::log(static_cast<double>(ScopedValue)));
        ScopedLogOk = ScopedLogOk && Near(Fby::Sim::Log(ScopedValue), ScopedReference, 2.0e-6f);
    }
    Check(ScopedLogOk, "log agrees with the reference within float accuracy");

    bool ScopedPowOk = true;
    for (int ScopedBase = 1; ScopedBase <= 100; ++ScopedBase)
    {
        for (int ScopedExponent = -10; ScopedExponent <= 10; ++ScopedExponent)
        {
            const float ScopedInputBase = static_cast<float>(ScopedBase) * 0.1f;
            const float ScopedInputExponent = static_cast<float>(ScopedExponent) * 0.5f;
            const double ScopedReference =
                std::pow(static_cast<double>(ScopedInputBase), static_cast<double>(ScopedInputExponent));
            const double ScopedResult = static_cast<double>(Fby::Sim::Pow(ScopedInputBase, ScopedInputExponent));
            ScopedPowOk = ScopedPowOk && std::fabs(ScopedResult - ScopedReference) <= 5.0e-6 * ScopedReference;
        }
    }
    Check(ScopedPowOk, "pow agrees with the reference within float accuracy");
    Check(Near(Fby::Sim::Pow(-8.0f, 3.0f), -512.0f, 0.01f), "pow handles negative bases with integral exponents");

    // WrapPi keeps the canonical interval used by the angle lerp.
    bool ScopedWrapOk = true;
    for (int ScopedIndex = -100; ScopedIndex <= 100; ++ScopedIndex)
    {
        const float ScopedAngle = static_cast<float>(ScopedIndex) * 0.37f;
        const float ScopedWrapped = Fby::Sim::WrapPi(ScopedAngle);
        ScopedWrapOk = ScopedWrapOk && ScopedWrapped >= -Fby::Sim::kPi && ScopedWrapped <= Fby::Sim::kPi;
    }
    Check(ScopedWrapOk, "WrapPi returns an angle inside the canonical interval");

    if (GFailures == 0)
    {
        std::printf("sim_math: OK (trig, atan2, acos, cbrt, exp, log, pow and wrap accuracy)\n");
        return 0;
    }
    std::fprintf(stderr, "sim_math: %d failure(s)\n", GFailures);
    return 1;
}
