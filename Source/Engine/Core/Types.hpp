// FemboyEngine2D - core value types.
#pragma once

#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Fby
{

// Vector2D - the engine's 2D coordinate type.
struct Vector2D
{
    float X = 0.0f;
    float Y = 0.0f;

    constexpr Vector2D() = default;
    constexpr Vector2D(float InX, float InY) : X(InX), Y(InY)
    {
    }
    constexpr explicit Vector2D(float InScalar) : X(InScalar), Y(InScalar)
    {
    }

    [[nodiscard]] constexpr Vector2D operator+(Vector2D InRhs) const
    {
        return {X + InRhs.X, Y + InRhs.Y};
    }
    [[nodiscard]] constexpr Vector2D operator-(Vector2D InRhs) const
    {
        return {X - InRhs.X, Y - InRhs.Y};
    }
    [[nodiscard]] constexpr Vector2D operator*(Vector2D InRhs) const
    {
        return {X * InRhs.X, Y * InRhs.Y};
    }
    [[nodiscard]] constexpr Vector2D operator*(float InScalar) const
    {
        return {X * InScalar, Y * InScalar};
    }
    [[nodiscard]] constexpr Vector2D operator/(float InScalar) const
    {
        return {X / InScalar, Y / InScalar};
    }
    constexpr Vector2D& operator+=(Vector2D InRhs)
    {
        X += InRhs.X;
        Y += InRhs.Y;
        return *this;
    }
    constexpr Vector2D& operator-=(Vector2D InRhs)
    {
        X -= InRhs.X;
        Y -= InRhs.Y;
        return *this;
    }
    constexpr Vector2D& operator*=(float InScalar)
    {
        X *= InScalar;
        Y *= InScalar;
        return *this;
    }

    [[nodiscard]] friend constexpr Vector2D operator*(float InScalar, Vector2D InVector)
    {
        return {InVector.X * InScalar, InVector.Y * InScalar};
    }
    [[nodiscard]] friend constexpr bool operator==(Vector2D InLeft, Vector2D InRight)
    {
        return InLeft.X == InRight.X && InLeft.Y == InRight.Y;
    }

    [[nodiscard]] constexpr float Dot(Vector2D InRhs) const
    {
        return X * InRhs.X + Y * InRhs.Y;
    }
    // 2D cross product: the z component of the 3D cross of (X, Y, 0)
    [[nodiscard]] constexpr float Cross(Vector2D InRhs) const
    {
        return X * InRhs.Y - Y * InRhs.X;
    }
    [[nodiscard]] constexpr float LengthSquared() const
    {
        return X * X + Y * Y;
    }
};

// Vector3D - the engine's 3D coordinate type.
struct Vector3D
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;

    constexpr Vector3D() = default;
    constexpr Vector3D(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ)
    {
    }
    constexpr explicit Vector3D(float InScalar) : X(InScalar), Y(InScalar), Z(InScalar)
    {
    }

    [[nodiscard]] constexpr Vector3D operator+(Vector3D InRhs) const
    {
        return {X + InRhs.X, Y + InRhs.Y, Z + InRhs.Z};
    }
    [[nodiscard]] constexpr Vector3D operator-(Vector3D InRhs) const
    {
        return {X - InRhs.X, Y - InRhs.Y, Z - InRhs.Z};
    }
    [[nodiscard]] constexpr Vector3D operator*(float InScalar) const
    {
        return {X * InScalar, Y * InScalar, Z * InScalar};
    }
    constexpr Vector3D& operator+=(Vector3D InRhs)
    {
        X += InRhs.X;
        Y += InRhs.Y;
        Z += InRhs.Z;
        return *this;
    }

    [[nodiscard]] constexpr float Dot(Vector3D InRhs) const
    {
        return X * InRhs.X + Y * InRhs.Y + Z * InRhs.Z;
    }
    [[nodiscard]] constexpr Vector3D Cross(Vector3D InRhs) const
    {
        return {Y * InRhs.Z - Z * InRhs.Y, Z * InRhs.X - X * InRhs.Z, X * InRhs.Y - Y * InRhs.X};
    }
    [[nodiscard]] constexpr float LengthSquared() const
    {
        return X * X + Y * Y + Z * Z;
    }
};

// Vector4D - homogeneous coordinates and material constants.
struct Vector4D
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 1.0f;

    constexpr Vector4D() = default;
    constexpr Vector4D(float InX, float InY, float InZ, float InW) : X(InX), Y(InY), Z(InZ), W(InW)
    {
    }
    constexpr explicit Vector4D(float InScalar) : X(InScalar), Y(InScalar), Z(InScalar), W(InScalar)
    {
    }

    [[nodiscard]] constexpr Vector4D operator+(Vector4D InRhs) const
    {
        return {X + InRhs.X, Y + InRhs.Y, Z + InRhs.Z, W + InRhs.W};
    }
    [[nodiscard]] constexpr Vector4D operator-(Vector4D InRhs) const
    {
        return {X - InRhs.X, Y - InRhs.Y, Z - InRhs.Z, W - InRhs.W};
    }
    [[nodiscard]] constexpr Vector4D operator*(float InScalar) const
    {
        return {X * InScalar, Y * InScalar, Z * InScalar, W * InScalar};
    }
    [[nodiscard]] constexpr float Dot(Vector4D InRhs) const
    {
        return X * InRhs.X + Y * InRhs.Y + Z * InRhs.Z + W * InRhs.W;
    }
};

