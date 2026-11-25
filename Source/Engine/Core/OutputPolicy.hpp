// FemboyEngine2D - independently switchable render and audio output policies.
//
// The policy is a runtime safety boundary, not an engine-imposed player setting.
// A game registers settings that call these methods; safe mode restores Full.
#pragma once

#include <atomic>
#include <cstdint>

namespace Fby::Output
{

enum class RenderPolicy : std::uint8_t
{
    Full = 0,
    NoWorld = 1,
    Disabled = 2,
    Headless = 3,
};

enum class AudioPolicy : std::uint8_t
{
    Full = 0,
    CaptionsOnly = 1,
    Disabled = 2,
};

class Policy
{
public:
    [[nodiscard]] static Policy& Instance() noexcept
    {
        static Policy Scoped;
        return Scoped;
    }

    void SetRenderPolicy(RenderPolicy InPolicy) noexcept
    {
        MRender.store(InPolicy, std::memory_order_release);
    }
    [[nodiscard]] RenderPolicy Render() const noexcept
    {
        return MRender.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool RenderWorldEnabled() const noexcept
    {
        return Render() == RenderPolicy::Full;
    }
    [[nodiscard]] bool RenderEnabled() const noexcept
    {
        return Render() != RenderPolicy::Disabled && Render() != RenderPolicy::Headless;
    }

    void SetAudioPolicy(AudioPolicy InPolicy) noexcept
    {
        MAudio.store(InPolicy, std::memory_order_release);
    }
    [[nodiscard]] AudioPolicy Audio() const noexcept
    {
        return MAudio.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool AudioEnabled() const noexcept
    {
        return Audio() == AudioPolicy::Full;
    }
    [[nodiscard]] bool CaptionsEnabled() const noexcept
    {
        return Audio() != AudioPolicy::Disabled;
    }

    // A persisted dangerous combination must not be able to hide every output.
    [[nodiscard]] bool IsPlayerVisible() const noexcept
    {
        return RenderEnabled() || AudioEnabled();
    }

    void RequestSafeMode() noexcept
    {
        MSafe.store(true, std::memory_order_release);
    }
    [[nodiscard]] bool SafeModeRequested() const noexcept
    {
        return MSafe.load(std::memory_order_acquire);
    }
    void RestoreSafeMode() noexcept
    {
        SetRenderPolicy(RenderPolicy::Full);
        SetAudioPolicy(AudioPolicy::Full);
        MSafe.store(false, std::memory_order_release);
    }

private:
    std::atomic<RenderPolicy> MRender{RenderPolicy::Full};
    std::atomic<AudioPolicy> MAudio{AudioPolicy::Full};
    std::atomic<bool> MSafe{false};
};

}
