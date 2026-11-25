// FemboyEngine2D - non-owning callable reference.
//
// The C++26 standard library calls this std::function_ref; the engine baseline
// is C++23, so this small equivalent lives here. It stores a pointer and a
// trampoline and never allocates, which is what the engine's callbacks need.
#pragma once

#include <type_traits>
#include <utility>

namespace Fby
{
template <typename Signature>
class FunctionRef;

template <typename Return, typename... Args>
class FunctionRef<Return(Args...)>
{
public:
    template <typename Callable>
        requires std::is_rvalue_reference_v<Callable&&>
    FunctionRef(Callable&&) = delete;

    template <typename Callable>
    FunctionRef(const Callable& InCallable) noexcept
        : MUser(&InCallable),
          MInvoke(+[](const void* InUser, Args... InArgs) -> Return
                  { return (*static_cast<const Callable*>(InUser))(std::forward<Args>(InArgs)...); })
    {
    }

    FunctionRef(Return (*InFunction)(Args...)) noexcept : MFree(InFunction)
    {
    }

    Return operator()(Args... InArgs) const
    {
        if (MFree != nullptr)
        {
            return MFree(std::forward<Args>(InArgs)...);
        }
        return MInvoke(MUser, std::forward<Args>(InArgs)...);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return MFree != nullptr || MInvoke != nullptr;
    }

private:
    const void* MUser = nullptr;
    Return (*MInvoke)(const void*, Args...) = nullptr;
    Return (*MFree)(Args...) = nullptr;
};
}
