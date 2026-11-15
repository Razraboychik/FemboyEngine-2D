// FemboyEngine2D - D3D12 FSR3 frame-generation bridge.
#pragma once

#include <cstdint>

namespace Fby::Rhi::Fsr3Dx12
{

[[nodiscard]] bool Available() noexcept;
[[nodiscard]] bool CreateContext(void* InDevice, std::uint32_t InRenderWidth, std::uint32_t InRenderHeight,
                                 std::uint32_t InOutputWidth, std::uint32_t InOutputHeight, void** OutContext) noexcept;
[[nodiscard]] bool Dispatch(void* InContext, void* InCommandList, void* InColor, void* InOutput,
                            std::uint32_t InOutputWidth, std::uint32_t InOutputHeight, bool InReset,
                            std::uint64_t InFrameId) noexcept;
void DestroyContext(void* InContext) noexcept;

}
