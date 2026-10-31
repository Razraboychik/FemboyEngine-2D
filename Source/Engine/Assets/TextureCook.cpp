// FemboyEngine2D - texture cooker implementation.
#include "Engine/Assets/TextureCook.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Fby::Cook
{
namespace
{

void SetError(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
}

// Small byte helpers
[[nodiscard]] std::string ToLower(std::string_view InText)
{
    std::string ScopedOut(InText);
    for (char& ScopedCharacter : ScopedOut)
    {
        ScopedCharacter = static_cast<char>(std::tolower(static_cast<unsigned char>(ScopedCharacter)));
    }
    return ScopedOut;
}

[[nodiscard]] std::string ExtensionOf(std::string_view InPath)
{
    const std::size_t ScopedSlash = InPath.find_last_of("/\\");
    const std::size_t ScopedDot = InPath.find_last_of('.');
    if (ScopedDot == std::string_view::npos || (ScopedSlash != std::string_view::npos && ScopedDot < ScopedSlash))
    {
        return {};
    }
    return ToLower(InPath.substr(ScopedDot));
}

[[nodiscard]] bool IsJpegMagic(std::span<const std::byte> InBytes) noexcept
{
    return InBytes.size() >= 3 && static_cast<std::uint8_t>(InBytes[0]) == 0xFF &&
           static_cast<std::uint8_t>(InBytes[1]) == 0xD8 && static_cast<std::uint8_t>(InBytes[2]) == 0xFF;
}

[[nodiscard]] bool IsPngMagic(std::span<const std::byte> InBytes) noexcept
{
    constexpr std::uint8_t ScopedKPngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (InBytes.size() < sizeof(ScopedKPngSignature))
    {
        return false;
    }
    for (std::size_t ScopedI = 0; ScopedI < sizeof(ScopedKPngSignature); ++ScopedI)
    {
        if (static_cast<std::uint8_t>(InBytes[ScopedI]) != ScopedKPngSignature[ScopedI])
        {
            return false;
        }
    }
    return true;
}

// A TGA file has no magic; validate the 18-byte header enough that an
[[nodiscard]] bool LooksLikeTga(std::span<const std::byte> InBytes) noexcept
{
    if (InBytes.size() < 18)
    {
        return false;
    }
    const auto ScopedByteAt = [&InBytes](std::size_t InIndex) { return static_cast<std::uint8_t>(InBytes[InIndex]); };
    const std::uint8_t ScopedImageType = ScopedByteAt(2);
    const bool ScopedUncompressedOrRle =
        ScopedImageType == 2 || ScopedImageType == 3 || ScopedImageType == 10 || ScopedImageType == 11;
    if (!ScopedUncompressedOrRle)
    {
        return false;
    }
    const std::uint16_t ScopedWidth = static_cast<std::uint16_t>(ScopedByteAt(12) | (ScopedByteAt(13) << 8));
    const std::uint16_t ScopedHeight = static_cast<std::uint16_t>(ScopedByteAt(14) | (ScopedByteAt(15) << 8));
    if (ScopedWidth == 0 || ScopedHeight == 0)
    {
        return false;
    }
    const std::uint8_t ScopedBitsPerPixel = ScopedByteAt(16);
    return ScopedBitsPerPixel == 8 || ScopedBitsPerPixel == 24 || ScopedBitsPerPixel == 32;
}

// sRGB transfer functions. RGB filtering happens in linear light; alpha is
[[nodiscard]] float SrgbToLinearScalar(std::uint8_t InValue) noexcept
{
    const float ScopedChannel = static_cast<float>(InValue) / 255.0f;
    if (ScopedChannel <= 0.04045f)
    {
        return ScopedChannel / 12.92f;
    }
    return std::pow((ScopedChannel + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] std::uint8_t LinearToSrgbScalar(float InLinear) noexcept
{
    if (!(InLinear > 0.0f))
    {
        return 0;
    }
    if (InLinear >= 1.0f)
    {
        return 255;
    }
    const float ScopedEncoded =
        InLinear <= 0.0031308f ? InLinear * 12.92f : 1.055f * std::pow(InLinear, 1.0f / 2.4f) - 0.055f;
    const float ScopedScaled = ScopedEncoded * 255.0f + 0.5f;
    if (ScopedScaled <= 0.0f)
    {
        return 0;
    }
    if (ScopedScaled >= 255.0f)
    {
        return 255;
    }
    return static_cast<std::uint8_t>(ScopedScaled);
}

[[nodiscard]] const std::array<float, 256>& SrgbDecodeTable()
{
    static const std::array<float, 256> ScopedTable = []
    {
        std::array<float, 256> ScopedValues{};
        for (std::size_t ScopedI = 0; ScopedI < ScopedValues.size(); ++ScopedI)
        {
            ScopedValues[ScopedI] = SrgbToLinearScalar(static_cast<std::uint8_t>(ScopedI));
        }
        return ScopedValues;
    }();
    return ScopedTable;
}

// Mip generation
void DownsampleBox(const std::uint8_t* InSource, std::uint32_t InSourceWidth, std::uint32_t InSourceHeight,
                   std::uint8_t* InDestination, std::uint32_t InDestinationWidth, std::uint32_t InDestinationHeight,
                   bool InSrgb)
{
    const std::array<float, 256>& ScopedDecode = SrgbDecodeTable();
    for (std::uint32_t ScopedY = 0; ScopedY < InDestinationHeight; ++ScopedY)
    {
        std::uint32_t ScopedSourceY0 =
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(ScopedY) * InSourceHeight) / InDestinationHeight);
        std::uint32_t ScopedSourceY1 = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(ScopedY + 1) * InSourceHeight) / InDestinationHeight);
        if (ScopedSourceY1 <= ScopedSourceY0)
        {
            ScopedSourceY1 = ScopedSourceY0 + 1;
        }
        ScopedSourceY1 = std::min(ScopedSourceY1, InSourceHeight);

        for (std::uint32_t ScopedX = 0; ScopedX < InDestinationWidth; ++ScopedX)
        {
            std::uint32_t ScopedSourceX0 =
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(ScopedX) * InSourceWidth) / InDestinationWidth);
            std::uint32_t ScopedSourceX1 = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(ScopedX + 1) * InSourceWidth) / InDestinationWidth);
            if (ScopedSourceX1 <= ScopedSourceX0)
            {
                ScopedSourceX1 = ScopedSourceX0 + 1;
            }
            ScopedSourceX1 = std::min(ScopedSourceX1, InSourceWidth);

            float ScopedAccumR = 0.0f;
            float ScopedAccumG = 0.0f;
            float ScopedAccumB = 0.0f;
            float ScopedAccumA = 0.0f;
            std::uint32_t ScopedCount = 0;
            for (std::uint32_t ScopedSy = ScopedSourceY0; ScopedSy < ScopedSourceY1; ++ScopedSy)
            {
                const std::uint8_t* ScopedRow = InSource + (static_cast<std::size_t>(ScopedSy) * InSourceWidth) * 4;
                for (std::uint32_t ScopedSx = ScopedSourceX0; ScopedSx < ScopedSourceX1; ++ScopedSx)
                {
                    const std::uint8_t* ScopedTexel = ScopedRow + static_cast<std::size_t>(ScopedSx) * 4;
                    if (InSrgb)
                    {
                        ScopedAccumR += ScopedDecode[ScopedTexel[0]];
                        ScopedAccumG += ScopedDecode[ScopedTexel[1]];
                        ScopedAccumB += ScopedDecode[ScopedTexel[2]];
                    }
                    else
                    {
                        ScopedAccumR += static_cast<float>(ScopedTexel[0]) / 255.0f;
                        ScopedAccumG += static_cast<float>(ScopedTexel[1]) / 255.0f;
                        ScopedAccumB += static_cast<float>(ScopedTexel[2]) / 255.0f;
                    }
                    ScopedAccumA += static_cast<float>(ScopedTexel[3]) / 255.0f;
                    ++ScopedCount;
                }
            }

            const float ScopedInverse = 1.0f / static_cast<float>(ScopedCount);
            std::uint8_t* ScopedOut =
                InDestination + (static_cast<std::size_t>(ScopedY) * InDestinationWidth + ScopedX) * 4;
            if (InSrgb)
            {
                ScopedOut[0] = LinearToSrgbScalar(ScopedAccumR * ScopedInverse);
                ScopedOut[1] = LinearToSrgbScalar(ScopedAccumG * ScopedInverse);
                ScopedOut[2] = LinearToSrgbScalar(ScopedAccumB * ScopedInverse);
            }
            else
            {
                ScopedOut[0] =
                    static_cast<std::uint8_t>(std::min(255.0f, ScopedAccumR * ScopedInverse * 255.0f + 0.5f));
                ScopedOut[1] =
                    static_cast<std::uint8_t>(std::min(255.0f, ScopedAccumG * ScopedInverse * 255.0f + 0.5f));
                ScopedOut[2] =
                    static_cast<std::uint8_t>(std::min(255.0f, ScopedAccumB * ScopedInverse * 255.0f + 0.5f));
            }
            ScopedOut[3] = static_cast<std::uint8_t>(std::min(255.0f, ScopedAccumA * ScopedInverse * 255.0f + 0.5f));
        }
    }
}

