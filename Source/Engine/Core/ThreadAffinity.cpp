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
// A namespace-scope dynamic initialiser runs on the process main thread before
std::atomic<std::thread::id> GMainThreadId{std::this_thread::get_id()};
}

void MarkMainThread() noexcept
{
    GMainThreadId.store(std::this_thread::get_id(), std::memory_order_release);
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
    LogError("thread", "{} must be called on the OS main thread; the call was made from a worker and is dropped",
             InWho != nullptr ? InWho : "operation");
    FBY_ASSERT_MSG(IsMainThread(), "operation requires the OS main thread (see Engine/Core/ThreadAffinity.hpp)");
    return false;
}
}
