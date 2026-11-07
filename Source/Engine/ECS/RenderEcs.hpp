// FemboyEngine2D - GPU-visible render ECS chunks.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/RHI/RenderItem.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Fby::Render
{

// One chunk is the smallest unit the uploader maps and copies.
inline constexpr std::uint32_t ChunkBytes = 16u * 1024u;
// The header occupies the first 64 bytes; every column starts after it.
inline constexpr std::uint32_t ChunkHeaderBytes = 64u;
// The payload is split into 512-byte blocks; one bit tracks each block.
inline constexpr std::uint32_t DirtyBlockBytes = 512u;
inline constexpr std::uint32_t DirtyBlockCount = ChunkBytes / DirtyBlockBytes;
inline constexpr std::uint32_t ChunkCapacity = 384u;
inline constexpr std::uint32_t MaxChunks = 4096u;

// Stable GPU-side identity. It never contains a host pointer.
struct GpuEntity
{
    std::uint32_t Chunk = 0xFFFFFFFFu;
    std::uint32_t Slot = 0xFFFFFFFFu;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Chunk != 0xFFFFFFFFu && Slot != 0xFFFFFFFFu;
    }
};

static_assert(sizeof(GpuEntity) == 8);

// The header is the only part the GPU reads before it touches a column.
struct alignas(64) ChunkHeader
{
    std::uint32_t Archetype = 0;
    std::uint32_t Capacity = 0;
    std::uint32_t Count = 0;
    std::uint32_t Version = 0;
    std::uint32_t PayloadBytes = 0;
    std::uint32_t DirtyBlockMask = 0;
    std::uint32_t ActiveCount = 0;
    std::uint32_t Flags = 0;
    std::uint64_t EntityBase = 0;
    std::uint32_t ColumnOffset[6] = {};
};

static_assert(sizeof(ChunkHeader) == ChunkHeaderBytes);

// The CPU mirror of one GPU chunk. The uploader copies only dirty blocks.
struct alignas(64) RenderChunk
{
    ChunkHeader Header;
    std::array<std::byte, ChunkBytes - ChunkHeaderBytes> Payload;
};

static_assert(sizeof(RenderChunk) == ChunkBytes);
static_assert(DirtyBlockCount == 32u);

// Marks the 512-byte blocks that cover one written range.
inline void MarkDirty(RenderChunk& InChunk, std::uint32_t InOffset, std::uint32_t InBytes) noexcept
{
    if (InBytes == 0)
    {
        return;
    }
    const std::uint32_t ScopedFirst = InOffset / DirtyBlockBytes;
    const std::uint32_t ScopedLast = (InOffset + InBytes - 1u) / DirtyBlockBytes;
    if (ScopedLast >= DirtyBlockCount)
    {
        return;
    }
    const std::uint32_t ScopedCount = ScopedLast - ScopedFirst + 1u;
    const std::uint32_t ScopedMask = ScopedCount == DirtyBlockCount ? 0xFFFFFFFFu : ((1u << ScopedCount) - 1u);
    InChunk.Header.DirtyBlockMask |= ScopedMask << ScopedFirst;
}

// One contiguous byte range the uploader can copy with vkCmdCopyBuffer.
struct ChunkCopyRange
{
    std::uint32_t Offset = 0;
    std::uint32_t Bytes = 0;
};

// Coalesces the dirty-block bitmask into contiguous copy ranges. The caller
// supplies the destination span; the return value is the number written.
[[nodiscard]] inline std::uint32_t CollectDirtyRanges(const RenderChunk& InChunk,
                                                      std::span<ChunkCopyRange> OutRanges) noexcept
{
    if (OutRanges.empty())
    {
        return 0;
    }
    std::uint32_t ScopedWritten = 0;
    std::uint32_t ScopedBlock = 0;
    while (ScopedBlock < DirtyBlockCount && ScopedWritten < OutRanges.size())
    {
        if ((InChunk.Header.DirtyBlockMask & (1u << ScopedBlock)) == 0u)
        {
            ++ScopedBlock;
            continue;
        }
        const std::uint32_t ScopedFirst = ScopedBlock;
        while (ScopedBlock + 1u < DirtyBlockCount &&
               (InChunk.Header.DirtyBlockMask & (1u << (ScopedBlock + 1u))) != 0u)
        {
            ++ScopedBlock;
        }
        OutRanges[ScopedWritten].Offset = ScopedFirst * DirtyBlockBytes;
        OutRanges[ScopedWritten].Bytes = (ScopedBlock - ScopedFirst + 1u) * DirtyBlockBytes;
        ++ScopedWritten;
        ++ScopedBlock;
    }
    return ScopedWritten;
}