// BC7 mode 6
constexpr std::array<std::int32_t, 16> kWeights4 = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

struct Bc7Block
{
    // 7-bit endpoint values
    std::int32_t v0[4] = {};
    std::int32_t v1[4] = {};
    std::int32_t p0 = 0;
    std::int32_t p1 = 0;
    std::uint8_t indices[16] = {};
    std::uint64_t error = std::numeric_limits<std::uint64_t>::max();
};

// Quantizes one float endpoint to the mode-6 representation, choosing the
void QuantizeEndpoint(const float InEndpoint[4], std::int32_t InValues[4], std::int32_t& InPbit)
{
    std::int32_t ScopedBestValues[4] = {};
    std::int32_t ScopedBestP = 0;
    std::uint64_t ScopedBestError = std::numeric_limits<std::uint64_t>::max();
    for (std::int32_t ScopedP = 0; ScopedP <= 1; ++ScopedP)
    {
        std::uint64_t ScopedError = 0;
        std::int32_t ScopedCandidate[4] = {};
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            const float ScopedClamped = std::clamp(InEndpoint[ScopedChannel], 0.0f, 255.0f);
            std::int32_t ScopedSeven =
                static_cast<std::int32_t>(std::lround((ScopedClamped - static_cast<float>(ScopedP)) * 0.5f));
            ScopedSeven = std::clamp(ScopedSeven, 0, 127);
            const std::int32_t ScopedExpanded = ScopedSeven * 2 + ScopedP;
            const std::int64_t ScopedDifference =
                static_cast<std::int64_t>(ScopedExpanded) - std::lround(ScopedClamped);
            ScopedError += static_cast<std::uint64_t>(ScopedDifference * ScopedDifference);
            ScopedCandidate[ScopedChannel] = ScopedSeven;
        }
        if (ScopedError < ScopedBestError)
        {
            ScopedBestError = ScopedError;
            ScopedBestP = ScopedP;
            std::copy(ScopedCandidate, ScopedCandidate + 4, ScopedBestValues);
        }
    }
    InPbit = ScopedBestP;
    std::copy(ScopedBestValues, ScopedBestValues + 4, InValues);
}

// Least-squares endpoint refinement for fixed interpolation weights.
bool RefineEndpoints(const std::uint8_t* InPixels, const std::uint8_t* InIndices, float InE0[4], float InE1[4])
{
    double ScopedA = 0.0;
    double ScopedB = 0.0;
    double ScopedC = 0.0;
    double ScopedD[4] = {};
    double ScopedE[4] = {};
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        const double ScopedT = static_cast<double>(kWeights4[InIndices[ScopedTexel]]) / 64.0;
        const double ScopedOneMinus = 1.0 - ScopedT;
        ScopedA += ScopedOneMinus * ScopedOneMinus;
        ScopedB += ScopedOneMinus * ScopedT;
        ScopedC += ScopedT * ScopedT;
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            const double ScopedValue = static_cast<double>(InPixels[ScopedTexel * 4 + ScopedChannel]);
            ScopedD[ScopedChannel] += ScopedOneMinus * ScopedValue;
            ScopedE[ScopedChannel] += ScopedT * ScopedValue;
        }
    }
    const double ScopedDeterminant = ScopedA * ScopedC - ScopedB * ScopedB;
    if (std::abs(ScopedDeterminant) < 1.0e-6)
    {
        // All texels share one weight: nothing to solve
        return false;
    }
    for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
    {
        const double ScopedEndpoint0 =
            (ScopedC * ScopedD[ScopedChannel] - ScopedB * ScopedE[ScopedChannel]) / ScopedDeterminant;
        const double ScopedEndpoint1 =
            (ScopedA * ScopedE[ScopedChannel] - ScopedB * ScopedD[ScopedChannel]) / ScopedDeterminant;
        InE0[ScopedChannel] = static_cast<float>(std::clamp(ScopedEndpoint0, 0.0, 255.0));
        InE1[ScopedChannel] = static_cast<float>(std::clamp(ScopedEndpoint1, 0.0, 255.0));
    }
    return true;
}

