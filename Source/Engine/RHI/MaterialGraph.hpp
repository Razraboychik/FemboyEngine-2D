// FemboyEngine2D - the material graph data model.
#pragma once

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>

namespace Fby::Render
{

// Capacities. A material that needs more than these is a design smell, not a
inline constexpr std::uint32_t kMaxMaterialNodes = 128;
inline constexpr std::uint32_t kMaxMaterialPins = 384;
inline constexpr std::uint32_t kMaxMaterialParams = 64;

// Deepest input signature (Output has BaseColor + Opacity + Emissive).
inline constexpr std::uint8_t kMaxMaterialInputs = 4;

// 16-bit indices are invalid at 0xFFFF, which is also what a serialized graph
inline constexpr std::uint16_t kInvalidMaterialNode = 0xFFFFu;
inline constexpr std::uint16_t kInvalidMaterialParam = 0xFFFFu;

// Node kinds. The enumerator order is part of the cooked material format and of
enum class MaterialNodeKind : std::uint8_t
{
    Output = 0,

    // Engine mesh.vert varying location 0
    Uv = 1,
    // Engine mesh.vert varying location 1
    VertexColor = 2,
    // Material push-constant clock (see compiler contract)
    Time = 3,
    // Fragment-side reconstruction or generated varying
    WorldPosition = 4,
    // Model-space bind-pose position, generated vertex stage only
    VertexPosition = 5,

    // Float
    Scalar = 6,
    // Vec2
    Vector2D = 7,
    // Vec3
    Vector3D = 8,
    // Vec4
    Color = 9,
    // Sampler2DArray sample at an explicit layer
    TextureSample = 10,

    Add = 11,
    Sub = 12,
    Mul = 13,
    Div = 14,
    Lerp = 15,
    Saturate = 16,
    Clamp = 17,
    Power = 18,
    Sin = 19,
    Cos = 20,
    OneMinus = 21,
    Dot = 22,
    Fresnel = 23,

    // Tilt25D projects a height along an authored slope vector: the classic
    Tilt25D = 24,
    // Inputs: UV (optional), Strength float -> vec2
    Distortion = 25,

    Count
};

// The value that flows along an edge. There are no matrices: a material graph
enum class MaterialValueType : std::uint8_t
{
    Float = 0,
    Vector2D = 1,
    Vector3D = 2,
    Vector4D = 3,
    Count
};

// Constant payload discriminator. TextureSample needs no float payload, only
enum class MaterialParamKind : std::uint8_t
{
    Scalar = 0,
    Vector2D = 1,
    Vector3D = 2,
    Color = 3,
    TextureLayer = 4,
    Count
};

// One constant. The members are plain floats instead of a tagged union so the
struct MaterialParam
{
    MaterialParamKind kind = MaterialParamKind::Scalar;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 1.0f;
    // TextureSample only: index into the mesh pass's sampler2DArray.
    std::uint32_t textureLayer = 0;
};

// Reserved for editor / serialization state; today no flag is defined and a
enum MaterialNodeFlags : std::uint16_t
{
    MaterialNodeFlagNone = 0,
};

// One node of the DAG. `firstInput`/`inputCount` delimit a contiguous range of
struct MaterialNode
{
    MaterialNodeKind kind = MaterialNodeKind::Scalar;
    std::uint16_t firstInput = 0;
    std::uint16_t inputCount = 0;
    std::uint16_t paramIndex = kInvalidMaterialParam;
    std::uint16_t flags = MaterialNodeFlags::MaterialNodeFlagNone;
    // Editor-only position; deliberately not part of Hash().
    Vector2D uiPosition{};
};

// One input slot. `node`/`slot` identify the slot, `sourceNode`/`sourceSlot`
struct MaterialPin
{
    std::uint16_t node = kInvalidMaterialNode;
    std::uint8_t slot = 0;
    MaterialValueType type = MaterialValueType::Float;
    std::uint16_t sourceNode = kInvalidMaterialNode;
    std::uint8_t sourceSlot = 0;
    std::uint8_t flags = 0;
};

// Node introspection. These live in the header so the editor panels and tests
[[nodiscard]] constexpr const char* MaterialNodeKindName(MaterialNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Output:
            return "Output";
        case MaterialNodeKind::Uv:
            return "UV";
        case MaterialNodeKind::VertexColor:
            return "VertexColor";
        case MaterialNodeKind::Time:
            return "Time";
        case MaterialNodeKind::WorldPosition:
            return "WorldPosition";
        case MaterialNodeKind::VertexPosition:
            return "VertexPosition";
        case MaterialNodeKind::Scalar:
            return "Scalar";
        case MaterialNodeKind::Vector2D:
            return "Vec2";
        case MaterialNodeKind::Vector3D:
            return "Vec3";
        case MaterialNodeKind::Color:
            return "Color";
        case MaterialNodeKind::TextureSample:
            return "TextureSample";
        case MaterialNodeKind::Add:
            return "Add";
        case MaterialNodeKind::Sub:
            return "Sub";
        case MaterialNodeKind::Mul:
            return "Mul";
        case MaterialNodeKind::Div:
            return "Div";
        case MaterialNodeKind::Lerp:
            return "Lerp";
        case MaterialNodeKind::Saturate:
            return "Saturate";
        case MaterialNodeKind::Clamp:
            return "Clamp";
        case MaterialNodeKind::Power:
            return "Power";
        case MaterialNodeKind::Sin:
            return "Sin";
        case MaterialNodeKind::Cos:
            return "Cos";
        case MaterialNodeKind::OneMinus:
            return "OneMinus";
        case MaterialNodeKind::Dot:
            return "Dot";
        case MaterialNodeKind::Fresnel:
            return "Fresnel";
        case MaterialNodeKind::Tilt25D:
            return "Tilt25D";
        case MaterialNodeKind::Distortion:
            return "Distortion";
        case MaterialNodeKind::Count:
            break;
    }
    return "?";
}