// Clears the dirty mask after the uploader has recorded the copies.
inline void ClearDirty(RenderChunk& InChunk) noexcept
{
    InChunk.Header.DirtyBlockMask = 0u;
}

// IEEE 754 binary16 bits. The helpers keep the layout independent of the
// compiler's native half type.
using Float16Bits = std::uint16_t;

// Converts one float to binary16 bits with round-to-nearest ties away from zero.
[[nodiscard]] inline Float16Bits PackFloat16(float InValue) noexcept
{
    const std::uint32_t ScopedBits = std::bit_cast<std::uint32_t>(InValue);
    const std::uint32_t ScopedSign = (ScopedBits >> 16u) & 0x8000u;
    std::uint32_t ScopedExponent = (ScopedBits >> 23u) & 0xFFu;
    std::uint32_t ScopedMantissa = ScopedBits & 0x7FFFFFu;
    if (ScopedExponent == 0xFFu)
    {
        if (ScopedMantissa == 0u)
        {
            return static_cast<Float16Bits>(ScopedSign | 0x7C00u);
        }
        return static_cast<Float16Bits>(ScopedSign | 0x7E00u | (ScopedMantissa >> 13u));
    }
    std::int32_t ScopedNewExponent = static_cast<std::int32_t>(ScopedExponent) - 127 + 15;
    if (ScopedNewExponent >= 31)
    {
        return static_cast<Float16Bits>(ScopedSign | 0x7C00u);
    }
    if (ScopedNewExponent <= 0)
    {
        if (ScopedNewExponent < -10)
        {
            return static_cast<Float16Bits>(ScopedSign);
        }
        ScopedMantissa |= 0x800000u;
        const std::uint32_t ScopedShift = static_cast<std::uint32_t>(14 - ScopedNewExponent);
        std::uint32_t ScopedHalfMantissa = ScopedMantissa >> ScopedShift;
        const std::uint32_t ScopedRound = 1u << (ScopedShift - 1u);
        if ((ScopedMantissa & ScopedRound) != 0u)
        {
            ++ScopedHalfMantissa;
        }
        return static_cast<Float16Bits>(ScopedSign | ScopedHalfMantissa);
    }
    std::uint32_t ScopedHalfMantissa = ScopedMantissa >> 13u;
    if ((ScopedMantissa & 0x1000u) != 0u)
    {
        ++ScopedHalfMantissa;
        if (ScopedHalfMantissa == 0x400u)
        {
            ScopedHalfMantissa = 0u;
            ++ScopedNewExponent;
            if (ScopedNewExponent >= 31)
            {
                return static_cast<Float16Bits>(ScopedSign | 0x7C00u);
            }
        }
    }
    return static_cast<Float16Bits>(ScopedSign | (static_cast<std::uint32_t>(ScopedNewExponent) << 10u) |
                                    ScopedHalfMantissa);
}

// Converts binary16 bits back to a float. NaN payloads are preserved loosely.
[[nodiscard]] inline float UnpackFloat16(Float16Bits InBits) noexcept
{
    const std::uint32_t ScopedSign = (static_cast<std::uint32_t>(InBits) & 0x8000u) << 16u;
    std::uint32_t ScopedExponent = (static_cast<std::uint32_t>(InBits) >> 10u) & 0x1Fu;
    std::uint32_t ScopedMantissa = static_cast<std::uint32_t>(InBits) & 0x3FFu;
    std::uint32_t ScopedFloatExponent = 0;
    if (ScopedExponent == 0u)
    {
        if (ScopedMantissa == 0u)
        {
            return std::bit_cast<float>(ScopedSign);
        }
        std::int32_t ScopedNormalExponent = -14;
        while ((ScopedMantissa & 0x400u) == 0u)
        {
            ScopedMantissa <<= 1u;
            --ScopedNormalExponent;
        }
        ScopedMantissa &= 0x3FFu;
        ScopedFloatExponent = static_cast<std::uint32_t>(ScopedNormalExponent + 127);
    }
    else if (ScopedExponent == 31u)
    {
        ScopedFloatExponent = 255u;
    }
    else
    {
        ScopedFloatExponent = ScopedExponent + 127u - 15u;
    }
    const std::uint32_t ScopedFloatBits =
        ScopedSign | (ScopedFloatExponent << 23u) | (ScopedMantissa << 13u);
    return std::bit_cast<float>(ScopedFloatBits);
}