// Fits one candidate endpoint pair. Returns the quantized block and its error.
Bc7Block FitBc7Mode6(const std::uint8_t* InPixels, const float InStart0[4], const float InStart1[4])
{
    float ScopedE0[4];
    float ScopedE1[4];
    std::copy(InStart0, InStart0 + 4, ScopedE0);
    std::copy(InStart1, InStart1 + 4, ScopedE1);

    std::uint8_t ScopedIndices[16] = {};
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 3; ++ScopedIteration)
    {
        // Project every texel on the endpoint axis and snap to the palette.
        float ScopedAxis[4];
        float ScopedLengthSquared = 0.0f;
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            ScopedAxis[ScopedChannel] = ScopedE1[ScopedChannel] - ScopedE0[ScopedChannel];
            ScopedLengthSquared += ScopedAxis[ScopedChannel] * ScopedAxis[ScopedChannel];
        }
        if (ScopedLengthSquared < 1.0e-6f)
        {
            break;
        }
        for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
        {
            float ScopedProjection = 0.0f;
            for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
            {
                ScopedProjection +=
                    (static_cast<float>(InPixels[ScopedTexel * 4 + ScopedChannel]) - ScopedE0[ScopedChannel]) *
                    ScopedAxis[ScopedChannel];
            }
            const float ScopedT = std::clamp(ScopedProjection / ScopedLengthSquared, 0.0f, 1.0f);
            std::int32_t ScopedBestIndex = 0;
            float ScopedBestDistance = 1.0e30f;
            for (std::int32_t ScopedIndex = 0; ScopedIndex < 16; ++ScopedIndex)
            {
                const float ScopedWeight = static_cast<float>(kWeights4[ScopedIndex]) / 64.0f;
                const float ScopedDistance = std::abs(ScopedWeight - ScopedT);
                if (ScopedDistance < ScopedBestDistance)
                {
                    ScopedBestDistance = ScopedDistance;
                    ScopedBestIndex = ScopedIndex;
                }
            }
            ScopedIndices[ScopedTexel] = static_cast<std::uint8_t>(ScopedBestIndex);
        }
        if (!RefineEndpoints(InPixels, ScopedIndices, ScopedE0, ScopedE1))
        {
            break;
        }
    }

    // Final quantization and error measurement with the exact decoded values.
    Bc7Block ScopedBlock;
    QuantizeEndpoint(ScopedE0, ScopedBlock.v0, ScopedBlock.p0);
    QuantizeEndpoint(ScopedE1, ScopedBlock.v1, ScopedBlock.p1);

    float ScopedEndpoint0[4];
    float ScopedEndpoint1[4];
    for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
    {
        ScopedEndpoint0[ScopedChannel] = static_cast<float>(ScopedBlock.v0[ScopedChannel] * 2 + ScopedBlock.p0);
        ScopedEndpoint1[ScopedChannel] = static_cast<float>(ScopedBlock.v1[ScopedChannel] * 2 + ScopedBlock.p1);
    }
    float ScopedAxis[4];
    float ScopedLengthSquared = 0.0f;
    for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
    {
        ScopedAxis[ScopedChannel] = ScopedEndpoint1[ScopedChannel] - ScopedEndpoint0[ScopedChannel];
        ScopedLengthSquared += ScopedAxis[ScopedChannel] * ScopedAxis[ScopedChannel];
    }

    std::uint64_t ScopedError = 0;
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        std::int32_t ScopedIndex = 0;
        if (ScopedLengthSquared >= 1.0e-6f)
        {
            float ScopedProjection = 0.0f;
            for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
            {
                ScopedProjection +=
                    (static_cast<float>(InPixels[ScopedTexel * 4 + ScopedChannel]) - ScopedEndpoint0[ScopedChannel]) *
                    ScopedAxis[ScopedChannel];
            }
            const float ScopedT = std::clamp(ScopedProjection / ScopedLengthSquared, 0.0f, 1.0f);
            float ScopedBestDistance = 1.0e30f;
            for (std::int32_t ScopedCandidate = 0; ScopedCandidate < 16; ++ScopedCandidate)
            {
                const float ScopedDistance = std::abs(static_cast<float>(kWeights4[ScopedCandidate]) / 64.0f - ScopedT);
                if (ScopedDistance < ScopedBestDistance)
                {
                    ScopedBestDistance = ScopedDistance;
                    ScopedIndex = ScopedCandidate;
                }
            }
        }
        ScopedBlock.indices[ScopedTexel] = static_cast<std::uint8_t>(ScopedIndex);
        const std::int32_t ScopedWeight = kWeights4[ScopedIndex];
        const std::int32_t ScopedInverse = 64 - ScopedWeight;
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            const std::int32_t ScopedDecoded =
                (static_cast<std::int32_t>(ScopedEndpoint0[ScopedChannel]) * ScopedInverse +
                 static_cast<std::int32_t>(ScopedEndpoint1[ScopedChannel]) * ScopedWeight + 32) >>
                6;
            const std::int32_t ScopedSource = InPixels[ScopedTexel * 4 + ScopedChannel];
            const std::int64_t ScopedDifference = static_cast<std::int64_t>(ScopedDecoded) - ScopedSource;
            ScopedError += static_cast<std::uint64_t>(ScopedDifference * ScopedDifference);
        }
    }
    ScopedBlock.error = ScopedError;

    // The anchor texel index is stored in 3 bits (its top bit is implied 0).
    if (ScopedBlock.indices[0] > 7)
    {
        std::swap(ScopedBlock.v0, ScopedBlock.v1);
        std::swap(ScopedBlock.p0, ScopedBlock.p1);
        for (std::uint8_t& ScopedIndex : ScopedBlock.indices)
        {
            ScopedIndex = static_cast<std::uint8_t>(15 - ScopedIndex);
        }
    }
    return ScopedBlock;
}

