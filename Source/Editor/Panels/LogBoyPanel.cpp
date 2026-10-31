// FemboyEngine2D - LogBoy implementation.
#include "Editor/Panels/LogBoyPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Core/JobSystem.hpp"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

namespace Fby::Editor
{
namespace
{
constexpr std::size_t kDrainBatch = 32;
constexpr double kRateWindowSeconds = 0.5;

[[nodiscard]] char AsciiLower(char InCharacter) noexcept
{
    return InCharacter >= 'A' && InCharacter <= 'Z' ? static_cast<char>(InCharacter - 'A' + 'a') : InCharacter;
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view InHaystack, const char* InNeedle) noexcept
{
    if (InNeedle == nullptr || InNeedle[0] == '\0')
    {
        return true;
    }
    const std::size_t ScopedNeedleLength = std::strlen(InNeedle);
    if (ScopedNeedleLength > InHaystack.size())
    {
        return false;
    }
    for (std::size_t ScopedStart = 0; ScopedStart + ScopedNeedleLength <= InHaystack.size(); ++ScopedStart)
    {
        bool ScopedMatch = true;
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedNeedleLength; ++ScopedIndex)
        {
            if (AsciiLower(InHaystack[ScopedStart + ScopedIndex]) != InNeedle[ScopedIndex])
            {
                ScopedMatch = false;
                break;
            }
        }
        if (ScopedMatch)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsDigit(char InCharacter) noexcept
{
    return InCharacter >= '0' && InCharacter <= '9';
}

// Parses a system hash the three ways a developer types one in a console:
[[nodiscard]] bool ParseHash(std::string_view InText, std::uint64_t* OutHash) noexcept
{
    if (OutHash == nullptr || InText.empty())
    {
        return false;
    }
    int ScopedBase = 10;
    if (InText.size() > 2 && InText[0] == '0' && (InText[1] == 'x' || InText[1] == 'X'))
    {
        ScopedBase = 16;
        InText.remove_prefix(2);
    }
    else if (InText.size() > 0 && InText[0] == '#')
    {
        ScopedBase = 16;
        InText.remove_prefix(1);
        if (InText.size() > 2 && InText[0] == '0' && (InText[1] == 'x' || InText[1] == 'X'))
        {
            InText.remove_prefix(2);
        }
    }
    if (InText.empty())
    {
        return false;
    }
    std::uint64_t ScopedValue = 0;
    for (const char ScopedCharacter : InText)
    {
        std::uint32_t ScopedDigit = 0;
        if (ScopedCharacter >= '0' && ScopedCharacter <= '9')
        {
            ScopedDigit = static_cast<std::uint32_t>(ScopedCharacter - '0');
        }
        else if (ScopedBase == 16 && ScopedCharacter >= 'a' && ScopedCharacter <= 'f')
        {
            ScopedDigit = static_cast<std::uint32_t>(ScopedCharacter - 'a' + 10);
        }
        else if (ScopedBase == 16 && ScopedCharacter >= 'A' && ScopedCharacter <= 'F')
        {
            ScopedDigit = static_cast<std::uint32_t>(ScopedCharacter - 'A' + 10);
        }
        else
        {
            return false;
        }
        ScopedValue = ScopedValue * static_cast<std::uint64_t>(ScopedBase) + ScopedDigit;
    }
    *OutHash = ScopedValue;
    return true;
}

[[nodiscard]] ImVec4 LevelColor(LogBoySeverity InSeverity) noexcept
{
    return ImGui::ColorConvertU32ToFloat4(LogBoySeverityColor(InSeverity));
}

// The engine Log() bridge. A plain function pointer keeps the sink free of
std::atomic<LogBoyPanel*> LogBoyBridge{nullptr};

void LogBoyObserver(LogLevel InLevel, std::string_view InChannel, std::string_view InMessage) noexcept
{
    LogBoyPanel* ScopedTarget = LogBoyBridge.load(std::memory_order_acquire);
    if (ScopedTarget != nullptr)
    {
        ScopedTarget->PushFromEngine(InLevel, InChannel, InMessage);
    }
}
}

void LogBoyPanel::Initialize()
{
    LogBoyBridge.store(this, std::memory_order_release);
    SetLogObserver(&LogBoyObserver);
    MInstalled = true;
    MFirstTimestamp = 0;
    MRateWindowStart = 0.0;
    MLastRateTime = 0.0;
    MRateWindowBase = 0;
    MSystem.Log(LogBoySeverity::Info, "LogBoy", "ring armed: {} records, {} bytes per record", kLogBoyCapacity,
                static_cast<unsigned>(sizeof(LogBoyRecord)));
}

void LogBoyPanel::Shutdown()
{
    if (!MInstalled)
    {
        return;
    }
    SetLogObserver(nullptr);
    LogBoyBridge.store(nullptr, std::memory_order_release);
    MInstalled = false;
}

void LogBoyPanel::PushFromEngine(LogLevel InLevel, std::string_view InChannel, std::string_view InMessage) noexcept
{
    const std::uint64_t ScopedHash = LogBoySystem::HashSystem(InChannel);
    MSystem.PushText(LogBoySeverityFromLevel(InLevel), ScopedHash, InMessage);
}

const LogBoyRecord& LogBoyPanel::ViewAt(std::uint32_t InIndex) const noexcept
{
    const std::uint32_t ScopedOldest = (MViewHead + kLogBoyVisibleRecords - MViewCount) % kLogBoyVisibleRecords;
    return MView[(ScopedOldest + InIndex) % kLogBoyVisibleRecords];
}

void LogBoyPanel::DrainRing()
{
    LogBoyRecord ScopedBatch[kDrainBatch]{};
    for (;;)
    {
        const std::uint32_t ScopedDrained =
            MSystem.Drain(std::span<LogBoyRecord>(ScopedBatch, static_cast<std::size_t>(kDrainBatch)));
        if (ScopedDrained == 0)
        {
            return;
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedDrained; ++ScopedIndex)
        {
            const LogBoyRecord& ScopedRecord = ScopedBatch[ScopedIndex];
            if (MFirstTimestamp == 0)
            {
                MFirstTimestamp = ScopedRecord.timestampNanoseconds;
            }
            ++MSeenTotal;
            if (ScopedRecord.severity == static_cast<std::uint8_t>(LogBoySeverity::Desync))
            {
                ++MDesyncCount;
            }
            if (MPaused)
            {
                continue;
            }
            MView[MViewHead] = ScopedRecord;
            MViewHead = (MViewHead + 1) % kLogBoyVisibleRecords;
            if (MViewCount < kLogBoyVisibleRecords)
            {
                ++MViewCount;
            }
        }
    }
}

void LogBoyPanel::ParseQuery()
{
    MQueryKind = QueryKind::None;
    MQueryHash = 0;
    MQueryLower.fill('\0');

    std::string_view ScopedQuery(MSearch);
    while (!ScopedQuery.empty() && (ScopedQuery.front() == ' ' || ScopedQuery.front() == '\t'))
    {
        ScopedQuery.remove_prefix(1);
    }
    while (!ScopedQuery.empty() && (ScopedQuery.back() == ' ' || ScopedQuery.back() == '\t'))
    {
        ScopedQuery.remove_suffix(1);
    }
    if (ScopedQuery.empty())
    {
        return;
    }

    if (ScopedQuery.starts_with("sys:"))
    {
        std::string_view ScopedName = ScopedQuery.substr(4);
        while (!ScopedName.empty() && ScopedName.front() == ' ')
        {
            ScopedName.remove_prefix(1);
        }
        MQueryHash = LogBoySystem::HashSystem(ScopedName);
        MQueryKind = QueryKind::SystemHash;
        return;
    }

    const bool ScopedPrefixed = ScopedQuery.front() == '#' || (ScopedQuery.size() > 2 && ScopedQuery[0] == '0' &&
                                                              (ScopedQuery[1] == 'x' || ScopedQuery[1] == 'X'));
    if (ScopedPrefixed && ParseHash(ScopedQuery, &MQueryHash))
    {
        MQueryKind = QueryKind::SystemHash;
        return;
    }

    bool ScopedAllDigits = ScopedQuery.size() >= 6;
    for (const char ScopedCharacter : ScopedQuery)
    {
        if (!IsDigit(ScopedCharacter))
        {
            ScopedAllDigits = false;
            break;
        }
    }
    if (ScopedAllDigits && ParseHash(ScopedQuery, &MQueryHash))
    {
        MQueryKind = QueryKind::SystemHash;
        return;
    }

    MQueryKind = QueryKind::Text;
    const std::size_t ScopedLength = std::min<std::size_t>(ScopedQuery.size(), MQueryLower.size() - 1);
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedLength; ++ScopedIndex)
    {
        MQueryLower[ScopedIndex] = AsciiLower(ScopedQuery[ScopedIndex]);
    }
}

bool LogBoyPanel::Passes(const LogBoyRecord& InRecord) const noexcept
{
    const std::uint32_t ScopedSeverity = InRecord.severity;
    if (ScopedSeverity >= kLogBoySeverityCount || !MLevelEnabled[ScopedSeverity])
    {
        return false;
    }
    if (MSystemFilter != 0 && InRecord.systemHash != MSystemFilter)
    {
        return false;
    }
    switch (MQueryKind)
    {
        case QueryKind::None:
            return true;
        case QueryKind::SystemHash:
            return InRecord.systemHash == MQueryHash;
        case QueryKind::Text:
        {
            const std::string_view ScopedText(InRecord.text, InRecord.textLength);
            if (ContainsCaseInsensitive(ScopedText, MQueryLower.data()))
            {
                return true;
            }
            return ContainsCaseInsensitive(MSystem.SystemNameFor(InRecord.systemHash), MQueryLower.data());
        }
    }
    return true;
}

void LogBoyPanel::RefreshRows()
{
    MRowCount = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MViewCount; ++ScopedIndex)
    {
        if (Passes(ViewAt(ScopedIndex)))
        {
            MRowIndex[MRowCount] = static_cast<std::uint16_t>(ScopedIndex);
            ++MRowCount;
        }
    }
    MHiddenCount = MViewCount - MRowCount;
}