[[nodiscard]] constexpr const char* MaterialValueTypeName(MaterialValueType InType) noexcept
{
    switch (InType)
    {
        case MaterialValueType::Float:
            return "float";
        case MaterialValueType::Vector2D:
            return "vec2";
        case MaterialValueType::Vector3D:
            return "vec3";
        case MaterialValueType::Vector4D:
            return "vec4";
        case MaterialValueType::Count:
            break;
    }
    return "?";
}

[[nodiscard]] constexpr const char* MaterialValueTypeGlsl(MaterialValueType InType) noexcept
{
    switch (InType)
    {
        case MaterialValueType::Float:
            return "float";
        case MaterialValueType::Vector2D:
            return "vec2";
        case MaterialValueType::Vector3D:
            return "vec3";
        case MaterialValueType::Vector4D:
            return "vec4";
        case MaterialValueType::Count:
            break;
    }
    return "float";
}

[[nodiscard]] constexpr std::uint8_t MaterialNodeInputCount(MaterialNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Output:
            // BaseColor, Opacity, Emissive
            return 3;
        case MaterialNodeKind::TextureSample:
            // Optional UV override
            return 1;
        case MaterialNodeKind::Fresnel:
            // Optional power
            return 1;
        case MaterialNodeKind::Tilt25D:
            // Optional UV, slope, height
            return 3;
        case MaterialNodeKind::Distortion:
            // Optional UV, strength
            return 2;
        case MaterialNodeKind::Uv:
        case MaterialNodeKind::VertexColor:
        case MaterialNodeKind::Time:
        case MaterialNodeKind::WorldPosition:
        case MaterialNodeKind::VertexPosition:
        case MaterialNodeKind::Scalar:
        case MaterialNodeKind::Vector2D:
        case MaterialNodeKind::Vector3D:
        case MaterialNodeKind::Color:
            return 0;
        case MaterialNodeKind::Add:
        case MaterialNodeKind::Sub:
        case MaterialNodeKind::Mul:
        case MaterialNodeKind::Div:
        case MaterialNodeKind::Power:
        case MaterialNodeKind::Dot:
            return 2;
        case MaterialNodeKind::Lerp:
        case MaterialNodeKind::Clamp:
            return 3;
        case MaterialNodeKind::Saturate:
        case MaterialNodeKind::Sin:
        case MaterialNodeKind::Cos:
        case MaterialNodeKind::OneMinus:
            return 1;
        case MaterialNodeKind::Count:
            break;
    }
    return 0;
}

// Optional slots may stay unconnected and fall back to the documented default.
[[nodiscard]] constexpr bool MaterialNodeSlotOptional(MaterialNodeKind InKind, std::uint8_t InSlot) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Output:
            // BaseColor is required, Opacity/Emissive are additive
            return InSlot != 0;
        case MaterialNodeKind::TextureSample:
            // UV defaults to the engine varying
            return InSlot == 0;
        case MaterialNodeKind::Fresnel:
            // Power defaults to 5
            return InSlot == 0;
        case MaterialNodeKind::Tilt25D:
            // Every slot has an authored fallback (vUv / slope / height)
            return true;
        case MaterialNodeKind::Distortion:
            // VUv / authored strength
            return true;
        default:
            return false;
    }
}

