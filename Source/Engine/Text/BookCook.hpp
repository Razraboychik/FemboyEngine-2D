// FemboyEngine2D - source document to cooked book.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Text
{

enum class BookCookError : std::uint8_t
{
    Ok = 0,
    Empty,
    InvalidFrontMatter,
    UnclosedCodeFence,
    TooManyBlocks,
    TooManySpans,
    TooManyStyles,
    TooManyInlines,
    TextTooLarge,
    InvalidMarkup,
};

[[nodiscard]] const char* BookCookErrorName(BookCookError InError) noexcept;

// Source is UTF-8 Markdown with optional TOML front matter. The writer produces
// the .fbybook bytes consumed by BookReader; it does not touch the filesystem.
[[nodiscard]] BookCookError CookBook(std::string_view InSource, std::vector<std::byte>* OutBytes,
                                     std::string* InError = nullptr);

}
