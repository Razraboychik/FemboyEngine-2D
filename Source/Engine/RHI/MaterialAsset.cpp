// FemboyEngine2D - cooked material and material graph wire formats.
#include "Engine/RHI/MaterialAsset.hpp"

#include <bit>
#include <cstring>

namespace Fby::Render
{
namespace
{

void PutU8(std::vector<std::byte>* OutBytes, std::uint8_t InValue)
{
    OutBytes->push_back(std::byte{InValue});
}

void PutU16(std::vector<std::byte>* OutBytes, std::uint16_t InValue)
{
    PutU8(OutBytes, static_cast<std::uint8_t>(InValue & 0xFFu));
    PutU8(OutBytes, static_cast<std::uint8_t>((InValue >> 8) & 0xFFu));
}

void PutU32(std::vector<std::byte>* OutBytes, std::uint32_t InValue)
{
    for (std::uint32_t ScopedShift = 0; ScopedShift < 32; ScopedShift += 8)
    {
        PutU8(OutBytes, static_cast<std::uint8_t>((InValue >> ScopedShift) & 0xFFu));
    }
}

void PutU64(std::vector<std::byte>* OutBytes, std::uint64_t InValue)
{
    for (std::uint32_t ScopedShift = 0; ScopedShift < 64; ScopedShift += 8)
    {
        PutU8(OutBytes, static_cast<std::uint8_t>((InValue >> ScopedShift) & 0xFFu));
    }
}

void PutF32(std::vector<std::byte>* OutBytes, float InValue)
{
    PutU32(OutBytes, std::bit_cast<std::uint32_t>(InValue));
}

// Bounded little-endian reader. Every accessor returns false once the payload
// runs out, so the decoders check one flag instead of a size before each field.
struct ByteReader
{
    std::span<const std::byte> bytes{};
    std::size_t offset = 0;

    [[nodiscard]] bool U8(std::uint8_t* OutValue) noexcept
    {
        if (offset + 1 > bytes.size())
        {
            return false;
        }
        *OutValue = std::to_integer<std::uint8_t>(bytes[offset]);
        offset += 1;
        return true;
    }

    [[nodiscard]] bool U16(std::uint16_t* OutValue) noexcept
    {
        std::uint8_t ScopedLow = 0;
        std::uint8_t ScopedHigh = 0;
        if (!U8(&ScopedLow) || !U8(&ScopedHigh))
        {
            return false;
        }
        *OutValue = static_cast<std::uint16_t>(ScopedLow | (static_cast<std::uint16_t>(ScopedHigh) << 8));
        return true;
    }

    [[nodiscard]] bool U32(std::uint32_t* OutValue) noexcept
    {
        std::uint32_t ScopedValue = 0;
        for (std::uint32_t ScopedShift = 0; ScopedShift < 32; ScopedShift += 8)
        {
            std::uint8_t ScopedByte = 0;
            if (!U8(&ScopedByte))
            {
                return false;
            }
            ScopedValue |= static_cast<std::uint32_t>(ScopedByte) << ScopedShift;
        }
        *OutValue = ScopedValue;
        return true;
    }

    [[nodiscard]] bool U64(std::uint64_t* OutValue) noexcept
    {
        std::uint64_t ScopedValue = 0;
        for (std::uint32_t ScopedShift = 0; ScopedShift < 64; ScopedShift += 8)
        {
            std::uint8_t ScopedByte = 0;
            if (!U8(&ScopedByte))
            {
                return false;
            }
            ScopedValue |= static_cast<std::uint64_t>(ScopedByte) << ScopedShift;
        }
        *OutValue = ScopedValue;
        return true;
    }

    [[nodiscard]] bool F32(float* OutValue) noexcept
    {
        std::uint32_t ScopedBits = 0;
        if (!U32(&ScopedBits))
        {
            return false;
        }
        *OutValue = std::bit_cast<float>(ScopedBits);
        return true;
    }