void LogBoyPanel::DrawToolbar()
{
    const std::uint64_t ScopedDropped = MSystem.Dropped();
    const std::uint64_t ScopedPending = MSystem.Pending();

    ImGui::TextColored(LevelColor(LogBoySeverity::Info), "logged %llu", static_cast<unsigned long long>(MSeenTotal));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("queued %llu", static_cast<unsigned long long>(ScopedPending));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ScopedDropped > 0)
    {
        ImGui::TextColored(LevelColor(LogBoySeverity::Critical), "dropped %llu",
                           static_cast<unsigned long long>(ScopedDropped));
    }
    else
    {
        ImGui::TextDisabled("dropped 0");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("%.1f msg/s", MRate);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("match %u / %u", MRowCount, MViewCount);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("dropped-by-filter %u", MHiddenCount);

    if (ImGui::Button(MPaused ? "Resume" : "Pause"))
    {
        MPaused = !MPaused;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        MSystem.Clear();
        MViewCount = 0;
        MViewHead = 0;
        MRowCount = 0;
        MSeenTotal = 0;
        MDesyncCount = 0;
        MFirstTimestamp = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy visible"))
    {
        std::size_t ScopedCursor = 0;
        MClipboard[0] = '\0';
        for (std::uint32_t ScopedRow = 0; ScopedRow < MRowCount && ScopedCursor + 1 < kLogBoyClipboardBytes; ++ScopedRow)
        {
            const LogBoyRecord& ScopedRecord = ViewAt(MRowIndex[ScopedRow]);
            const double ScopedSeconds =
                MFirstTimestamp == 0
                    ? 0.0
                    : static_cast<double>(ScopedRecord.timestampNanoseconds - MFirstTimestamp) / 1.0e9;
            const char* ScopedSystemName = MSystem.SystemNameFor(ScopedRecord.systemHash).empty()
                                               ? "?"
                                               : MSystem.SystemNameFor(ScopedRecord.systemHash).data();
            const int ScopedWritten = std::snprintf(MClipboard + ScopedCursor, kLogBoyClipboardBytes - ScopedCursor,
                                                    "[%8.3f] %-8s %016llx %-18s %.*s\n", ScopedSeconds,
                                                    LogBoySeverityName(static_cast<LogBoySeverity>(ScopedRecord.severity)),
                                                    static_cast<unsigned long long>(ScopedRecord.systemHash),
                                                    ScopedSystemName, static_cast<int>(ScopedRecord.textLength),
                                                    ScopedRecord.text);
            if (ScopedWritten <= 0)
            {
                break;
            }
            ScopedCursor += static_cast<std::size_t>(ScopedWritten);
        }
        ImGui::SetClipboardText(MClipboard);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &MAutoScroll);
    ImGui::SameLine();
    ImGui::Checkbox("Frame", &MShowFrame);

    for (std::uint32_t ScopedSeverity = 0; ScopedSeverity < kLogBoySeverityCount; ++ScopedSeverity)
    {
        const auto ScopedLevel = static_cast<LogBoySeverity>(ScopedSeverity);
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(ScopedLevel));
        ImGui::Checkbox(LogBoySeverityName(ScopedLevel), &MLevelEnabled[ScopedSeverity]);
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::InputTextWithHint("##logboy_query", "search text, #hash, 0xhex, sys:name", MSearch, sizeof(MSearch)))
    {
        ParseQuery();
    }
    ImGui::SameLine();

    char ScopedCurrentSystem[64] = "All systems";
    if (MSystemFilter != 0)
    {
        const std::string_view ScopedName = MSystem.SystemNameFor(MSystemFilter);
        std::snprintf(ScopedCurrentSystem, sizeof(ScopedCurrentSystem), "%016llx %.*s",
                      static_cast<unsigned long long>(MSystemFilter), static_cast<int>(ScopedName.size()),
                      ScopedName.empty() ? "?" : ScopedName.data());
    }
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("##logboy_system", ScopedCurrentSystem))
    {
        if (ImGui::Selectable("All systems", MSystemFilter == 0))
        {
            MSystemFilter = 0;
        }
        const std::uint32_t ScopedRegistry = MSystem.RegistryCount();
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedRegistry; ++ScopedIndex)
        {
            const LogBoySystem::SystemName& ScopedEntry = MSystem.RegistryAt(ScopedIndex);
            if (!ScopedEntry.ready.load(std::memory_order_acquire))
            {
                continue;
            }
            const std::uint64_t ScopedHash = ScopedEntry.hash.load(std::memory_order_relaxed);
            ImGui::PushID(static_cast<int>(ScopedIndex));
            if (ImGui::Selectable(ScopedEntry.name, MSystemFilter == ScopedHash))
            {
                MSystemFilter = ScopedHash;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%016llx", static_cast<unsigned long long>(ScopedHash));
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset filters"))
    {
        MSystemFilter = 0;
        MSearch[0] = '\0';
        MQueryKind = QueryKind::None;
        for (std::uint32_t ScopedSeverity = 0; ScopedSeverity < kLogBoySeverityCount; ++ScopedSeverity)
        {
            MLevelEnabled[ScopedSeverity] = true;
        }
    }
    ImGui::Separator();
}

void LogBoyPanel::DrawTable()
{
    if (MRowCount == 0)
    {
        ImGui::TextDisabled("%s", MViewCount == 0 ? "No records yet." : "No record matches the active filters.");
        return;
    }

    const ImGuiTableFlags ScopedFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    if (ImGui::BeginTable("##logboy_rows", MShowFrame ? 6 : 5, ScopedFlags))
    {
        ImGui::TableSetupColumn("t +s", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("sev", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        if (MShowFrame)
        {
            ImGui::TableSetupColumn("frame", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        }
        ImGui::TableSetupColumn("system", ImGuiTableColumnFlags_WidthFixed, 210.0f);
        ImGui::TableSetupColumn("thread", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (std::uint32_t ScopedRow = 0; ScopedRow < MRowCount; ++ScopedRow)
        {
            const LogBoyRecord& ScopedRecord = ViewAt(MRowIndex[ScopedRow]);
            const auto ScopedLevel = static_cast<LogBoySeverity>(ScopedRecord.severity);
            const double ScopedSeconds =
                MFirstTimestamp == 0
                    ? 0.0
                    : static_cast<double>(ScopedRecord.timestampNanoseconds - MFirstTimestamp) / 1.0e9;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%8.3f", ScopedSeconds);

            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(ScopedLevel));
            ImGui::TextUnformatted(LogBoySeverityName(ScopedLevel));
            ImGui::PopStyleColor();

            if (MShowFrame)
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%u", ScopedRecord.frame);
            }

            ImGui::TableNextColumn();
            const std::string_view ScopedName = MSystem.SystemNameFor(ScopedRecord.systemHash);
            ImGui::Text("%08llx", static_cast<unsigned long long>(ScopedRecord.systemHash & 0xFFFFFFFFull));
            ImGui::SameLine();
            ImGui::TextDisabled("%.*s", static_cast<int>(ScopedName.size()),
                                ScopedName.empty() ? "?" : ScopedName.data());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::IsItemClicked(ImGuiMouseButton_Middle))
            {
                MSystemFilter = ScopedRecord.systemHash;
            }
            if (ImGui::BeginPopupContextItem("##logboy_filter"))
            {
                if (ImGui::MenuItem("Filter this system"))
                {
                    MSystemFilter = ScopedRecord.systemHash;
                }
                if (ImGui::MenuItem("Clear system filter"))
                {
                    MSystemFilter = 0;
                }
                ImGui::EndPopup();
            }

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%08x", ScopedRecord.threadNameHash);

            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(ScopedLevel));
            ImGui::TextUnformatted(ScopedRecord.text, ScopedRecord.text + ScopedRecord.textLength);
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    if (MAutoScroll)
    {
        ImGui::SetScrollHereY(1.0f);
    }
}

void LogBoyPanel::Draw(bool* InOpen)
{
    if (!BeginEditorPanel("LogBoy", InOpen))
    {
        EndEditorPanel();
        return;
    }

    const double ScopedNow = ImGui::GetTime();
    if (MRateWindowStart == 0.0)
    {
        MRateWindowStart = ScopedNow;
        MLastRateTime = ScopedNow;
        MRateWindowBase = MSeenTotal;
    }
    DrainRing();
    RefreshRows();
    if (ScopedNow - MLastRateTime >= kRateWindowSeconds)
    {
        const double ScopedSeconds = ScopedNow - MLastRateTime;
        MRate = ScopedSeconds > 0.0 ? static_cast<double>(MSeenTotal - MRateWindowBase) / ScopedSeconds : 0.0;
        MRateWindowBase = MSeenTotal;
        MLastRateTime = ScopedNow;
    }

    DrawToolbar();
    ImGui::BeginChild("##logboy_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    DrawTable();
    ImGui::EndChild();
    EndEditorPanel();
}
}