// Default type carried by an unconnected pin (also what Disconnect() restores).
[[nodiscard]] constexpr MaterialValueType MaterialNodeSlotDefaultType(MaterialNodeKind InKind,
                                                                      std::uint8_t InSlot) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Output:
            return InSlot == 1 ? MaterialValueType::Float : MaterialValueType::Vector4D;
        case MaterialNodeKind::TextureSample:
            return MaterialValueType::Vector2D;
        case MaterialNodeKind::Fresnel:
            return MaterialValueType::Float;
        case MaterialNodeKind::Tilt25D:
            return InSlot == 2 ? MaterialValueType::Float : MaterialValueType::Vector2D;
        case MaterialNodeKind::Distortion:
            return InSlot == 1 ? MaterialValueType::Float : MaterialValueType::Vector2D;
        default:
            return MaterialValueType::Float;
    }
}

// Human readable slot label for the editor panel and error messages.
[[nodiscard]] constexpr const char* MaterialNodeInputName(MaterialNodeKind InKind, std::uint8_t InSlot) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Output:
            switch (InSlot)
            {
                case 0:
                    return "BaseColor";
                case 1:
                    return "Opacity";
                case 2:
                    return "Emissive";
                default:
                    return "?";
            }
        case MaterialNodeKind::TextureSample:
            return "UV";
        case MaterialNodeKind::Fresnel:
            return "Power";
        case MaterialNodeKind::Tilt25D:
            switch (InSlot)
            {
                case 0:
                    return "UV";
                case 1:
                    return "Slope";
                case 2:
                    return "Height";
                default:
                    return "?";
            }
        case MaterialNodeKind::Distortion:
            return InSlot == 0 ? "UV" : "Strength";
        default:
            switch (InSlot)
            {
                case 0:
                    return "A";
                case 1:
                    return "B";
                case 2:
                    return "C";
                default:
                    return "?";
            }
    }
}

// Coarse per-slot acceptance: what a single pin may carry. The cross-slot math
[[nodiscard]] constexpr bool MaterialSlotAccepts(MaterialNodeKind InKind, std::uint8_t InSlot,
                                                 MaterialValueType InType) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Output:
            switch (InSlot)
            {
                // BaseColor: scalar widens to a grey colour
                case 0:
                // Emissive
                case 2:
                    return InType == MaterialValueType::Float || InType == MaterialValueType::Vector3D ||
                           InType == MaterialValueType::Vector4D;
                // Opacity
                case 1:
                    return InType == MaterialValueType::Float;
                default:
                    return false;
            }
        case MaterialNodeKind::TextureSample:
            return InSlot == 0 && InType == MaterialValueType::Vector2D;
        case MaterialNodeKind::Fresnel:
            return InSlot == 0 && InType == MaterialValueType::Float;
        case MaterialNodeKind::Tilt25D:
            return InSlot < 2 ? InType == MaterialValueType::Vector2D : InType == MaterialValueType::Float;
        case MaterialNodeKind::Distortion:
            return InSlot == 0 ? InType == MaterialValueType::Vector2D : InType == MaterialValueType::Float;
        case MaterialNodeKind::Dot:
            return InSlot < 2 && InType != MaterialValueType::Float;
        default:
            return InType < MaterialValueType::Count;
    }
}

// The output type of nodes that never change with their inputs. Input-derived
[[nodiscard]] constexpr MaterialValueType MaterialNodeStaticOutputType(MaterialNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Uv:
            return MaterialValueType::Vector2D;
        case MaterialNodeKind::VertexColor:
            return MaterialValueType::Vector4D;
        case MaterialNodeKind::Time:
            return MaterialValueType::Float;
        case MaterialNodeKind::WorldPosition:
            return MaterialValueType::Vector3D;
        case MaterialNodeKind::VertexPosition:
            return MaterialValueType::Vector3D;
        case MaterialNodeKind::Scalar:
            return MaterialValueType::Float;
        case MaterialNodeKind::Vector2D:
            return MaterialValueType::Vector2D;
        case MaterialNodeKind::Vector3D:
            return MaterialValueType::Vector3D;
        case MaterialNodeKind::Color:
            return MaterialValueType::Vector4D;
        case MaterialNodeKind::TextureSample:
            return MaterialValueType::Vector4D;
        case MaterialNodeKind::Tilt25D:
        case MaterialNodeKind::Distortion:
            return MaterialValueType::Vector2D;
        case MaterialNodeKind::Dot:
        case MaterialNodeKind::Fresnel:
            return MaterialValueType::Float;
        default:
            return MaterialValueType::Count;
    }
}