// 4D principal axis through power iteration. Deterministic: fixed start vector
void PrincipalAxis(const std::uint8_t* InPixels, float InAxis[4])
{
    float ScopedMean[4] = {};
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            ScopedMean[ScopedChannel] += static_cast<float>(InPixels[ScopedTexel * 4 + ScopedChannel]);
        }
    }
    for (float& ScopedValue : ScopedMean)
    {
        ScopedValue /= 16.0f;
    }

    float ScopedCovariance[4][4] = {};
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        float ScopedCentered[4];
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            ScopedCentered[ScopedChannel] =
                static_cast<float>(InPixels[ScopedTexel * 4 + ScopedChannel]) - ScopedMean[ScopedChannel];
        }
        for (std::int32_t ScopedRow = 0; ScopedRow < 4; ++ScopedRow)
        {
            for (std::int32_t ScopedColumn = 0; ScopedColumn < 4; ++ScopedColumn)
            {
                ScopedCovariance[ScopedRow][ScopedColumn] += ScopedCentered[ScopedRow] * ScopedCentered[ScopedColumn];
            }
        }
    }

    float ScopedVector[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (std::int32_t ScopedIteration = 0; ScopedIteration < 12; ++ScopedIteration)
    {
        float ScopedNext[4] = {};
        for (std::int32_t ScopedRow = 0; ScopedRow < 4; ++ScopedRow)
        {
            for (std::int32_t ScopedColumn = 0; ScopedColumn < 4; ++ScopedColumn)
            {
                ScopedNext[ScopedRow] += ScopedCovariance[ScopedRow][ScopedColumn] * ScopedVector[ScopedColumn];
            }
        }
        float ScopedLength = 0.0f;
        for (float ScopedValue : ScopedNext)
        {
            ScopedLength += ScopedValue * ScopedValue;
        }
        ScopedLength = std::sqrt(ScopedLength);
        if (ScopedLength < 1.0e-6f)
        {
            // Flat block: any axis works, endpoints coincide
            break;
        }
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            ScopedVector[ScopedChannel] = ScopedNext[ScopedChannel] / ScopedLength;
        }
    }
    std::copy(ScopedVector, ScopedVector + 4, InAxis);
}

void ProjectExtents(const std::uint8_t* InPixels, const float InAxis[4], float InE0[4], float InE1[4])
{
    float ScopedMean[4] = {};
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            ScopedMean[ScopedChannel] += static_cast<float>(InPixels[ScopedTexel * 4 + ScopedChannel]);
        }
    }
    for (float& ScopedValue : ScopedMean)
    {
        ScopedValue /= 16.0f;
    }

    float ScopedMinimum = std::numeric_limits<float>::max();
    float ScopedMaximum = std::numeric_limits<float>::lowest();
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        float ScopedProjection = 0.0f;
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            ScopedProjection +=
                (static_cast<float>(InPixels[ScopedTexel * 4 + ScopedChannel]) - ScopedMean[ScopedChannel]) *
                InAxis[ScopedChannel];
        }
        ScopedMinimum = std::min(ScopedMinimum, ScopedProjection);
        ScopedMaximum = std::max(ScopedMaximum, ScopedProjection);
    }
    for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
    {
        InE0[ScopedChannel] =
            std::clamp(ScopedMean[ScopedChannel] + InAxis[ScopedChannel] * ScopedMinimum, 0.0f, 255.0f);
        InE1[ScopedChannel] =
            std::clamp(ScopedMean[ScopedChannel] + InAxis[ScopedChannel] * ScopedMaximum, 0.0f, 255.0f);
    }
}

void FarthestPair(const std::uint8_t* InPixels, float InE0[4], float InE1[4])
{
    float ScopedMean[4] = {};
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            ScopedMean[ScopedChannel] += static_cast<float>(InPixels[ScopedTexel * 4 + ScopedChannel]);
        }
    }
    for (float& ScopedValue : ScopedMean)
    {
        ScopedValue /= 16.0f;
    }

    std::int32_t ScopedFirst = 0;
    float ScopedFirstDistance = -1.0f;
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        float ScopedDistance = 0.0f;
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            const float ScopedDifference =
                static_cast<float>(InPixels[ScopedTexel * 4 + ScopedChannel]) - ScopedMean[ScopedChannel];
            ScopedDistance += ScopedDifference * ScopedDifference;
        }
        if (ScopedDistance > ScopedFirstDistance)
        {
            ScopedFirstDistance = ScopedDistance;
            ScopedFirst = ScopedTexel;
        }
    }
    std::int32_t ScopedSecond = 0;
    float ScopedSecondDistance = -1.0f;
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        float ScopedDistance = 0.0f;
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            const float ScopedDifference = static_cast<float>(InPixels[ScopedTexel * 4 + ScopedChannel]) -
                                           static_cast<float>(InPixels[ScopedFirst * 4 + ScopedChannel]);
            ScopedDistance += ScopedDifference * ScopedDifference;
        }
        if (ScopedDistance > ScopedSecondDistance)
        {
            ScopedSecondDistance = ScopedDistance;
            ScopedSecond = ScopedTexel;
        }
    }
    for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
    {
        InE0[ScopedChannel] = static_cast<float>(InPixels[ScopedFirst * 4 + ScopedChannel]);
        InE1[ScopedChannel] = static_cast<float>(InPixels[ScopedSecond * 4 + ScopedChannel]);
    }
}

