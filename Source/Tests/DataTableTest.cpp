// FemboyEngine2D - CSV table parsing/lookup test.
#include "Engine/Core/DataTable.hpp"

#include <cstdio>
#include <string>

int main()
{
    constexpr std::string_view kCsv =
        "name,hp,rate,enabled,note\n"
        "slime,10,0.5,true,\"slow, green\"\n"
        "bat,4,1.25,false,\"say \"\"hi\"\"\"\n";
    Fby::DataTable ScopedTable;
    if (!Fby::ParseDataTable(kCsv, &ScopedTable))
    {
        std::fprintf(stderr, "table parse failed\n");
        return 1;
    }
    if (ScopedTable.RowCount() != 2 || ScopedTable.ColumnCount() != 5 ||
        ScopedTable.Cell(0, "name") != "slime" || ScopedTable.Cell(1, "note") != "say \"hi\"")
    {
        std::fprintf(stderr, "table content mismatch\n");
        return 1;
    }
    std::int64_t ScopedHp = 0;
    float ScopedRate = 0.0f;
    bool ScopedEnabled = false;
    if (!ScopedTable.GetInt(0, "hp", &ScopedHp) || ScopedHp != 10 ||
        !ScopedTable.GetFloat(1, "rate", &ScopedRate) || ScopedRate < 1.24f || ScopedRate > 1.26f ||
        !ScopedTable.GetBool(0, "enabled", &ScopedEnabled) || !ScopedEnabled)
    {
        std::fprintf(stderr, "table typed lookup failed\n");
        return 1;
    }
    std::printf("data table: rows=%zu columns=%zu\n", ScopedTable.RowCount(), ScopedTable.ColumnCount());
    return 0;
}
