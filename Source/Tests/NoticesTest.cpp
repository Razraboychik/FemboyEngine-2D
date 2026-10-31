// FemboyEngine2D - shipped third-party notice table tests.
#include "Engine/Core/ThirdPartyNotices.hpp"

#include <cstdint>
#include <cstdio>
#include <string_view>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

[[nodiscard]] const Fby::Notices::NoticeEntry* Find(std::string_view InName)
{
    for (const Fby::Notices::NoticeEntry& ScopedEntry : Fby::Notices::Entries())
    {
        if (ScopedEntry.Name == InName)
        {
            return &ScopedEntry;
        }
    }
    return nullptr;
}
}

int main()
{
    using namespace Fby::Notices;

    const std::span<const NoticeEntry> ScopedEntries = Entries();
    FBY_CHECK(ScopedEntries.size() >= 15);

    for (const NoticeEntry& ScopedEntry : ScopedEntries)
    {
        FBY_CHECK(!ScopedEntry.Name.empty());
        FBY_CHECK(!ScopedEntry.Version.empty());
        FBY_CHECK(!ScopedEntry.Spdx.empty());
        FBY_CHECK(!ScopedEntry.Purpose.empty());
        FBY_CHECK(!ScopedEntry.Copyright.empty());
        FBY_CHECK(ScopedEntry.LicenseText.size() > 32);
    }

    const NoticeEntry* ScopedMonocypher = Find("Monocypher");
    FBY_CHECK(ScopedMonocypher != nullptr);
    if (ScopedMonocypher != nullptr)
    {
        FBY_CHECK(ScopedMonocypher->Spdx.find("BSD-2-Clause") != std::string_view::npos);
        FBY_CHECK(ScopedMonocypher->Copyright.find("Loup Vaillant") != std::string_view::npos);
        FBY_CHECK(ScopedMonocypher->LicenseText.find("Redistribution and use in source and binary forms") !=
                  std::string_view::npos);
    }

    const NoticeEntry* ScopedImGui = Find("Dear ImGui");
    FBY_CHECK(ScopedImGui != nullptr);
    if (ScopedImGui != nullptr)
    {
        FBY_CHECK(ScopedImGui->LicenseText.find("The MIT License") != std::string_view::npos);
    }

    const std::string_view ScopedAll = AllText();
    FBY_CHECK(ScopedAll.size() > 10000);
    FBY_CHECK(ScopedAll.find("Mozilla Public License 2.0") != std::string_view::npos);
    FBY_CHECK(ScopedAll.find("Monocypher") != std::string_view::npos);
    FBY_CHECK(ScopedAll.find("SDL3") != std::string_view::npos);

    if (GFailures == 0)
    {
        std::printf("notices_test: all checks passed (%zu components)\n", ScopedEntries.size());
        return 0;
    }
    std::fprintf(stderr, "notices_test: %u failure(s)\n", GFailures);
    return 1;
}