// Packs a fitted block into the 16-byte BC7 mode-6 layout.
void PackBc7Mode6(const Bc7Block& InBlock, std::span<std::byte, 16> Out)
{
    std::uint64_t ScopedLow = 0;
    std::uint64_t ScopedHigh = 0;
    const auto ScopedWrite =
        [&ScopedLow, &ScopedHigh](std::uint32_t InOffset, std::uint32_t InBits, std::uint64_t InValue)
    {
        if (InOffset < 64)
        {
            ScopedLow |= InValue << InOffset;
            if (InOffset + InBits > 64)
            {
                ScopedHigh |= InValue >> (64 - InOffset);
            }
        }
        else
        {
            ScopedHigh |= InValue << (InOffset - 64);
        }
    };

    // Mode 6: bit 6 set
    ScopedWrite(0, 7, 0x40u);
    ScopedWrite(7, 7, static_cast<std::uint64_t>(InBlock.v0[0]));
    ScopedWrite(14, 7, static_cast<std::uint64_t>(InBlock.v1[0]));
    ScopedWrite(21, 7, static_cast<std::uint64_t>(InBlock.v0[1]));
    ScopedWrite(28, 7, static_cast<std::uint64_t>(InBlock.v1[1]));
    ScopedWrite(35, 7, static_cast<std::uint64_t>(InBlock.v0[2]));
    ScopedWrite(42, 7, static_cast<std::uint64_t>(InBlock.v1[2]));
    ScopedWrite(49, 7, static_cast<std::uint64_t>(InBlock.v0[3]));
    ScopedWrite(56, 7, static_cast<std::uint64_t>(InBlock.v1[3]));
    ScopedWrite(63, 1, static_cast<std::uint64_t>(InBlock.p0));
    ScopedWrite(64, 1, static_cast<std::uint64_t>(InBlock.p1));
    // Anchor index (texel 0) has its top bit implied; the fit already made it
    ScopedWrite(65, 3, static_cast<std::uint64_t>(InBlock.indices[0] & 0x7));
    for (std::uint32_t ScopedTexel = 1; ScopedTexel < 16; ++ScopedTexel)
    {
        ScopedWrite(68 + (ScopedTexel - 1) * 4, 4, static_cast<std::uint64_t>(InBlock.indices[ScopedTexel]));
    }

    for (std::uint32_t ScopedByte = 0; ScopedByte < 8; ++ScopedByte)
    {
        Out[ScopedByte] = static_cast<std::byte>((ScopedLow >> (ScopedByte * 8)) & 0xFFu);
        Out[8 + ScopedByte] = static_cast<std::byte>((ScopedHigh >> (ScopedByte * 8)) & 0xFFu);
    }
}

// Gathers one 4x4 block from an image, clamping at the edges: the block is
void GatherBlock(const std::uint8_t* InImage, std::uint32_t InWidth, std::uint32_t InHeight, std::uint32_t InBlockX,
                 std::uint32_t InBlockY, std::uint32_t InBlockWidth, std::uint32_t InBlockHeight, std::uint8_t* Out)
{
    for (std::uint32_t ScopedY = 0; ScopedY < InBlockHeight; ++ScopedY)
    {
        const std::uint32_t ScopedSourceY = std::min(InBlockY * InBlockHeight + ScopedY, InHeight - 1);
        for (std::uint32_t ScopedX = 0; ScopedX < InBlockWidth; ++ScopedX)
        {
            const std::uint32_t ScopedSourceX = std::min(InBlockX * InBlockWidth + ScopedX, InWidth - 1);
            std::memcpy(Out + (static_cast<std::size_t>(ScopedY) * InBlockWidth + ScopedX) * 4,
                        InImage + (static_cast<std::size_t>(ScopedSourceY) * InWidth + ScopedSourceX) * 4, 4);
        }
    }
}

// ASTC seam
AstcEncoder GAstcEncoder = nullptr;
}

// Public codec building blocks
void EncodeBc7Mode6Block(const std::uint8_t* InRgba8Block, std::span<std::byte, 16> Out)
{
    // Candidate endpoints: 4D PCA, farthest pair, and the channel min/max box.
    float ScopedAxis[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    PrincipalAxis(InRgba8Block, ScopedAxis);

    float ScopedPcaStart0[4];
    float ScopedPcaStart1[4];
    ProjectExtents(InRgba8Block, ScopedAxis, ScopedPcaStart0, ScopedPcaStart1);

    float ScopedFarStart0[4];
    float ScopedFarStart1[4];
    FarthestPair(InRgba8Block, ScopedFarStart0, ScopedFarStart1);

    float ScopedMinimum[4] = {255.0f, 255.0f, 255.0f, 255.0f};
    float ScopedMaximum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (std::int32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            const float ScopedValue = static_cast<float>(InRgba8Block[ScopedTexel * 4 + ScopedChannel]);
            ScopedMinimum[ScopedChannel] = std::min(ScopedMinimum[ScopedChannel], ScopedValue);
            ScopedMaximum[ScopedChannel] = std::max(ScopedMaximum[ScopedChannel], ScopedValue);
        }
    }

    Bc7Block ScopedBest = FitBc7Mode6(InRgba8Block, ScopedPcaStart0, ScopedPcaStart1);
    const Bc7Block ScopedFar = FitBc7Mode6(InRgba8Block, ScopedFarStart0, ScopedFarStart1);
    if (ScopedFar.error < ScopedBest.error)
    {
        ScopedBest = ScopedFar;
    }
    const Bc7Block ScopedBox = FitBc7Mode6(InRgba8Block, ScopedMinimum, ScopedMaximum);
    if (ScopedBox.error < ScopedBest.error)
    {
        ScopedBest = ScopedBox;
    }
    PackBc7Mode6(ScopedBest, Out);
}

