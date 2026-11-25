// FemboyEngine2D - material compiler process argument contract.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Tools::MaterialCompiler
{

// Rejects empty arguments and embedded line breaks/NULs. Every other character
// is preserved literally: the process launcher passes arguments as an array, so
// shell metacharacters have no meaning.
[[nodiscard]] inline bool IsSafeProcessArgument(std::string_view InArgument) noexcept
{
    if (InArgument.empty())
    {
        return false;
    }
    for (const char ScopedCharacter : InArgument)
    {
        if (ScopedCharacter == '\0' || ScopedCharacter == '\r' || ScopedCharacter == '\n')
        {
            return false;
        }
    }
    return true;
}

// The complete dxc command line, without a shell. Element zero is the compiler
// path/name, followed by -T <profile> -E main -Fo <dxil> <hlsl>.
[[nodiscard]] inline std::optional<std::vector<std::string>> BuildDxcArguments(std::string_view InCompiler,
                                                                               std::string_view InHlslPath,
                                                                               std::string_view InDxilPath,
                                                                               std::string_view InProfile) noexcept
{
    if (!IsSafeProcessArgument(InCompiler) || !IsSafeProcessArgument(InHlslPath) ||
        !IsSafeProcessArgument(InDxilPath) || !IsSafeProcessArgument(InProfile))
    {
        return std::nullopt;
    }
    return std::vector<std::string>{std::string(InCompiler), "-T", std::string(InProfile), "-E", "main",
                                    "-Fo", std::string(InDxilPath), std::string(InHlslPath)};
}

// Quotes one argument for the Microsoft C runtime command-line parser. The
// CreateProcess command line is still not a shell command; this only preserves
// spaces, quotes and backslashes in paths.
[[nodiscard]] inline std::wstring QuoteWindowsArgument(std::wstring_view InArgument)
{
    const bool ScopedNeedsQuotes =
        InArgument.empty() || InArgument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
    if (!ScopedNeedsQuotes)
    {
        return std::wstring(InArgument);
    }
    std::wstring ScopedQuoted;
    ScopedQuoted.reserve(InArgument.size() + 2);
    ScopedQuoted.push_back(L'"');
    std::size_t ScopedBackslashes = 0;
    for (const wchar_t ScopedCharacter : InArgument)
    {
        if (ScopedCharacter == L'\\')
        {
            ++ScopedBackslashes;
            continue;
        }
        if (ScopedCharacter == L'"')
        {
            ScopedQuoted.append(ScopedBackslashes * 2 + 1, L'\\');
            ScopedQuoted.push_back(L'"');
            ScopedBackslashes = 0;
            continue;
        }
        ScopedQuoted.append(ScopedBackslashes, L'\\');
        ScopedBackslashes = 0;
        ScopedQuoted.push_back(ScopedCharacter);
    }
    ScopedQuoted.append(ScopedBackslashes * 2, L'\\');
    ScopedQuoted.push_back(L'"');
    return ScopedQuoted;
}
}