    void Skip(std::size_t InBytes) noexcept
    {
        offset += InBytes;
    }
};

void SetError(MaterialAssetError* OutError, MaterialAssetError InError) noexcept
{
    if (OutError != nullptr)
    {
        *OutError = InError;
    }
}

[[nodiscard]] bool IsSpirvStage(std::span<const std::byte> InBytes) noexcept
{
    if (InBytes.empty() || InBytes.size() % sizeof(std::uint32_t) != 0)
    {
        return false;
    }
    if (InBytes.size() < static_cast<std::size_t>(kSpirvMinimumWords) * sizeof(std::uint32_t))
    {
        return false;
    }
    std::uint32_t ScopedMagic = 0;
    std::memcpy(&ScopedMagic, InBytes.data(), sizeof(ScopedMagic));
    return ScopedMagic == kSpirvMagicWord;
}
}

bool EncodeMaterialGraph(const MaterialGraph& InGraph, std::vector<std::byte>* OutBytes) noexcept
{
    if (OutBytes == nullptr)
    {
        return false;
    }
    const std::uint32_t ScopedNodes = InGraph.NodeCount();
    const std::uint32_t ScopedPins = InGraph.PinCount();
    const std::uint32_t ScopedParams = InGraph.ParamCount();
    if (ScopedNodes > kMaxMaterialNodes || ScopedPins > kMaxMaterialPins || ScopedParams > kMaxMaterialParams)
    {
        return false;
    }

    const std::size_t ScopedBytes = static_cast<std::size_t>(kMaterialGraphHeaderBytes) +
                                    static_cast<std::size_t>(ScopedNodes) * kMaterialGraphNodeBytes +
                                    static_cast<std::size_t>(ScopedPins) * kMaterialGraphPinBytes +
                                    static_cast<std::size_t>(ScopedParams) * kMaterialGraphParamBytes;
    OutBytes->clear();
    OutBytes->reserve(ScopedBytes);

    PutU32(OutBytes, kMaterialGraphMagic);
    PutU16(OutBytes, kMaterialGraphVersion);
    PutU16(OutBytes, 0);
    PutU16(OutBytes, static_cast<std::uint16_t>(ScopedNodes));
    PutU16(OutBytes, static_cast<std::uint16_t>(ScopedPins));
    PutU16(OutBytes, static_cast<std::uint16_t>(ScopedParams));
    PutU16(OutBytes, 0);

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedNodes; ++ScopedIndex)
    {
        const MaterialNode& ScopedNode = InGraph.Node(static_cast<std::uint16_t>(ScopedIndex));
        PutU8(OutBytes, static_cast<std::uint8_t>(ScopedNode.kind));
        PutU16(OutBytes, ScopedNode.firstInput);
        PutU16(OutBytes, ScopedNode.inputCount);
        PutU16(OutBytes, ScopedNode.paramIndex);
        PutU16(OutBytes, ScopedNode.flags);
        PutF32(OutBytes, ScopedNode.uiPosition.X);
        PutF32(OutBytes, ScopedNode.uiPosition.Y);
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedPins; ++ScopedIndex)
    {
        const MaterialPin& ScopedPin = InGraph.Pin(static_cast<std::uint16_t>(ScopedIndex));
        PutU16(OutBytes, ScopedPin.node);
        PutU8(OutBytes, ScopedPin.slot);
        PutU8(OutBytes, static_cast<std::uint8_t>(ScopedPin.type));
        PutU16(OutBytes, ScopedPin.sourceNode);
        PutU8(OutBytes, ScopedPin.sourceSlot);
        PutU8(OutBytes, ScopedPin.flags);
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedParams; ++ScopedIndex)
    {
        const MaterialParam& ScopedParam = InGraph.Param(static_cast<std::uint16_t>(ScopedIndex));
        PutU8(OutBytes, static_cast<std::uint8_t>(ScopedParam.kind));
        PutU8(OutBytes, 0);
        PutU8(OutBytes, 0);
        PutU8(OutBytes, 0);
        PutF32(OutBytes, ScopedParam.X);
        PutF32(OutBytes, ScopedParam.Y);
        PutF32(OutBytes, ScopedParam.Z);
        PutF32(OutBytes, ScopedParam.W);
        PutU32(OutBytes, ScopedParam.textureLayer);
    }

    return OutBytes->size() == ScopedBytes;
}