// Rect - half-open [min, max) axis aligned box in world units.
struct Rect
{
    Vector2D min{};
    Vector2D max{};

    constexpr Rect() = default;
    constexpr Rect(Vector2D InLo, Vector2D InHi) : min(InLo), max(InHi)
    {
    }

    [[nodiscard]] constexpr Vector2D Size() const
    {
        return {max.X - min.X, max.Y - min.Y};
    }
    [[nodiscard]] constexpr Vector2D Center() const
    {
        return {(min.X + max.X) * 0.5f, (min.Y + max.Y) * 0.5f};
    }
    [[nodiscard]] constexpr bool Contains(Vector2D InP) const
    {
        return InP.X >= min.X && InP.Y >= min.Y && InP.X < max.X && InP.Y < max.Y;
    }
    [[nodiscard]] static constexpr Rect FromCenter(Vector2D InCenter, Vector2D InSize)
    {
        const Vector2D ScopedHalf{InSize.X * 0.5f, InSize.Y * 0.5f};
        return {InCenter - ScopedHalf, InCenter + ScopedHalf};
    }
};

// Color32 - one packed uint32, layout 0xAABBGGRR.
struct Color32
{
    std::uint32_t rgba = 0xFFFFFFFFu;

    constexpr Color32() = default;
    constexpr explicit Color32(std::uint32_t InPacked) : rgba(InPacked)
    {
    }

    [[nodiscard]] static constexpr Color32 FromBytes(std::uint8_t InR, std::uint8_t InG, std::uint8_t InB,
                                                     std::uint8_t InA = 255)
    {
        return Color32{static_cast<std::uint32_t>(InR) | (static_cast<std::uint32_t>(InG) << 8) |
                       (static_cast<std::uint32_t>(InB) << 16) | (static_cast<std::uint32_t>(InA) << 24)};
    }
    [[nodiscard]] static constexpr Color32 FromFloats(float InR, float InG, float InB, float InA = 1.0f);
    [[nodiscard]] static constexpr Color32 White()
    {
        return Color32{0xFFFFFFFFu};
    }
    [[nodiscard]] static constexpr Color32 Black()
    {
        // Layout is 0xAABBGGRR: opaque black is alpha 0xFF in the high byte.
        return Color32{0xFF000000u};
    }

    [[nodiscard]] constexpr std::uint8_t R() const
    {
        return static_cast<std::uint8_t>(rgba & 0xFFu);
    }
    [[nodiscard]] constexpr std::uint8_t G() const
    {
        return static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
    }
    [[nodiscard]] constexpr std::uint8_t B() const
    {
        return static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
    }
    [[nodiscard]] constexpr std::uint8_t A() const
    {
        return static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
    }

    [[nodiscard]] friend constexpr bool operator==(Color32 InA, Color32 InB)
    {
        return InA.rgba == InB.rgba;
    }
};

namespace Detail
{
[[nodiscard]] constexpr std::uint8_t Unorm8(float InValue)
{
    if (!(InValue > 0.0f))
    {
        // Also catches NaN
        return 0;
    }
    if (InValue >= 1.0f)
    {
        return 255;
    }
    return static_cast<std::uint8_t>(InValue * 255.0f + 0.5f);
}
}

constexpr Color32 Color32::FromFloats(float InR, float InG, float InB, float InA)
{
    return FromBytes(Detail::Unorm8(InR), Detail::Unorm8(InG), Detail::Unorm8(InB), Detail::Unorm8(InA));
}

// Handle - a typed, nullable 32-bit id.
template <typename Tag> struct Handle
{
    using Value = std::uint32_t;
    static constexpr Value kInvalid = std::numeric_limits<Value>::max();

    Value id = kInvalid;

    constexpr Handle() = default;
    constexpr explicit Handle(Value InValue) : id(InValue)
    {
    }

    [[nodiscard]] constexpr bool IsValid() const
    {
        return id != kInvalid;
    }
    [[nodiscard]] constexpr explicit operator bool() const
    {
        return IsValid();
    }
    [[nodiscard]] friend constexpr bool operator==(Handle InA, Handle InB)
    {
        return InA.id == InB.id;
    }
};

struct BufferTag;
struct TextureTag;
struct PipelineTag;
struct SamplerTag;

using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using PipelineHandle = Handle<PipelineTag>;
using SamplerHandle = Handle<SamplerTag>;

// Small utilities shared by every module.
[[nodiscard]] constexpr std::uint64_t PackU64(std::uint32_t InHi, std::uint32_t InLo)
{
    return (static_cast<std::uint64_t>(InHi) << 32) | InLo;
}

template <typename To, typename From> [[nodiscard]] constexpr To BitCast(const From& InValue) noexcept
{
    static_assert(sizeof(To) == sizeof(From), "BitCast requires equal sizes");
    static_assert(std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From>,
                  "BitCast requires trivially copyable types");
    return std::bit_cast<To>(InValue);
}
}