// A compact 2D transform: position, scale and the sine/cosine pair. The pair
// avoids trigonometry in the vertex stage.
struct alignas(16) PackedTransform2D
{
    float PositionX = 0.0f;
    float PositionY = 0.0f;
    Float16Bits ScaleX = 0;
    Float16Bits ScaleY = 0;
    Float16Bits Cos = 0;
    Float16Bits Sin = 0;
};

static_assert(sizeof(PackedTransform2D) == 16);
static_assert(offsetof(PackedTransform2D, ScaleX) == 8);
static_assert(offsetof(PackedTransform2D, Cos) == 12);

// One 32-byte sprite record. The size is already multiplied by the transform
// scale, and the rotation is stored as sine/cosine.
struct alignas(32) GpuSpriteInstance
{
    float PositionX = 0.0f;
    float PositionY = 0.0f;
    Float16Bits SizeX = 0;
    Float16Bits SizeY = 0;
    Float16Bits UvMinX = 0;
    Float16Bits UvMinY = 0;
    Float16Bits UvMaxX = 0;
    Float16Bits UvMaxY = 0;
    Float16Bits Layer = 0;
    Float16Bits Flags = 0;
    Float16Bits Cos = 0;
    Float16Bits Sin = 0;
    std::uint32_t Color = 0xFFFFFFFFu;
};

static_assert(sizeof(GpuSpriteInstance) == 32);
static_assert(offsetof(GpuSpriteInstance, UvMinX) == 12);
static_assert(offsetof(GpuSpriteInstance, Layer) == 20);
static_assert(offsetof(GpuSpriteInstance, Cos) == 24);
static_assert(offsetof(GpuSpriteInstance, Color) == 28);

// Builds a packed transform from an authored value.
[[nodiscard]] inline PackedTransform2D MakePackedTransform2D(Vector2D InPosition, Vector2D InScale,
                                                             float InRotation) noexcept
{
    PackedTransform2D ScopedTransform;
    ScopedTransform.PositionX = InPosition.X;
    ScopedTransform.PositionY = InPosition.Y;
    ScopedTransform.ScaleX = PackFloat16(InScale.X);
    ScopedTransform.ScaleY = PackFloat16(InScale.Y);
    ScopedTransform.Cos = PackFloat16(std::cos(InRotation));
    ScopedTransform.Sin = PackFloat16(std::sin(InRotation));
    return ScopedTransform;
}

// Converts one render item into the compact sprite layout.
[[nodiscard]] inline GpuSpriteInstance MakeGpuSpriteInstance(const Rhi::RenderItem& InItem) noexcept
{
    GpuSpriteInstance ScopedInstance;
    ScopedInstance.PositionX = InItem.position.X;
    ScopedInstance.PositionY = InItem.position.Y;
    ScopedInstance.SizeX = PackFloat16(InItem.size.X);
    ScopedInstance.SizeY = PackFloat16(InItem.size.Y);
    ScopedInstance.UvMinX = PackFloat16(InItem.uvMin.X);
    ScopedInstance.UvMinY = PackFloat16(InItem.uvMin.Y);
    ScopedInstance.UvMaxX = PackFloat16(InItem.uvMax.X);
    ScopedInstance.UvMaxY = PackFloat16(InItem.uvMax.Y);
    ScopedInstance.Layer = static_cast<Float16Bits>(InItem.layer);
    ScopedInstance.Flags = static_cast<Float16Bits>(InItem.flags);
    ScopedInstance.Cos = PackFloat16(std::cos(InItem.rotation));
    ScopedInstance.Sin = PackFloat16(std::sin(InItem.rotation));
    ScopedInstance.Color = InItem.color;
    return ScopedInstance;
}