bool DecodeMaterialGraph(std::span<const std::byte> InBytes, MaterialGraph* OutGraph,
                         MaterialAssetError* OutError) noexcept
{
    SetError(OutError, MaterialAssetError::None);
    if (OutGraph == nullptr)
    {
        SetError(OutError, MaterialAssetError::BadCounts);
        return false;
    }
    if (InBytes.size() < kMaterialGraphHeaderBytes)
    {
        SetError(OutError, MaterialAssetError::TooSmall);
        return false;
    }

    ByteReader ScopedReader{InBytes, 0};
    std::uint32_t ScopedMagic = 0;
    std::uint16_t ScopedVersion = 0;
    std::uint16_t ScopedFlags = 0;
    std::uint16_t ScopedNodes = 0;
    std::uint16_t ScopedPins = 0;
    std::uint16_t ScopedParams = 0;
    std::uint16_t ScopedReserved = 0;
    if (!ScopedReader.U32(&ScopedMagic) || !ScopedReader.U16(&ScopedVersion) || !ScopedReader.U16(&ScopedFlags) ||
        !ScopedReader.U16(&ScopedNodes) || !ScopedReader.U16(&ScopedPins) || !ScopedReader.U16(&ScopedParams) ||
        !ScopedReader.U16(&ScopedReserved))
    {
        SetError(OutError, MaterialAssetError::TooSmall);
        return false;
    }
    if (ScopedMagic != kMaterialGraphMagic)
    {
        SetError(OutError, MaterialAssetError::BadMagic);
        return false;
    }
    if (ScopedVersion != kMaterialGraphVersion)
    {
        SetError(OutError, MaterialAssetError::UnsupportedVersion);
        return false;
    }
    if (ScopedFlags != 0 || ScopedNodes == 0 || ScopedNodes > kMaxMaterialNodes || ScopedPins > kMaxMaterialPins ||
        ScopedParams > kMaxMaterialParams)
    {
        SetError(OutError, MaterialAssetError::BadCounts);
        return false;
    }

    const std::size_t ScopedExpected = static_cast<std::size_t>(kMaterialGraphHeaderBytes) +
                                       static_cast<std::size_t>(ScopedNodes) * kMaterialGraphNodeBytes +
                                       static_cast<std::size_t>(ScopedPins) * kMaterialGraphPinBytes +
                                       static_cast<std::size_t>(ScopedParams) * kMaterialGraphParamBytes;
    if (InBytes.size() != ScopedExpected)
    {
        SetError(OutError, MaterialAssetError::BadPayloadSize);
        return false;
    }

    if (!OutGraph->AdoptCounts(ScopedNodes, ScopedPins, ScopedParams))
    {
        SetError(OutError, MaterialAssetError::BadCounts);
        return false;
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedNodes; ++ScopedIndex)
    {
        MaterialNode& ScopedNode = OutGraph->Node(static_cast<std::uint16_t>(ScopedIndex));
        std::uint8_t ScopedKind = 0;
        if (!ScopedReader.U8(&ScopedKind) || !ScopedReader.U16(&ScopedNode.firstInput) ||
            !ScopedReader.U16(&ScopedNode.inputCount) || !ScopedReader.U16(&ScopedNode.paramIndex) ||
            !ScopedReader.U16(&ScopedNode.flags) || !ScopedReader.F32(&ScopedNode.uiPosition.X) ||
            !ScopedReader.F32(&ScopedNode.uiPosition.Y))
        {
            SetError(OutError, MaterialAssetError::BadPayloadSize);
            return false;
        }
        if (ScopedKind >= static_cast<std::uint8_t>(MaterialNodeKind::Count))
        {
            SetError(OutError, MaterialAssetError::BadCounts);
            return false;
        }
        ScopedNode.kind = static_cast<MaterialNodeKind>(ScopedKind);
        if (ScopedNode.paramIndex != kInvalidMaterialParam && ScopedNode.paramIndex >= ScopedParams)
        {
            SetError(OutError, MaterialAssetError::BadCounts);
            return false;
        }
        if (static_cast<std::uint32_t>(ScopedNode.firstInput) + ScopedNode.inputCount > ScopedPins)
        {
            SetError(OutError, MaterialAssetError::BadCounts);
            return false;
        }
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedPins; ++ScopedIndex)
    {
        MaterialPin& ScopedPin = OutGraph->Pin(static_cast<std::uint16_t>(ScopedIndex));
        std::uint8_t ScopedType = 0;
        if (!ScopedReader.U16(&ScopedPin.node) || !ScopedReader.U8(&ScopedPin.slot) || !ScopedReader.U8(&ScopedType) ||
            !ScopedReader.U16(&ScopedPin.sourceNode) || !ScopedReader.U8(&ScopedPin.sourceSlot) ||
            !ScopedReader.U8(&ScopedPin.flags))
        {
            SetError(OutError, MaterialAssetError::BadPayloadSize);
            return false;
        }
        if (ScopedType >= static_cast<std::uint8_t>(MaterialValueType::Count))
        {
            SetError(OutError, MaterialAssetError::BadCounts);
            return false;
        }
        ScopedPin.type = static_cast<MaterialValueType>(ScopedType);
        if (ScopedPin.node >= ScopedNodes ||
            (ScopedPin.sourceNode != kInvalidMaterialNode && ScopedPin.sourceNode >= ScopedNodes))
        {
            SetError(OutError, MaterialAssetError::BadCounts);
            return false;
        }
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedParams; ++ScopedIndex)
    {
        MaterialParam& ScopedParam = OutGraph->Param(static_cast<std::uint16_t>(ScopedIndex));
        std::uint8_t ScopedKind = 0;
        std::uint8_t ScopedPad[3] = {};
        if (!ScopedReader.U8(&ScopedKind) || !ScopedReader.U8(&ScopedPad[0]) || !ScopedReader.U8(&ScopedPad[1]) ||
            !ScopedReader.U8(&ScopedPad[2]) || !ScopedReader.F32(&ScopedParam.X) || !ScopedReader.F32(&ScopedParam.Y) ||
            !ScopedReader.F32(&ScopedParam.Z) || !ScopedReader.F32(&ScopedParam.W) ||
            !ScopedReader.U32(&ScopedParam.textureLayer))
        {
            SetError(OutError, MaterialAssetError::BadPayloadSize);
            return false;
        }
        if (ScopedKind >= static_cast<std::uint8_t>(MaterialParamKind::Count) || ScopedPad[0] != 0 ||
            ScopedPad[1] != 0 || ScopedPad[2] != 0)
        {
            SetError(OutError, MaterialAssetError::BadCounts);
            return false;
        }
        ScopedParam.kind = static_cast<MaterialParamKind>(ScopedKind);
    }

    return true;
}

