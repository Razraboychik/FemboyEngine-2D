// FemboyEngine2D - debug validation of declared system access.
#pragma once

#include "Engine/Core/Assert.hpp"

#include <array>
#include <cstdint>

namespace Fby::Fcs::Detail
{

#if defined(FBY_ENABLE_ACCESS_VALIDATION) && FBY_ENABLE_ACCESS_VALIDATION

// The scheduler publishes the current system's declared component masks into
// thread-local storage. FCS then checks each view/get against them. The whole
// class compiles to empty calls in release builds.
class AccessValidation
{
public:
    static void Begin(const std::uint64_t* InReads, const std::uint64_t* InWrites) noexcept
    {
        // A legacy system that declared no access at all is not validated: the
        // alternative is breaking every un-migrated registration in debug.
        if (InReads == nullptr && InWrites == nullptr)
        {
            MActive = false;
            return;
        }
        for (std::size_t ScopedWord = 0; ScopedWord < MReads.size(); ++ScopedWord)
        {
            MReads[ScopedWord] = InReads != nullptr ? InReads[ScopedWord] : 0;
            MWrites[ScopedWord] = InWrites != nullptr ? InWrites[ScopedWord] : 0;
        }
        MActive = true;
    }

    static void End() noexcept
    {
        MActive = false;
    }

    static void Read(std::uint16_t InId) noexcept
    {
        Check(InId, false);
    }

    static void Write(std::uint16_t InId) noexcept
    {
        Check(InId, true);
    }

private:
    static void Check(std::uint16_t InId, bool InIsWrite) noexcept
    {
        if (!MActive)
        {
            return;
        }
        const std::size_t ScopedWord = static_cast<std::size_t>(InId) / 64u;
        const std::uint64_t ScopedBit = std::uint64_t{1} << (static_cast<std::size_t>(InId) % 64u);
        if (ScopedWord >= MReads.size())
        {
            return;
        }
        if (InIsWrite)
        {
            FBY_ASSERT_MSG((MWrites[ScopedWord] & ScopedBit) != 0,
                           "system wrote a component it did not declare in ComponentWrites");
        }
        else
        {
            FBY_ASSERT_MSG(((MReads[ScopedWord] | MWrites[ScopedWord]) & ScopedBit) != 0,
                           "system read a component it did not declare in ComponentReads/ComponentWrites");
        }
    }

    inline static thread_local std::array<std::uint64_t, 4> MReads{};
    inline static thread_local std::array<std::uint64_t, 4> MWrites{};
    inline static thread_local bool MActive = false;
};

#else

class AccessValidation
{
public:
    static void Begin(const std::uint64_t*, const std::uint64_t*) noexcept
    {
    }
    static void End() noexcept
    {
    }
    static void Read(std::uint16_t) noexcept
    {
    }
    static void Write(std::uint16_t) noexcept
    {
    }
};

#endif

// RAII wrapper so a throwing system cannot leave stale masks in the thread.
class ScopedAccess
{
public:
    ScopedAccess(const std::uint64_t* InReads, const std::uint64_t* InWrites) noexcept
    {
        AccessValidation::Begin(InReads, InWrites);
    }
    ~ScopedAccess()
    {
        AccessValidation::End();
    }

    ScopedAccess(const ScopedAccess&) = delete;
    ScopedAccess& operator=(const ScopedAccess&) = delete;
};

}