// Result type of a math node given the resolved types of its first two inputs
[[nodiscard]] constexpr MaterialValueType MaterialNodeMathOutputType(MaterialNodeKind InKind, MaterialValueType InLeft,
                                                                     MaterialValueType InRight) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Add:
        case MaterialNodeKind::Sub:
        case MaterialNodeKind::Mul:
        case MaterialNodeKind::Div:
            if (InLeft == MaterialValueType::Float)
            {
                return InRight;
            }
            if (InRight == MaterialValueType::Float)
            {
                return InLeft;
            }
            return InLeft;
        case MaterialNodeKind::Lerp:
        case MaterialNodeKind::Clamp:
        case MaterialNodeKind::Power:
        case MaterialNodeKind::Saturate:
        case MaterialNodeKind::Sin:
        case MaterialNodeKind::Cos:
        case MaterialNodeKind::OneMinus:
            return InLeft;
        case MaterialNodeKind::Dot:
        case MaterialNodeKind::Fresnel:
            return MaterialValueType::Float;
        case MaterialNodeKind::Tilt25D:
        case MaterialNodeKind::Distortion:
            return MaterialValueType::Vector2D;
        default:
            return InLeft;
    }
}

// True when constant nodes own a MaterialParam and when that payload kind must
[[nodiscard]] constexpr bool MaterialNodeUsesParam(MaterialNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Scalar:
        case MaterialNodeKind::Vector2D:
        case MaterialNodeKind::Vector3D:
        case MaterialNodeKind::Color:
        case MaterialNodeKind::TextureSample:
        case MaterialNodeKind::Tilt25D:
        case MaterialNodeKind::Distortion:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr MaterialParamKind MaterialNodeParamKind(MaterialNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Scalar:
            return MaterialParamKind::Scalar;
        case MaterialNodeKind::Vector2D:
            return MaterialParamKind::Vector2D;
        case MaterialNodeKind::Vector3D:
            return MaterialParamKind::Vector3D;
        case MaterialNodeKind::Color:
            return MaterialParamKind::Color;
        case MaterialNodeKind::TextureSample:
            return MaterialParamKind::TextureLayer;
        case MaterialNodeKind::Tilt25D:
            // Xy = slope, z = height
            return MaterialParamKind::Vector2D;
        case MaterialNodeKind::Distortion:
            // X = strength, y = frequency
            return MaterialParamKind::Scalar;
        default:
            return MaterialParamKind::Scalar;
    }
}

// Full input-signature check for one node, including the cross-slot math rules
[[nodiscard]] bool MaterialNodeInputsCompatible(MaterialNodeKind InKind,
                                                std::span<const MaterialValueType> InInputTypes) noexcept;

// What a graph needs from the stage configuration. The editor runtime uses this
struct MaterialGraphRequirements
{
    // VertexPosition can only come from a varying
    bool needsVertexStage = false;
    // Material clock (pc.materialParams.x)
    bool needsTime = false;
    // WorldPosition or Fresnel
    bool needsWorldPosition = false;
    // Fragment stage reads pc.viewProjection
    bool needsFragmentViewProjection = false;
};

// MaterialGraph
class MaterialGraph
{
public:
    static_assert(kMaxMaterialNodes <= 0xFFFEu, "node indices must stay 16-bit");
    static_assert(kMaxMaterialPins <= 0xFFFEu, "pin indices must stay 16-bit");

    // Starts with the single Output node and its three (unconnected) input pins.
    MaterialGraph();
    ~MaterialGraph() = default;
    MaterialGraph(const MaterialGraph&) = default;
    MaterialGraph& operator=(const MaterialGraph&) = default;

    // Resets to the initial single-Output layout.
    void Clear() noexcept;

    // Adopts cooked storage counts before a wire format writes the arrays. The
    // arrays are cleared first, so a partially decoded graph never aliases an old
    // material. Returns false when a count exceeds its capacity.
    [[nodiscard]] bool AdoptCounts(std::uint32_t InNodeCount, std::uint32_t InPinCount,
                                   std::uint32_t InParamCount) noexcept;

