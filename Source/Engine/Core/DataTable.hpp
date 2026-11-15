// FemboyEngine2D - small CSV/TSV table for gameplay and UI data.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Fby
{

struct DataTable
{
    std::vector<std::string> Columns;
    std::vector<std::vector<std::string>> Rows;

    [[nodiscard]] bool Empty() const noexcept
    {
        return Columns.empty();
    }
    [[nodiscard]] std::size_t RowCount() const noexcept
    {
        return Rows.size();
    }
    [[nodiscard]] std::size_t ColumnCount() const noexcept
    {
        return Columns.size();
    }
    // -1 when the column is absent.
    [[nodiscard]] std::int32_t ColumnIndex(std::string_view InName) const noexcept;
    [[nodiscard]] std::string_view Cell(std::size_t InRow, std::size_t InColumn) const noexcept;
    [[nodiscard]] std::string_view Cell(std::size_t InRow, std::string_view InColumn) const noexcept;
    [[nodiscard]] bool GetInt(std::size_t InRow, std::string_view InColumn, std::int64_t* OutValue) const noexcept;
    [[nodiscard]] bool GetFloat(std::size_t InRow, std::string_view InColumn, float* OutValue) const noexcept;
    [[nodiscard]] bool GetBool(std::size_t InRow, std::string_view InColumn, bool* OutValue) const noexcept;
    void Clear() noexcept;
};

// Parses RFC-4180-style CSV. `InDelimiter` may be ',' or '\t'. The first record
// is the header. Quoted fields may contain delimiters, CRLF and doubled quotes.
[[nodiscard]] bool ParseDataTable(std::string_view InText, DataTable* OutTable, char InDelimiter = ',');

}
