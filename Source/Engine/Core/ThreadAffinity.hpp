// FemboyEngine2D - OS main-thread affinity.
#pragma once

#include <thread>

namespace Fby
{

// Marks the calling thread as the process main thread. Startup-only; safe to
void MarkMainThread() noexcept;

// True when the calling thread is the process main thread.
[[nodiscard]] bool IsMainThread() noexcept;

// The recorded main thread id. Use it to seed a subsystem's own affinity field
[[nodiscard]] std::thread::id MainThreadId() noexcept;

// Logs an error and trips the debug assert when called off the main thread.
[[nodiscard]] bool RequireMainThread(const char* InWho) noexcept;
}