    // Appends a node and its unconnected input pins. Returns
    [[nodiscard]] std::uint16_t AddNode(MaterialNodeKind InKind, Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddInputNode(MaterialNodeKind InKind, Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddScalarNode(float InValue, Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddVec2Node(Vector2D InValue, Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddVec3Node(float InX, float InY, float InZ, Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddColorNode(float InRed, float InGreen, float InBlue, float InAlpha,
                                             Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddTextureSampleNode(std::uint32_t InTextureLayer, Vector2D InUiPosition = {});
    // Math helpers: Unary(kind) for Saturate/Sin/Cos/OneMinus, Binary(kind) for
    [[nodiscard]] std::uint16_t AddUnaryNode(MaterialNodeKind InKind, Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddBinaryNode(MaterialNodeKind InKind, Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddLerpNode(Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddClampNode(Vector2D InUiPosition = {});
    [[nodiscard]] std::uint16_t AddFresnelNode(Vector2D InUiPosition = {});
    // 2.5D tilt: `slope` is the per-unit-height UV displacement, `height` the
    [[nodiscard]] std::uint16_t AddTilt25DNode(Vector2D InSlope, float InHeight, Vector2D InUiPosition = {});
    // Animated UV warp: strength in UV units, frequency in radians per UV unit.
    [[nodiscard]] std::uint16_t AddDistortionNode(float InStrength, float InFrequency, Vector2D InUiPosition = {});

    // Connects `sourceNode`'s single output to input `slot` of `node`. Rejects
    [[nodiscard]] bool Connect(std::uint16_t InNode, std::uint8_t InSlot, std::uint16_t InSourceNode,
                               std::string* InError = nullptr);
    void Disconnect(std::uint16_t InNode, std::uint8_t InSlot) noexcept;
    // Re-resolves the cached pin types after programmatic pin edits. Returns
    [[nodiscard]] bool RefreshPinTypes(std::string* InError = nullptr);

    [[nodiscard]] std::uint32_t NodeCount() const noexcept
    {
        return MNodeCount;
    }
    [[nodiscard]] std::uint32_t PinCount() const noexcept
    {
        return MPinCount;
    }
    [[nodiscard]] std::uint32_t ParamCount() const noexcept
    {
        return MParamCount;
    }
    [[nodiscard]] const MaterialNode& Node(std::uint16_t InIndex) const noexcept;
    [[nodiscard]] MaterialNode& Node(std::uint16_t InIndex) noexcept;
    [[nodiscard]] const MaterialPin& Pin(std::uint16_t InIndex) const noexcept;
    [[nodiscard]] MaterialPin& Pin(std::uint16_t InIndex) noexcept;
    [[nodiscard]] const MaterialParam& Param(std::uint16_t InIndex) const noexcept;
    [[nodiscard]] MaterialParam& Param(std::uint16_t InIndex) noexcept;
    // The node's contiguous input-pin range, in slot order.
    [[nodiscard]] std::span<const MaterialPin> InputPins(std::uint16_t InNode) const noexcept;
    [[nodiscard]] std::uint16_t OutputNode() const noexcept;
    [[nodiscard]] bool UsesNodeKind(MaterialNodeKind InKind) const noexcept;
    [[nodiscard]] MaterialGraphRequirements Requirements() const noexcept;

    // Tracks the graph's semantic content; UI positions are excluded on purpose.
    [[nodiscard]] std::uint64_t Hash() const noexcept;

    // Full structural validation: one Output, connected required pins, no type
    [[nodiscard]] bool Validate(std::string* InError = nullptr) const;
    // Resolves the output type of every node (Count for the Output node) in
    [[nodiscard]] bool ResolveTypes(std::span<MaterialValueType> InNodeTypes, std::string* InError = nullptr) const;
    // Writes the indices of the nodes reachable from the Output in dependency
    [[nodiscard]] std::uint32_t BuildEvaluationOrder(std::span<std::uint16_t> InOrder,
                                                     std::string* InError = nullptr) const;

private:
    std::array<MaterialNode, kMaxMaterialNodes> MNodes{};
    std::array<MaterialPin, kMaxMaterialPins> MPins{};
    std::array<MaterialParam, kMaxMaterialParams> MParams{};
    std::uint32_t MNodeCount = 0;
    std::uint32_t MPinCount = 0;
    std::uint32_t MParamCount = 0;
};

static_assert(std::is_trivially_copyable_v<MaterialNode>);
static_assert(std::is_trivially_copyable_v<MaterialPin>);
static_assert(std::is_trivially_copyable_v<MaterialParam>);
static_assert(std::is_trivially_copyable_v<MaterialGraph>);
static_assert(std::is_standard_layout_v<MaterialGraph>);
static_assert(sizeof(MaterialNode) == 20, "MaterialNode is a 20-byte DOD record");
static_assert(sizeof(MaterialPin) == 8, "MaterialPin is an 8-byte DOD record");
static_assert(sizeof(MaterialParam) == 24, "MaterialParam is a 24-byte DOD record");
}
