// FemboyEngine2D - network diagnostics panel implementation.
#include "Editor/Panels/NetworkDiagnosticsPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Network/Diagnostics.hpp"
#include "Engine/Network/NetProtocol.hpp"
#include "Engine/Core/CryptoAead.hpp"

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
    ImGui::SetNextWindowSize(ImVec2(520.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Network Diagnostics", InOpen))
    {
        EndEditorPanel();
        return;
    }

    ImGui::SeparatorText("WebSocket probe");
    ImGui::SetNextItemWidth(360.0f);
    (void)ImGui::InputText("URL", MWebSocketUrl.data(), MWebSocketUrl.size());
    ImGui::SameLine();
    if (ImGui::Button("Connect"))
    {
        MWebSocket.Close();
        std::string ScopedError;
        if (MWebSocket.Connect(MWebSocketUrl.data(), &ScopedError, 500))
        {
            MWebSocketLog += "connected\n";
        }
        else
        {
            MWebSocketLog += "connect failed: " + ScopedError + "\n";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close"))
    {
        MWebSocket.Close();
        MWebSocketLog += "closed\n";
    }
    if (MWebSocket.IsOpen())
    {
        std::string ScopedError;
        (void)MWebSocket.Poll(&ScopedError);
        Fby::Net::WebSocketMessage ScopedMessage;
        while (MWebSocket.Receive(&ScopedMessage, &ScopedError))
        {
            MWebSocketLog += ScopedMessage.Data + "\n";
        }
        if (!ScopedError.empty())
        {
            MWebSocketLog += "websocket: " + ScopedError + "\n";
        }
        ImGui::SetNextItemWidth(360.0f);
        (void)ImGui::InputText("Message", MWebSocketMessage.data(), MWebSocketMessage.size());
        ImGui::SameLine();
        if (ImGui::Button("Send") && MWebSocketMessage[0] != '\0')
        {
            if (!MWebSocket.SendText(MWebSocketMessage.data(), &ScopedError))
            {
                MWebSocketLog += "send failed: " + ScopedError + "\n";
            }
        }
    }
    ImGui::TextDisabled("state %d   sent %llu   received %llu", static_cast<int>(MWebSocket.State()),
                        static_cast<unsigned long long>(MWebSocket.Stats().bytesSent),
                        static_cast<unsigned long long>(MWebSocket.Stats().bytesReceived));
    if (MWebSocketLog.size() > 32768)
    {
        MWebSocketLog.erase(0, MWebSocketLog.size() - 32768);
    }
    if (!MWebSocketLog.empty())
    {
        if (ImGui::BeginChild("##ws_log", ImVec2(0.0f, 100.0f), ImGuiChildFlags_Borders))
        {
            ImGui::TextUnformatted(MWebSocketLog.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::SeparatorText("AEAD self-test");
    if (ImGui::Button("Round-trip XChaCha20-Poly1305"))
    {
        std::array<std::byte, Fby::Crypto::kXChaChaKeyBytes> ScopedKey{};
        std::array<std::byte, Fby::Crypto::kXChaChaNonceBytes> ScopedNonce{};
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedKey.size(); ++ScopedIndex)
        {
            ScopedKey[ScopedIndex] = static_cast<std::byte>(ScopedIndex + 1u);
        }
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedNonce.size(); ++ScopedIndex)
        {
            ScopedNonce[ScopedIndex] = static_cast<std::byte>(0xA0u + ScopedIndex);
        }
        const std::string_view ScopedPlain = "FemboyEngine2D AEAD self-test";
        std::array<std::byte, 64> ScopedCipher{};
        std::array<std::byte, Fby::Crypto::kXChaChaTagBytes> ScopedTag{};
        std::array<std::byte, 64> ScopedRoundTrip{};
        Fby::Crypto::XChaCha20Poly1305Encrypt(
            ScopedKey, ScopedNonce, {}, std::as_bytes(std::span(ScopedPlain.data(), ScopedPlain.size())), ScopedCipher,
            ScopedTag);
        const bool ScopedOk = Fby::Crypto::XChaCha20Poly1305Decrypt(
            ScopedKey, ScopedNonce, {}, std::span<const std::byte>(ScopedCipher.data(), ScopedPlain.size()), ScopedTag,
            std::span<std::byte>(ScopedRoundTrip.data(), ScopedPlain.size()));
        MWebSocketLog += ScopedOk && std::memcmp(ScopedRoundTrip.data(), ScopedPlain.data(), ScopedPlain.size()) == 0
                             ? "AEAD round-trip ok\n"
                             : "AEAD round-trip failed\n";
    }
    ImGui::SeparatorText("Input packet codec");
    if (ImGui::Button("Round-trip input packet"))
    {
        Fby::Net::InputPacket ScopedPacket;
        ScopedPacket.Player = Fby::Net::NetPlayerId{1};
        ScopedPacket.PacketSequence = 7;
        ScopedPacket.FirstSequence = 7;
        ScopedPacket.Count = 1;
        ScopedPacket.Frames[0].tick = 7;
        ScopedPacket.Frames[0].moveX = 16384;
        ScopedPacket.Frames[0].buttons = 1;
        ScopedPacket.Frames[0].sequence = 7;
        ScopedPacket.Frames[0].player = 1;
        std::array<std::byte, Fby::Net::kInputPacketBytes> ScopedBytes{};
        const std::size_t ScopedEncoded = Fby::Net::EncodeInputPacket(ScopedPacket, ScopedBytes);
        Fby::Net::InputPacket ScopedDecoded;
        const bool ScopedDecodedOk = ScopedEncoded != 0 && Fby::Net::DecodeInputPacket(
            std::span<const std::byte>(ScopedBytes.data(), ScopedEncoded), &ScopedDecoded);
        MWebSocketLog += ScopedDecodedOk && ScopedDecoded.Player.value == 1 && ScopedDecoded.Count == 1
                             ? "input packet round-trip ok\n"
                             : "input packet round-trip failed\n";
    }
    ImGui::Separator();

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