// A chunk arena with stable (chunk, slot) handles. Slots are reused before a
// new chunk is allocated; an empty chunk returns to the free list.
class RenderChunkArena
{
public:
    [[nodiscard]] GpuEntity Acquire(std::uint32_t InArchetype) noexcept
    {
        if (MFreeSlots.empty())
        {
            if (!AllocateChunk(InArchetype))
            {
                return {};
            }
        }
        GpuEntity ScopedEntity = MFreeSlots.back();
        MFreeSlots.pop_back();
        const auto ScopedFree = std::find(MFreeChunks.begin(), MFreeChunks.end(), ScopedEntity.Chunk);
        if (ScopedFree != MFreeChunks.end())
        {
            MFreeChunks.erase(ScopedFree);
        }
        RenderChunk& ScopedChunk = *MChunks[ScopedEntity.Chunk];
        ++ScopedChunk.Header.ActiveCount;
        ++MActiveCount;
        return ScopedEntity;
    }

    void Release(GpuEntity InEntity) noexcept
    {
        if (!InEntity.IsValid() || InEntity.Chunk >= MChunks.size() || MChunks[InEntity.Chunk] == nullptr)
        {
            return;
        }
        RenderChunk& ScopedChunk = *MChunks[InEntity.Chunk];
        if (ScopedChunk.Header.ActiveCount == 0u || InEntity.Slot >= ScopedChunk.Header.Capacity)
        {
            return;
        }
        std::byte* ScopedSlot = SlotData(ScopedChunk, InEntity.Slot);
        std::fill_n(ScopedSlot, sizeof(GpuSpriteInstance), std::byte{0});
        // The zeroed slot must reach the GPU on the next upload; without the dirty
        // mark a released sprite would stay visible in the packed chunk.
        MarkDirty(ScopedChunk, InEntity.Slot * static_cast<std::uint32_t>(sizeof(GpuSpriteInstance)),
                  static_cast<std::uint32_t>(sizeof(GpuSpriteInstance)));
        --ScopedChunk.Header.ActiveCount;
        --MActiveCount;
        MFreeSlots.push_back(InEntity);
        if (ScopedChunk.Header.ActiveCount == 0u)
        {
            MFreeChunks.push_back(InEntity.Chunk);
        }
    }

    [[nodiscard]] GpuSpriteInstance* Sprite(GpuEntity InEntity) noexcept
    {
        RenderChunk* ScopedChunk = ChunkFor(InEntity);
        if (ScopedChunk == nullptr)
        {
            return nullptr;
        }
        return reinterpret_cast<GpuSpriteInstance*>(SlotData(*ScopedChunk, InEntity.Slot));
    }

    [[nodiscard]] const GpuSpriteInstance* Sprite(GpuEntity InEntity) const noexcept
    {
        const RenderChunk* ScopedChunk = ChunkFor(InEntity);
        if (ScopedChunk == nullptr)
        {
            return nullptr;
        }
        return reinterpret_cast<const GpuSpriteInstance*>(SlotData(*ScopedChunk, InEntity.Slot));
    }

    [[nodiscard]] RenderChunk* ChunkFor(GpuEntity InEntity) noexcept
    {
        if (!InEntity.IsValid() || InEntity.Chunk >= MChunks.size())
        {
            return nullptr;
        }
        return MChunks[InEntity.Chunk].get();
    }

    [[nodiscard]] const RenderChunk* ChunkFor(GpuEntity InEntity) const noexcept
    {
        if (!InEntity.IsValid() || InEntity.Chunk >= MChunks.size())
        {
            return nullptr;
        }
        return MChunks[InEntity.Chunk].get();
    }

    [[nodiscard]] std::size_t ChunkCount() const noexcept
    {
        return MChunks.size() - MFreeChunks.size();
    }

    [[nodiscard]] std::uint32_t ActiveCount() const noexcept
    {
        return MActiveCount;
    }

    [[nodiscard]] std::uint32_t FreeSlotCount() const noexcept
    {
        return static_cast<std::uint32_t>(MFreeSlots.size());
    }

private:
    [[nodiscard]] bool AllocateChunk(std::uint32_t InArchetype) noexcept
    {
        std::uint32_t ScopedIndex = 0;
        if (!MFreeChunks.empty())
        {
            ScopedIndex = MFreeChunks.back();
            MFreeChunks.pop_back();
        }
        else
        {
            if (MChunks.size() >= MaxChunks)
            {
                return false;
            }
            ScopedIndex = static_cast<std::uint32_t>(MChunks.size());
            MChunks.push_back(std::make_unique<RenderChunk>());
        }
        RenderChunk& ScopedChunk = *MChunks[ScopedIndex];
        ScopedChunk.Header = ChunkHeader{};
        ScopedChunk.Header.Archetype = InArchetype;
        ScopedChunk.Header.Capacity = ChunkCapacity;
        ScopedChunk.Header.PayloadBytes = static_cast<std::uint32_t>(ScopedChunk.Payload.size());
        for (std::uint32_t ScopedSlot = ChunkCapacity; ScopedSlot > 0u; --ScopedSlot)
        {
            MFreeSlots.push_back(GpuEntity{ScopedIndex, ScopedSlot - 1u});
        }
        return true;
    }