bool EncodeCookedMaterial(std::uint64_t InGraphHash, std::uint16_t InContract,
                          std::span<const std::byte> InVertexSpirv, std::span<const std::byte> InFragmentSpirv,
                          std::vector<std::byte>* OutBytes) noexcept
{
    if (OutBytes == nullptr || InGraphHash == 0)
    {
        return false;
    }
    if ((InContract & ~static_cast<std::uint16_t>(MaterialContract::MaterialContractKnown)) != 0)
    {
        return false;
    }
    const bool ScopedGeneratedVertex =
        (InContract & static_cast<std::uint16_t>(MaterialContract::MaterialContractGeneratedVertexStage)) != 0;
    if (ScopedGeneratedVertex != !InVertexSpirv.empty())
    {
        return false;
    }
    if (!IsSpirvStage(InFragmentSpirv) || (!InVertexSpirv.empty() && !IsSpirvStage(InVertexSpirv)))
    {
        return false;
    }
    if (InVertexSpirv.size() > kMaxCookedStageBytes || InFragmentSpirv.size() > kMaxCookedStageBytes)
    {
        return false;
    }

    const std::size_t ScopedBytes = static_cast<std::size_t>(kMaterialAssetHeaderBytes) + InVertexSpirv.size() +
                                    InFragmentSpirv.size();
    OutBytes->clear();
    OutBytes->reserve(ScopedBytes);

    PutU32(OutBytes, kMaterialAssetMagic);
    PutU16(OutBytes, kMaterialAssetVersion);
    PutU16(OutBytes, InContract);
    PutU64(OutBytes, InGraphHash);
    PutU32(OutBytes, static_cast<std::uint32_t>(InVertexSpirv.size()));
    PutU32(OutBytes, static_cast<std::uint32_t>(InFragmentSpirv.size()));
    PutU64(OutBytes, 0);
    OutBytes->insert(OutBytes->end(), InVertexSpirv.begin(), InVertexSpirv.end());
    OutBytes->insert(OutBytes->end(), InFragmentSpirv.begin(), InFragmentSpirv.end());
    return OutBytes->size() == ScopedBytes;
}

