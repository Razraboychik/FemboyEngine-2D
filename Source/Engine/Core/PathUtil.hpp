// FemboyEngine2D - UTF-8 filesystem path helpers.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Fby::Core
{

// Project paths are UTF-8 on every platform. On Windows a narrow std::string
// would be interpreted in the ANSI code page, so route the bytes through
// char8_t before constructing the path.
[[nodiscard]] inline std::filesystem::path PathFromUtf8(std::string_view InPath)
{
#if defined(_WIN32)
    const auto* ScopedBegin = reinterpret_cast<const char8_t*>(InPath.data());
    return std::filesystem::path(std::u8string(ScopedBegin, ScopedBegin + InPath.size()));
#else
    return std::filesystem::path(std::string(InPath));
#endif
}

// The inverse conversion for APIs that still take a UTF-8 std::string.
[[nodiscard]] inline std::string Utf8String(const std::filesystem::path& InPath)
{
    const std::u8string ScopedUtf8 = InPath.u8string();
    return std::string(reinterpret_cast<const char*>(ScopedUtf8.data()), ScopedUtf8.size());
}

}