bool DecodeBc7Mode6Block(std::span<const std::byte, 16> InBlock, std::uint8_t* InRgba8Block)
{
    std::uint64_t ScopedLow = 0;
    std::uint64_t ScopedHigh = 0;
    for (std::uint32_t ScopedByte = 0; ScopedByte < 8; ++ScopedByte)
    {
        ScopedLow |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(InBlock[ScopedByte])) << (ScopedByte * 8);
        ScopedHigh |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(InBlock[8 + ScopedByte]))
                      << (ScopedByte * 8);
    }
    const auto ScopedRead = [&ScopedLow, &ScopedHigh](std::uint32_t InOffset, std::uint32_t InBits) -> std::uint64_t
    {
        std::uint64_t ScopedValue = 0;
        if (InOffset < 64)
        {
            ScopedValue = ScopedLow >> InOffset;
            if (InOffset + InBits > 64)
            {
                ScopedValue |= ScopedHigh << (64 - InOffset);
            }
        }
        else
        {
            ScopedValue = ScopedHigh >> (InOffset - 64);
        }
        const std::uint64_t ScopedMask = InBits == 64 ? ~0ull : ((1ull << InBits) - 1ull);
        return ScopedValue & ScopedMask;
    };

    // bits 0..5 zero and bit 6 set is BC7 mode 6.
    if ((ScopedRead(0, 7) != 0x40u))
    {
        return false;
    }
    std::int32_t ScopedEndpoint0[4];
    std::int32_t ScopedEndpoint1[4];
    ScopedEndpoint0[0] = static_cast<std::int32_t>((ScopedRead(7, 7) << 1) | ScopedRead(63, 1));
    ScopedEndpoint0[1] = static_cast<std::int32_t>((ScopedRead(21, 7) << 1) | ScopedRead(63, 1));
    ScopedEndpoint0[2] = static_cast<std::int32_t>((ScopedRead(35, 7) << 1) | ScopedRead(63, 1));
    ScopedEndpoint0[3] = static_cast<std::int32_t>((ScopedRead(49, 7) << 1) | ScopedRead(63, 1));
    ScopedEndpoint1[0] = static_cast<std::int32_t>((ScopedRead(14, 7) << 1) | ScopedRead(64, 1));
    ScopedEndpoint1[1] = static_cast<std::int32_t>((ScopedRead(28, 7) << 1) | ScopedRead(64, 1));
    ScopedEndpoint1[2] = static_cast<std::int32_t>((ScopedRead(42, 7) << 1) | ScopedRead(64, 1));
    ScopedEndpoint1[3] = static_cast<std::int32_t>((ScopedRead(56, 7) << 1) | ScopedRead(64, 1));

    std::uint8_t ScopedIndices[16] = {};
    ScopedIndices[0] = static_cast<std::uint8_t>(ScopedRead(65, 3));
    for (std::uint32_t ScopedTexel = 1; ScopedTexel < 16; ++ScopedTexel)
    {
        ScopedIndices[ScopedTexel] = static_cast<std::uint8_t>(ScopedRead(68 + (ScopedTexel - 1) * 4, 4));
    }

    for (std::uint32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        const std::int32_t ScopedWeight = kWeights4[ScopedIndices[ScopedTexel]];
        for (std::int32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            const std::int32_t ScopedValue = (ScopedEndpoint0[ScopedChannel] * (64 - ScopedWeight) +
                                              ScopedEndpoint1[ScopedChannel] * ScopedWeight + 32) >>
                                             6;
            InRgba8Block[ScopedTexel * 4 + ScopedChannel] = static_cast<std::uint8_t>(std::clamp(ScopedValue, 0, 255));
        }
    }
    return true;
}

