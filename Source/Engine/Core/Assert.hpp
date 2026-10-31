// FemboyEngine2D - assertions.
#pragma once

#include <cstddef>

namespace Fby
{
// Reports a failed assertion and terminates. Defined in core/Log.cpp so there is
[[noreturn]] void FailAssert(const char* InExpression, const char* InFile, int InLine, const char* InMessage) noexcept;
}

#if defined(FBY_ENABLE_ASSERTS) && FBY_ENABLE_ASSERTS
#define FBY_ASSERT(expression)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression)) [[unlikely]]                                                                                \
        {                                                                                                              \
            ::Fby::FailAssert(#expression, __FILE__, static_cast<int>(__LINE__), nullptr);                             \
        }                                                                                                              \
    } while (false)

#define FBY_ASSERT_MSG(expression, message)                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression)) [[unlikely]]                                                                                \
        {                                                                                                              \
            ::Fby::FailAssert(#expression, __FILE__, static_cast<int>(__LINE__), message);                             \
        }                                                                                                              \
    } while (false)
#else
#define FBY_ASSERT(expression) ((void)0)
#define FBY_ASSERT_MSG(expression, message) ((void)0)
#endif

// Always evaluates and always throws on failure, debug or not: used for
#define FBY_VERIFY(expression)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression)) [[unlikely]]                                                                                \
        {                                                                                                              \
            ::Fby::FailAssert(#expression, __FILE__, static_cast<int>(__LINE__), "verification failed");               \
        }                                                                                                              \
    } while (false)

#define FBY_UNREACHABLE() ::Fby::FailAssert("unreachable", __FILE__, static_cast<int>(__LINE__), "unreachable code")
