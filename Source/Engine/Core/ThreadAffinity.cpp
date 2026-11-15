// FemboyEngine2D - OS main-thread affinity implementation.
#include "Engine/Core/ThreadAffinity.hpp"

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"

#include <atomic>
#include <thread>

namespace Fby
{
namespace
{
// A namespace-scope dynamic initialiser runs on the process main thread.
std::atomic<std::thread::id> GMainThreadId{std::this_thread::get_id()};
}

void MarkMainThread() noexcept
{
    GMainThreadId.store(std::this_thread::get_id(), std::memory_order_release);
    BindThreadRole(ThreadRole::Main);
}

bool IsMainThread() noexcept
{
    return std::this_thread::get_id() == GMainThreadId.load(std::memory_order_acquire);
}

std::thread::id MainThreadId() noexcept
{
    return GMainThreadId.load(std::memory_order_acquire);
}

bool RequireMainThread(const char* InWho) noexcept
{
    if (IsMainThread())
    {
        return true;
    }
    LogError("thread", "{} must be called on the OS main thread; the call was made on role {} and is dropped",
             InWho != nullptr ? InWho : "operation", ThreadRoleName(CurrentThreadRole()));
    FBY_ASSERT_MSG(IsMainThread(), "operation requires the OS main thread (see Engine/Core/ThreadAffinity.hpp)");
    return false;
}

bool RequireThreadRole(ThreadRole InRole, const char* InWho) noexcept
{
    if (InRole == ThreadRole::Main)
    {
        return RequireMainThread(InWho);
    }
    if (CurrentThreadRole() == InRole)
    {
        return true;
    }
    LogError("thread", "{} requires role {} but the caller is {}; the call is dropped",
             InWho != nullptr ? InWho : "operation", ThreadRoleName(InRole),
             ThreadRoleName(CurrentThreadRole()));
    FBY_ASSERT_MSG(CurrentThreadRole() == InRole, "operation requires a specific thread role");
    return false;
}
}