void EncodeAstcConstantBlock(std::span<const std::uint8_t> InRgba8, std::span<std::byte, 16> Out)
{
    // Constant-colour LDR void-extent block, exactly what astcenc emits for
    constexpr std::uint8_t ScopedKHeader[8] = {0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    std::memcpy(Out.data(), ScopedKHeader, sizeof(ScopedKHeader));

    std::uint64_t ScopedSums[4] = {};
    const std::size_t ScopedTexelCount = InRgba8.size() / 4;
    for (std::size_t ScopedTexel = 0; ScopedTexel < ScopedTexelCount; ++ScopedTexel)
    {
        for (std::size_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            ScopedSums[ScopedChannel] += InRgba8[ScopedTexel * 4 + ScopedChannel];
        }
    }
    for (std::size_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
    {
        const std::uint32_t ScopedAverage =
            ScopedTexelCount == 0
                ? 0u
                : static_cast<std::uint32_t>((ScopedSums[ScopedChannel] + ScopedTexelCount / 2) / ScopedTexelCount);
        const std::uint16_t ScopedUnorm16 = static_cast<std::uint16_t>(ScopedAverage * 257u);
        Out[8 + ScopedChannel * 2] = static_cast<std::byte>(ScopedUnorm16 & 0xFFu);
        Out[9 + ScopedChannel * 2] = static_cast<std::byte>((ScopedUnorm16 >> 8) & 0xFFu);
    }
}

bool DecodeAstcConstantBlock(std::span<const std::byte, 16> InBlock, std::uint8_t* InRgba8)
{
    constexpr std::uint8_t ScopedKHeader[8] = {0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (std::size_t ScopedI = 0; ScopedI < sizeof(ScopedKHeader); ++ScopedI)
    {
        if (static_cast<std::uint8_t>(InBlock[ScopedI]) != ScopedKHeader[ScopedI])
        {
            return false;
        }
    }
    for (std::size_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
    {
        const std::uint32_t ScopedUnorm16 =
            static_cast<std::uint32_t>(static_cast<std::uint8_t>(InBlock[8 + ScopedChannel * 2])) |
            (static_cast<std::uint32_t>(static_cast<std::uint8_t>(InBlock[8 + ScopedChannel * 2 + 1])) << 8);
        const std::uint32_t ScopedValue = (ScopedUnorm16 + 128u) / 257u;
        InRgba8[ScopedChannel] = static_cast<std::uint8_t>(std::min(ScopedValue, 255u));
    }
    return true;
}

bool BuildMipChain(const std::uint8_t* InRgba8, std::uint32_t InWidth, std::uint32_t InHeight,
                   std::uint32_t InMipLevels, bool InSrgb, std::vector<std::uint8_t>& OutRgba8)
{
    if (InRgba8 == nullptr || InWidth == 0 || InHeight == 0 || InMipLevels == 0)
    {
        return false;
    }
    std::uint64_t ScopedTotalBytes = 0;
    for (std::uint32_t ScopedLevel = 0; ScopedLevel < InMipLevels; ++ScopedLevel)
    {
        ScopedTotalBytes += static_cast<std::uint64_t>(Assets::MipExtent(InWidth, ScopedLevel)) *
                            Assets::MipExtent(InHeight, ScopedLevel) * 4;
    }
    if (ScopedTotalBytes > std::numeric_limits<std::size_t>::max())
    {
        return false;
    }
    OutRgba8.assign(static_cast<std::size_t>(ScopedTotalBytes), 0);
    std::memcpy(OutRgba8.data(), InRgba8, static_cast<std::size_t>(InWidth) * InHeight * 4);

    std::uint32_t ScopedPreviousWidth = InWidth;
    std::uint32_t ScopedPreviousHeight = InHeight;
    const std::uint8_t* ScopedPrevious = OutRgba8.data();
    std::uint8_t* ScopedDestination = OutRgba8.data() + static_cast<std::size_t>(InWidth) * InHeight * 4;
    for (std::uint32_t ScopedLevel = 1; ScopedLevel < InMipLevels; ++ScopedLevel)
    {
        const std::uint32_t ScopedLevelWidth = Assets::MipExtent(InWidth, ScopedLevel);
        const std::uint32_t ScopedLevelHeight = Assets::MipExtent(InHeight, ScopedLevel);
        DownsampleBox(ScopedPrevious, ScopedPreviousWidth, ScopedPreviousHeight, ScopedDestination, ScopedLevelWidth,
                      ScopedLevelHeight, InSrgb);
        ScopedPrevious = ScopedDestination;
        ScopedPreviousWidth = ScopedLevelWidth;
        ScopedPreviousHeight = ScopedLevelHeight;
        ScopedDestination += static_cast<std::size_t>(ScopedLevelWidth) * ScopedLevelHeight * 4;
    }
    return true;
}

void SetAstcEncoder(AstcEncoder InEncoder) noexcept
{
    GAstcEncoder = InEncoder;
}

// Block compression
namespace
{

void CompressBc7(const std::uint8_t* InRgba8, std::uint32_t InWidth, std::uint32_t InHeight,
                 std::vector<std::byte>& Out)
{
    const std::uint32_t ScopedBlocksX = (InWidth + 3) / 4;
    const std::uint32_t ScopedBlocksY = (InHeight + 3) / 4;
    Out.resize(static_cast<std::size_t>(ScopedBlocksX) * ScopedBlocksY * 16);
    std::uint8_t ScopedBlock[16 * 4] = {};
    std::size_t ScopedCursor = 0;
    for (std::uint32_t ScopedBlockY = 0; ScopedBlockY < ScopedBlocksY; ++ScopedBlockY)
    {
        for (std::uint32_t ScopedBlockX = 0; ScopedBlockX < ScopedBlocksX; ++ScopedBlockX)
        {
            GatherBlock(InRgba8, InWidth, InHeight, ScopedBlockX, ScopedBlockY, 4, 4, ScopedBlock);
            EncodeBc7Mode6Block(ScopedBlock, std::span<std::byte, 16>(Out.data() + ScopedCursor, 16));
            ScopedCursor += 16;
        }
    }
}

void CompressAstc(const std::uint8_t* InRgba8, std::uint32_t InWidth, std::uint32_t InHeight,
                  const TextureCookOptions& InOptions, std::vector<std::byte>& Out)
{
    if (GAstcEncoder != nullptr && GAstcEncoder(InRgba8, InWidth, InHeight, InOptions, Out))
    {
        // A linked high-quality encoder produced the blocks
        return;
    }

    const std::uint32_t ScopedBlockWidth = InOptions.astcBlock == AstcBlock::Block6x6 ? 6u : 4u;
    const std::uint32_t ScopedBlockHeight = ScopedBlockWidth;
    const std::uint32_t ScopedBlocksX = (InWidth + ScopedBlockWidth - 1) / ScopedBlockWidth;
    const std::uint32_t ScopedBlocksY = (InHeight + ScopedBlockHeight - 1) / ScopedBlockHeight;
    Out.resize(static_cast<std::size_t>(ScopedBlocksX) * ScopedBlocksY * 16);
    std::array<std::uint8_t, 6 * 6 * 4> ScopedBlock{};
    std::size_t ScopedCursor = 0;
    for (std::uint32_t ScopedBlockY = 0; ScopedBlockY < ScopedBlocksY; ++ScopedBlockY)
    {
        for (std::uint32_t ScopedBlockX = 0; ScopedBlockX < ScopedBlocksX; ++ScopedBlockX)
        {
            GatherBlock(InRgba8, InWidth, InHeight, ScopedBlockX, ScopedBlockY, ScopedBlockWidth, ScopedBlockHeight,
                        ScopedBlock.data());
            EncodeAstcConstantBlock(
                std::span<const std::uint8_t>(ScopedBlock.data(), ScopedBlockWidth * ScopedBlockHeight * 4),
                std::span<std::byte, 16>(Out.data() + ScopedCursor, 16));
            ScopedCursor += 16;
        }
    }
}

// Input validation
bool ValidateSource(std::span<const std::byte> InSource, std::string_view InSourceName, std::string* InError)
{
    const std::string ScopedExtension = ExtensionOf(InSourceName);
    if (ScopedExtension == ".jpg" || ScopedExtension == ".jpeg" || ScopedExtension == ".jpe" ||
        ScopedExtension == ".jfif")
    {
        SetError(InError, "JPEG sources are not supported (lossy): cook a PNG or TGA instead");
        return false;
    }
    if (ScopedExtension != ".png" && ScopedExtension != ".tga")
    {
        SetError(InError, "texture cooker accepts PNG and TGA only (got '" + ScopedExtension + "')");
        return false;
    }
    if (IsJpegMagic(InSource))
    {
        // Renamed JPEG: extension looked innocent but the bytes are lossy.
        SetError(InError, "JPEG data found inside a '" + ScopedExtension +
                              "' file: rename does not make a lossy source lossless; cook a PNG or TGA instead");
        return false;
    }
    if (ScopedExtension == ".png")
    {
        if (!IsPngMagic(InSource))
        {
            SetError(InError, "file extension is .png but the bytes are not a PNG");
            return false;
        }
    }
    else if (IsPngMagic(InSource))
    {
        SetError(InError, "file extension is .tga but the bytes are a PNG");
        return false;
    }
    else if (!LooksLikeTga(InSource))
    {
        SetError(InError, "file extension is .tga but the bytes are not a valid TGA header");
        return false;
    }
    return true;
}

[[nodiscard]] bool ReadFileBytes(const std::filesystem::path& InPath, std::vector<std::byte>* InBytes)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.seekg(0, std::ios::end);
    const std::streamoff ScopedSize = ScopedStream.tellg();
    if (ScopedSize < 0)
    {
        return false;
    }
    ScopedStream.seekg(0, std::ios::beg);
    InBytes->resize(static_cast<std::size_t>(ScopedSize));
    if (ScopedSize > 0)
    {
        ScopedStream.read(reinterpret_cast<char*>(InBytes->data()), static_cast<std::streamsize>(ScopedSize));
    }
    return static_cast<bool>(ScopedStream);
}
}

// Cook entry points
bool CookTextureBytes(std::span<const std::byte> InSource, std::string_view InSourceName,
                      const TextureCookOptions& InOptions, CookedTexture* Out, std::string* InError)
{
    if (Out == nullptr)
    {
        SetError(InError, "CookTextureBytes needs an output object");
        return false;
    }
    if (InSource.empty())
    {
        SetError(InError, "texture source is empty");
        return false;
    }
    if (InSource.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        SetError(InError, "texture source is too large for the decoder");
        return false;
    }
    if (!ValidateSource(InSource, InSourceName, InError))
    {
        return false;
    }

    stbi_set_flip_vertically_on_load(0);
    int ScopedWidth = 0;
    int ScopedHeight = 0;
    int ScopedChannels = 0;
    stbi_uc* ScopedPixels =
        stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(InSource.data()), static_cast<int>(InSource.size()),
                              &ScopedWidth, &ScopedHeight, &ScopedChannels, 4);
    if (ScopedPixels == nullptr)
    {
        const char* ScopedReason = stbi_failure_reason();
        SetError(InError,
                 std::string("image decode failed: ") + (ScopedReason != nullptr ? ScopedReason : "unknown error"));
        return false;
    }
    if (ScopedWidth <= 0 || ScopedHeight <= 0 || ScopedWidth > 16384 || ScopedHeight > 16384)
    {
        stbi_image_free(ScopedPixels);
        SetError(InError, "texture dimensions are out of range (1..16384)");
        return false;
    }

    const bool ScopedHasAlpha = ScopedChannels == 4 || ScopedChannels == 2;
    Assets::TextureFormat ScopedFormat = Assets::TextureFormat::Bc7Srgb;
    if (InOptions.target == CookTarget::Desktop)
    {
        ScopedFormat = InOptions.srgb ? Assets::TextureFormat::Bc7Srgb : Assets::TextureFormat::Bc7Unorm;
    }
    else if (InOptions.astcBlock == AstcBlock::Block6x6)
    {
        ScopedFormat = InOptions.srgb ? Assets::TextureFormat::Astc6x6Srgb : Assets::TextureFormat::Astc6x6Unorm;
    }
    else
    {
        ScopedFormat = InOptions.srgb ? Assets::TextureFormat::Astc4x4Srgb : Assets::TextureFormat::Astc4x4Unorm;
    }

    const std::uint32_t ScopedMaximumMips = Assets::MaxMipLevels(
        static_cast<std::uint32_t>(ScopedWidth), static_cast<std::uint32_t>(ScopedHeight), ScopedFormat);
    std::uint32_t ScopedMipLevels =
        InOptions.mipLevels == 0 ? ScopedMaximumMips : std::min(InOptions.mipLevels, ScopedMaximumMips);
    ScopedMipLevels = std::max(ScopedMipLevels, 1u);

    std::vector<std::uint8_t> ScopedChain;
    const bool ScopedBuilt =
        BuildMipChain(ScopedPixels, static_cast<std::uint32_t>(ScopedWidth), static_cast<std::uint32_t>(ScopedHeight),
                      ScopedMipLevels, InOptions.srgb, ScopedChain);
    stbi_image_free(ScopedPixels);
    if (!ScopedBuilt)
    {
        SetError(InError, "mip generation failed");
        return false;
    }

    std::vector<std::byte> ScopedTexels;
    std::size_t ScopedOffset = 0;
    for (std::uint32_t ScopedLevel = 0; ScopedLevel < ScopedMipLevels; ++ScopedLevel)
    {
        const std::uint32_t ScopedLevelWidth = Assets::MipExtent(static_cast<std::uint32_t>(ScopedWidth), ScopedLevel);
        const std::uint32_t ScopedLevelHeight =
            Assets::MipExtent(static_cast<std::uint32_t>(ScopedHeight), ScopedLevel);
        std::vector<std::byte> ScopedCompressed;
        if (InOptions.target == CookTarget::Desktop)
        {
            CompressBc7(ScopedChain.data() + ScopedOffset, ScopedLevelWidth, ScopedLevelHeight, ScopedCompressed);
        }
        else
        {
            CompressAstc(ScopedChain.data() + ScopedOffset, ScopedLevelWidth, ScopedLevelHeight, InOptions,
                         ScopedCompressed);
        }
        ScopedTexels.insert(ScopedTexels.end(), ScopedCompressed.begin(), ScopedCompressed.end());
        ScopedOffset += static_cast<std::size_t>(ScopedLevelWidth) * ScopedLevelHeight * 4;
    }

    std::uint32_t ScopedFlags = 0;
    if (Assets::IsSrgb(ScopedFormat))
    {
        ScopedFlags |= Assets::kTextureFlagSrgb;
    }
    if (ScopedHasAlpha)
    {
        ScopedFlags |= Assets::kTextureFlagAlpha;
    }

    Out->payload.clear();
    if (!Assets::AppendCookedTexture(Out->payload, ScopedFormat, static_cast<std::uint32_t>(ScopedWidth),
                                     static_cast<std::uint32_t>(ScopedHeight), 1, ScopedMipLevels, ScopedFlags,
                                     std::span<const std::byte>(ScopedTexels.data(), ScopedTexels.size()), InError))
    {
        return false;
    }
    Out->format = ScopedFormat;
    Out->width = static_cast<std::uint32_t>(ScopedWidth);
    Out->height = static_cast<std::uint32_t>(ScopedHeight);
    Out->mipLevels = ScopedMipLevels;
    Out->hasAlpha = ScopedHasAlpha;
    return true;
}

bool CookTextureFile(const std::filesystem::path& InSource, const TextureCookOptions& InOptions, CookedTexture* Out,
                     std::string* InError)
{
    std::vector<std::byte> ScopedBytes;
    if (!ReadFileBytes(InSource, &ScopedBytes))
    {
        SetError(InError, "cannot read texture source: " + InSource.string());
        return false;
    }
    // The name carries the extension the cooker validates; keep the full path
    return CookTextureBytes(ScopedBytes, InSource.generic_string(), InOptions, Out, InError);
}
}