    [[nodiscard]] static std::byte* SlotData(RenderChunk& InChunk, std::uint32_t InSlot) noexcept
    {
        return std::span<std::byte>(InChunk.Payload)
            .subspan(static_cast<std::size_t>(InSlot) * sizeof(GpuSpriteInstance), sizeof(GpuSpriteInstance))
            .data();
    }

    [[nodiscard]] static const std::byte* SlotData(const RenderChunk& InChunk, std::uint32_t InSlot) noexcept
    {
        return std::span<const std::byte>(InChunk.Payload)
            .subspan(static_cast<std::size_t>(InSlot) * sizeof(GpuSpriteInstance), sizeof(GpuSpriteInstance))
            .data();
    }

    std::vector<std::unique_ptr<RenderChunk>> MChunks;
    std::vector<GpuEntity> MFreeSlots;
    std::vector<std::uint32_t> MFreeChunks;
    std::uint32_t MActiveCount = 0;
};

// A thin sprite-facing facade over the chunk arena. The renderer owns the
// arena; gameplay systems never see GpuEntity directly.
class RenderEcs
{
public:
    [[nodiscard]] GpuEntity AddSprite(const Rhi::RenderItem& InItem) noexcept
    {
        const GpuEntity ScopedEntity = MArena.Acquire(0u);
        if (!ScopedEntity.IsValid())
        {
            return {};
        }
        GpuSpriteInstance* ScopedSprite = MArena.Sprite(ScopedEntity);
        RenderChunk* ScopedChunk = MArena.ChunkFor(ScopedEntity);
        if (ScopedSprite == nullptr || ScopedChunk == nullptr)
        {
            MArena.Release(ScopedEntity);
            return {};
        }
        *ScopedSprite = MakeGpuSpriteInstance(InItem);
        const std::uint32_t ScopedOffset =
            ScopedEntity.Slot * static_cast<std::uint32_t>(sizeof(GpuSpriteInstance));
        MarkDirty(*ScopedChunk, ScopedOffset, sizeof(GpuSpriteInstance));
        return ScopedEntity;
    }

    bool UpdateSprite(GpuEntity InEntity, const Rhi::RenderItem& InItem) noexcept
    {
        GpuSpriteInstance* ScopedSprite = MArena.Sprite(InEntity);
        RenderChunk* ScopedChunk = MArena.ChunkFor(InEntity);
        if (ScopedSprite == nullptr || ScopedChunk == nullptr)
        {
            return false;
        }
        *ScopedSprite = MakeGpuSpriteInstance(InItem);
        const std::uint32_t ScopedOffset =
            InEntity.Slot * static_cast<std::uint32_t>(sizeof(GpuSpriteInstance));
        MarkDirty(*ScopedChunk, ScopedOffset, sizeof(GpuSpriteInstance));
        return true;
    }

    void RemoveSprite(GpuEntity InEntity) noexcept
    {
        MArena.Release(InEntity);
    }

    [[nodiscard]] GpuSpriteInstance* Sprite(GpuEntity InEntity) noexcept
    {
        return MArena.Sprite(InEntity);
    }

    [[nodiscard]] const GpuSpriteInstance* Sprite(GpuEntity InEntity) const noexcept
    {
        return MArena.Sprite(InEntity);
    }

    [[nodiscard]] RenderChunk* ChunkFor(GpuEntity InEntity) noexcept
    {
        return MArena.ChunkFor(InEntity);
    }

    [[nodiscard]] std::uint32_t ActiveCount() const noexcept
    {
        return MArena.ActiveCount();
    }

    [[nodiscard]] std::size_t ChunkCount() const noexcept
    {
        return MArena.ChunkCount();
    }

    [[nodiscard]] std::uint32_t FreeSlotCount() const noexcept
    {
        return MArena.FreeSlotCount();
    }

private:
    RenderChunkArena MArena;
};
}
