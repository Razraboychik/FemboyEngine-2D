// FemboyEngine2D - cloth to render-extract bridge.
#pragma once

#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/RHI/Mesh.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace Fby
{

class ClothRenderSystem
{
public:
    static constexpr std::uint32_t kMaxClothDraws = 64;

    ClothRenderSystem() = default;

    void Initialize(ClothSystem* InCloth) noexcept
    {
        MCloth = InCloth;
        MCount = 0;
        MTruncated = false;
    }

    // PostUpdate. Clears the previous frame and fills one item per valid
    void Collect(Ecs::SystemContext& InContext) noexcept;

    [[nodiscard]] std::span<const Rhi::MeshRenderItem> Items() const noexcept
    {
        return std::span<const Rhi::MeshRenderItem>(MItems.data(), MCount);
    }
    [[nodiscard]] std::uint32_t DrawCount() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] bool Truncated() const noexcept
    {
        return MTruncated;
    }

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, ClothRenderSystem& InSystem);

private:
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    ClothSystem* MCloth = nullptr;
    std::array<Rhi::MeshRenderItem, kMaxClothDraws> MItems{};
    std::uint32_t MCount = 0;
    bool MTruncated = false;
};
}