bool DecodeCookedMaterial(std::span<const std::byte> InBytes, CookedMaterialView* OutView,
                          MaterialAssetError* OutError) noexcept
{
    SetError(OutError, MaterialAssetError::None);
    if (OutView == nullptr)
    {
        SetError(OutError, MaterialAssetError::BadCounts);
        return false;
    }
    if (InBytes.size() < kMaterialAssetHeaderBytes)
    {
        SetError(OutError, MaterialAssetError::TooSmall);
        return false;
    }

    ByteReader ScopedReader{InBytes, 0};
    std::uint32_t ScopedMagic = 0;
    std::uint16_t ScopedVersion = 0;
    std::uint16_t ScopedContract = 0;
    std::uint64_t ScopedHash = 0;
    std::uint32_t ScopedVertexBytes = 0;
    std::uint32_t ScopedFragmentBytes = 0;
    std::uint64_t ScopedReserved = 0;
    if (!ScopedReader.U32(&ScopedMagic) || !ScopedReader.U16(&ScopedVersion) || !ScopedReader.U16(&ScopedContract) ||
        !ScopedReader.U64(&ScopedHash) || !ScopedReader.U32(&ScopedVertexBytes) ||
        !ScopedReader.U32(&ScopedFragmentBytes) || !ScopedReader.U64(&ScopedReserved))
    {
        SetError(OutError, MaterialAssetError::TooSmall);
        return false;
    }
    if (ScopedMagic != kMaterialAssetMagic)
    {
        SetError(OutError, MaterialAssetError::BadMagic);
        return false;
    }
    if (ScopedVersion != kMaterialAssetVersion)
    {
        SetError(OutError, MaterialAssetError::UnsupportedVersion);
        return false;
    }
    if (ScopedHash == 0 || ScopedReserved != 0)
    {
        SetError(OutError, MaterialAssetError::BadContract);
        return false;
    }
    if ((ScopedContract & ~static_cast<std::uint16_t>(MaterialContract::MaterialContractKnown)) != 0)
    {
        SetError(OutError, MaterialAssetError::BadContract);
        return false;
    }
    if (ScopedVertexBytes > kMaxCookedStageBytes || ScopedFragmentBytes > kMaxCookedStageBytes ||
        ScopedFragmentBytes == 0)
    {
        SetError(OutError, MaterialAssetError::BadPayloadSize);
        return false;
    }
    const std::size_t ScopedExpected = static_cast<std::size_t>(kMaterialAssetHeaderBytes) + ScopedVertexBytes +
                                       ScopedFragmentBytes;
    if (InBytes.size() != ScopedExpected)
    {
        SetError(OutError, MaterialAssetError::BadPayloadSize);
        return false;
    }

    const std::size_t ScopedVertexOffset = kMaterialAssetHeaderBytes;
    const std::span<const std::byte> ScopedVertex = InBytes.subspan(ScopedVertexOffset, ScopedVertexBytes);
    const std::span<const std::byte> ScopedFragment =
        InBytes.subspan(ScopedVertexOffset + ScopedVertexBytes, ScopedFragmentBytes);
    const bool ScopedGeneratedVertex =
        (ScopedContract & static_cast<std::uint16_t>(MaterialContract::MaterialContractGeneratedVertexStage)) != 0;
    if (ScopedGeneratedVertex != !ScopedVertex.empty())
    {
        SetError(OutError, MaterialAssetError::BadContract);
        return false;
    }
    if (!IsSpirvStage(ScopedFragment) || (!ScopedVertex.empty() && !IsSpirvStage(ScopedVertex)))
    {
        SetError(OutError, MaterialAssetError::BadSpirv);
        return false;
    }

    OutView->graphHash = ScopedHash;
    OutView->contract = ScopedContract;
    OutView->vertexSpirv = ScopedVertex;
    OutView->fragmentSpirv = ScopedFragment;
    return true;
}
}
