// FemboyEngine2D - allocation-free error and result vocabulary.
#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

namespace Fby::Core
{

enum class ErrorCode : std::uint8_t
{
    Unknown = 0,
    InvalidArgument,
    InvalidState,
    NotFound,
    Unsupported,
    AllocationFailed,
    AbiViolation,
    IoFailure,
};

struct Error
{
    ErrorCode Code = ErrorCode::Unknown;
    std::string_view Detail{};
};

template <typename T> using Result = std::expected<T, Error>;
using Status = std::expected<void, Error>;

[[nodiscard]] constexpr Error MakeError(ErrorCode InCode, std::string_view InDetail = {}) noexcept
{
    return Error{InCode, InDetail};
}

}
