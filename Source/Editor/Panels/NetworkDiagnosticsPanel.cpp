// FemboyEngine2D - network diagnostics panel implementation.
#include "Editor/Panels/NetworkDiagnosticsPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Network/Diagnostics.hpp"

#include "imgui.h"

namespace Fby::Editor
{
namespace
{
const char* RoleName(Fby::Net::NetRole InRole)
{
    return Fby::Net::NetRoleName(InRole);
}
}

void NetworkDiagnosticsPanel::Draw(bool* InOpen)
{
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Network Diagnostics", InOpen))
    {
        EndEditorPanel();
        return;
    }

    if (MSession == nullptr)
    {
        ImGui::TextWrapped("No session is bound. Start a Hosted or Client session and pass it to "
                           "EditorSystem::SetNetworkSession().");
        EndEditorPanel();
        return;
    }

    const Fby::Net::NetDiagnosticsSnapshot ScopedSnapshot = Fby::Net::CaptureNetDiagnostics(*MSession);
    ImGui::Text("role: %s   status: %s", RoleName(ScopedSnapshot.Role),
                Fby::Net::NetSessionStatusName(ScopedSnapshot.Status));
    ImGui::Text("local player: %u / %u", static_cast<unsigned>(ScopedSnapshot.LocalPlayer),
                static_cast<unsigned>(ScopedSnapshot.PlayerCount));
    ImGui::Text("tick: %llu (confirmed %llu)", static_cast<unsigned long long>(ScopedSnapshot.LocalTick),
                static_cast<unsigned long long>(ScopedSnapshot.ConfirmedTick));
    ImGui::Text("rtt: %u ms   clock offset: %lld ms", ScopedSnapshot.RttMs,
                static_cast<long long>(ScopedSnapshot.ClockOffsetMs));
    if (ScopedSnapshot.Remote.Port != 0)
    {
        ImGui::Text("remote: %u.%u.%u.%u:%u", static_cast<unsigned>((ScopedSnapshot.Remote.Host >> 24) & 0xFFu),
                    static_cast<unsigned>((ScopedSnapshot.Remote.Host >> 16) & 0xFFu),
                    static_cast<unsigned>((ScopedSnapshot.Remote.Host >> 8) & 0xFFu),
                    static_cast<unsigned>(ScopedSnapshot.Remote.Host & 0xFFu),
                    static_cast<unsigned>(ScopedSnapshot.Remote.Port));
    }
    ImGui::Separator();

    if (ImGui::BeginTable("##slots", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("id");
        ImGui::TableSetupColumn("name");
        ImGui::TableSetupColumn("state");
        ImGui::TableSetupColumn("join tick");
        ImGui::TableHeadersRow();
        for (const Fby::Net::NetPlayerSlot& ScopedSlot : ScopedSnapshot.Slots)
        {
            if (!ScopedSlot.Used)
            {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", static_cast<unsigned>(ScopedSlot.Id));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(ScopedSlot.Name);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", ScopedSlot.State == Fby::Net::NetSlotState::Ready ? "ready" : "joined");
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%llu", static_cast<unsigned long long>(ScopedSlot.JoinTick));
        }
        ImGui::EndTable();
    }

    EndEditorPanel();
}
}
