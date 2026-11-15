// FemboyEngine2D - CSV/TSV table parser.
#include "Engine/Core/DataTable.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace Fby
{
namespace
{

[[nodiscard]] std::string_view Trim(std::string_view InValue) noexcept
{
    while (!InValue.empty() && std::isspace(static_cast<unsigned char>(InValue.front())) != 0)
    {
        InValue.remove_prefix(1);
    }
    while (!InValue.empty() && std::isspace(static_cast<unsigned char>(InValue.back())) != 0)
    {
        InValue.remove_suffix(1);
    }
    return InValue;
}

}

std::int32_t DataTable::ColumnIndex(std::string_view InName) const noexcept
{
    for (std::size_t ScopedIndex = 0; ScopedIndex < Columns.size(); ++ScopedIndex)
    {
        if (Columns[ScopedIndex] == InName)
        {
            return static_cast<std::int32_t>(ScopedIndex);
        }
    }
    return -1;
}

std::string_view DataTable::Cell(std::size_t InRow, std::size_t InColumn) const noexcept
{
    if (InRow >= Rows.size() || InColumn >= Rows[InRow].size())
    {
        return {};
    }
    return Rows[InRow][InColumn];
}

std::string_view DataTable::Cell(std::size_t InRow, std::string_view InColumn) const noexcept
{
    const std::int32_t ScopedIndex = ColumnIndex(InColumn);
    return ScopedIndex < 0 ? std::string_view{} : Cell(InRow, static_cast<std::size_t>(ScopedIndex));
}

bool DataTable::GetInt(std::size_t InRow, std::string_view InColumn, std::int64_t* OutValue) const noexcept
{
    if (OutValue == nullptr)
    {
        return false;
    }
    const std::string_view ScopedText = Trim(Cell(InRow, InColumn));
    if (ScopedText.empty())
    {
        return false;
    }
    std::int64_t ScopedValue = 0;
    const auto ScopedResult =
        std::from_chars(ScopedText.data(), ScopedText.data() + ScopedText.size(), ScopedValue);
    if (ScopedResult.ec != std::errc{} || ScopedResult.ptr != ScopedText.data() + ScopedText.size())
    {
        return false;
    }
    *OutValue = ScopedValue;
    return true;
}

bool DataTable::GetFloat(std::size_t InRow, std::string_view InColumn, float* OutValue) const noexcept
{
    if (OutValue == nullptr)
    {
        return false;
    }
    const std::string_view ScopedText = Trim(Cell(InRow, InColumn));
    if (ScopedText.empty())
    {
        return false;
    }
    char* ScopedEnd = nullptr;
    const std::string ScopedNullTerminated(ScopedText);
    const float ScopedValue = std::strtof(ScopedNullTerminated.c_str(), &ScopedEnd);
    if (ScopedEnd == ScopedNullTerminated.c_str() || *ScopedEnd != '\0' || !std::isfinite(ScopedValue))
    {
        return false;
    }
    *OutValue = ScopedValue;
    return true;
}

bool DataTable::GetBool(std::size_t InRow, std::string_view InColumn, bool* OutValue) const noexcept
{
    if (OutValue == nullptr)
    {
        return false;
    }
    const std::string_view ScopedText = Trim(Cell(InRow, InColumn));
    if (ScopedText == "1" || ScopedText == "true" || ScopedText == "TRUE" || ScopedText == "yes")
    {
        *OutValue = true;
        return true;
    }
    if (ScopedText == "0" || ScopedText == "false" || ScopedText == "FALSE" || ScopedText == "no")
    {
        *OutValue = false;
        return true;
    }
    return false;
}

void DataTable::Clear() noexcept
{
    Columns.clear();
    Rows.clear();
}

bool ParseDataTable(std::string_view InText, DataTable* OutTable, char InDelimiter)
{
    if (OutTable == nullptr || InText.empty() || InDelimiter == '\0')
    {
        return false;
    }
    OutTable->Clear();
    std::vector<std::string> ScopedRecord;
    std::string ScopedField;
    bool ScopedQuoted = false;
    bool ScopedSawAny = false;
    const auto ScopedEndField = [&]() noexcept
    {
        ScopedRecord.push_back(ScopedField);
        ScopedField.clear();
    };
    const auto ScopedEndRecord = [&]() noexcept
    {
        ScopedEndField();
        ScopedSawAny = true;
        if (OutTable->Columns.empty())
        {
            OutTable->Columns = ScopedRecord;
        }
        else
        {
            ScopedRecord.resize(OutTable->Columns.size());
            OutTable->Rows.push_back(ScopedRecord);
        }
        ScopedRecord.clear();
    };
    for (std::size_t ScopedIndex = 0; ScopedIndex < InText.size(); ++ScopedIndex)
    {
        const char ScopedCharacter = InText[ScopedIndex];
        if (ScopedQuoted)
        {
            if (ScopedCharacter == '"')
            {
                if (ScopedIndex + 1 < InText.size() && InText[ScopedIndex + 1] == '"')
                {
                    ScopedField.push_back('"');
                    ++ScopedIndex;
                }
                else
                {
                    ScopedQuoted = false;
                }
            }
            else
            {
                ScopedField.push_back(ScopedCharacter);
            }
            continue;
        }
        if (ScopedCharacter == '"' && ScopedField.empty())
        {
            ScopedQuoted = true;
        }
        else if (ScopedCharacter == InDelimiter)
        {
            ScopedEndField();
        }
        else if (ScopedCharacter == '\n')
        {
            ScopedEndRecord();
        }
        else if (ScopedCharacter != '\r')
        {
            ScopedField.push_back(ScopedCharacter);
        }
    }
    if (ScopedQuoted)
    {
        OutTable->Clear();
        return false;
    }
    if (!ScopedField.empty() || !ScopedRecord.empty())
    {
        ScopedEndRecord();
    }
    return ScopedSawAny && !OutTable->Columns.empty();
}

}
