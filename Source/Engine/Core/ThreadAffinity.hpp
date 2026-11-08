// FemboyEngine2D - OS main-thread affinity.
#pragma once

#include <cstdint>
#include <thread>

namespace Fby
{

// Coarse role of the calling thread. The role is advisory for diagnostics and
// is enforced at subsystem boundaries with RequireMainThread/RequireThreadRole.
enum class ThreadRole : std::uint8_t
{
    Unknown = 0,
    Main,
    Render,
    Simulation,
    Audio,
    Video,
    Network,
    Io,
    Document,
    Worker,
};

namespace Detail
{
inline ThreadRole& ThreadRoleStorage() noexcept
{
    static thread_local ThreadRole ScopedRole = ThreadRole::Unknown;
    return ScopedRole;
}
}

[[nodiscard]] inline const char* ThreadRoleName(ThreadRole InRole) noexcept
{
    switch (InRole)
    {
        case ThreadRole::Unknown:
            return "Unknown";
        case ThreadRole::Main:
            return "Main";
        case ThreadRole::Render:
            return "Render";
        case ThreadRole::Simulation:
            return "Simulation";
        case ThreadRole::Audio:
            return "Audio";
        case ThreadRole::Video:
            return "Video";
        case ThreadRole::Network:
            return "Network";
        case ThreadRole::Io:
            return "Io";
        case ThreadRole::Document:
            return "Document";
        case ThreadRole::Worker:
            return "Worker";
    }
    return "?";
}

// Assigns the calling thread's role. The main thread is bound by MarkMainThread;
// subsystem-owned threads bind their role at the start of their loop. The
// storage is function-local so a tool that links JobSystem without the full
// engine still resolves BindThreadRole.
inline void BindThreadRole(ThreadRole InRole) noexcept
{
    Detail::ThreadRoleStorage() = InRole;
}

[[nodiscard]] inline ThreadRole CurrentThreadRole() noexcept
{
    return Detail::ThreadRoleStorage();
}

// Marks the calling thread as the process main thread. Startup-only; safe to
void MarkMainThread() noexcept;

// True when the calling thread is the process main thread.
[[nodiscard]] bool IsMainThread() noexcept;

// The recorded main thread id. Use it to seed a subsystem's own affinity field
[[nodiscard]] std::thread::id MainThreadId() noexcept;

// Logs an error and trips the debug assert when called off the main thread.
[[nodiscard]] bool RequireMainThread(const char* InWho) noexcept;

// Enforces a specific role. Main accepts the bound main thread; other roles
// require that the calling thread bound that exact role.
[[nodiscard]] bool RequireThreadRole(ThreadRole InRole, const char* InWho) noexcept;
}
