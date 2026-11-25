// FemboyEngine2D - network diagnostics panel implementation.
#include "Editor/Panels/NetworkDiagnosticsPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Network/Diagnostics.hpp"
#include "Engine/Network/NetProtocol.hpp"
#include "Engine/Core/CryptoAead.hpp"

#include <charconv>
#include <cstdlib>
#include <ctime>

#include "imgui.h"

namespace Fby::Editor
{
namespace
{
bool ParsePort(std::string_view InText, std::uint16_t* OutPort)
{
    if (OutPort == nullptr || InText.empty())
    {
        return false;
    }
    std::uint32_t ScopedPort = 0;
    const auto ScopedResult = std::from_chars(InText.data(), InText.data() + InText.size(), ScopedPort);
    if (ScopedResult.ec != std::errc{} || ScopedResult.ptr != InText.data() + InText.size() || ScopedPort > 65535u)
    {
        return false;
    }
    *OutPort = static_cast<std::uint16_t>(ScopedPort);
    return true;
}

const char* RoleName(Fby::Net::NetRole InRole)
{
    return Fby::Net::NetRoleName(InRole);
}

bool ParseKeyHex(std::string_view InText, std::array<std::byte, Fby::Crypto::kEddsaPublicBytes>& OutKey)
{
    if (InText.size() != OutKey.size() * 2)
    {
        return false;
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < OutKey.size(); ++ScopedIndex)
    {
        const auto ScopedNibble = [](char InCharacter) -> int
        {
            if (InCharacter >= '0' && InCharacter <= '9')
            {
                return InCharacter - '0';
            }
            if (InCharacter >= 'a' && InCharacter <= 'f')
            {
                return InCharacter - 'a' + 10;
            }
            if (InCharacter >= 'A' && InCharacter <= 'F')
            {
                return InCharacter - 'A' + 10;
            }
            return -1;
        };
        const int ScopedHigh = ScopedNibble(InText[ScopedIndex * 2]);
        const int ScopedLow = ScopedNibble(InText[ScopedIndex * 2 + 1]);
        if (ScopedHigh < 0 || ScopedLow < 0)
        {
            return false;
        }
        OutKey[ScopedIndex] = static_cast<std::byte>((ScopedHigh << 4) | ScopedLow);
    }
    return true;
}
}

NetworkDiagnosticsPanel::~NetworkDiagnosticsPanel()
{
    MLobby.Stop();
    MLobbyTransport.Close();
}

void NetworkDiagnosticsPanel::Draw(bool* InOpen)
{
    // Lobby traffic uses the same datagram transport as a game session; polling
    // while the panel is open keeps create/join/start responsive.
    MLobbyTransport.Tick(0.016f);
    MLobby.Tick(static_cast<std::uint64_t>(std::time(nullptr)));

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
    ImGui::SeparatorText("Lobby");
    ImGui::SetNextItemWidth(140.0f);
    (void)ImGui::InputText("Lobby host", MLobbyHost.data(), MLobbyHost.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    (void)ImGui::InputText("Port", MLobbyPort.data(), MLobbyPort.size());
    ImGui::SetNextItemWidth(360.0f);
    (void)ImGui::InputText("Lobby public key", MLobbyKey.data(), MLobbyKey.size());
    ImGui::SetNextItemWidth(140.0f);
    (void)ImGui::InputText("Player", MLobbyName.data(), MLobbyName.size());
    ImGui::SetNextItemWidth(140.0f);
    (void)ImGui::InputText("Room name", MLobbyRoomName.data(), MLobbyRoomName.size());
    if (!MLobbyStarted)
    {
        if (ImGui::Button("Start lobby client"))
        {
            std::string ScopedError;
            if (!MLobbyTransport.Opened() && !MLobbyTransport.Open(0, &ScopedError))
            {
                MLobbyStatus = "transport: " + ScopedError;
            }
            else
            {
                Fby::Net::NetAddress ScopedAddress;
                std::array<std::byte, Fby::Crypto::kEddsaPublicBytes> ScopedKey{};
                std::uint16_t ScopedPort = 0;
                if (!ParsePort(std::string_view(MLobbyPort.data()), &ScopedPort) ||
                    !Fby::Net::ParseAddress(MLobbyHost.data(), ScopedPort, &ScopedAddress) ||
                    !ParseKeyHex(MLobbyKey.data(), ScopedKey))
                {
                    MLobbyStatus = "invalid host, port or 32-byte hex key";
                }
                else
                {
                    Fby::Net::NetLobbyClientConfig ScopedConfig;
                    ScopedConfig.LobbyPublicKey = ScopedKey;
                    std::snprintf(ScopedConfig.PlayerName, sizeof(ScopedConfig.PlayerName), "%s", MLobbyName.data());
                    MLobbyStarted = MLobby.Start(Fby::Net::MakeTransportLink(&MLobbyTransport), ScopedAddress, ScopedConfig);
                    MLobbyStatus = MLobbyStarted ? "lobby client started" : "lobby client rejected the config";
                }
            }
        }
    }
    else
    {
        if (ImGui::Button("Stop lobby client"))
        {
            MLobby.Stop();
            MLobbyStarted = false;
            MLobbyStatus = "lobby client stopped";
        }
        ImGui::SameLine();
        if (ImGui::Button("Create room"))
        {
            if (!MLobby.CreateRoom(MLobbyRoomName.data()))
            {
                MLobbyStatus = std::string("create room failed: ") +
                               std::string(Fby::Net::LobbyErrorName(MLobby.Error()));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Start match"))
        {
            if (!MLobby.StartMatch())
            {
                MLobbyStatus = std::string("start match failed: ") +
                               std::string(Fby::Net::LobbyErrorName(MLobby.Error()));
            }
        }
        ImGui::SetNextItemWidth(100.0f);
        (void)ImGui::InputInt("Room id", &MLobbyRoomId);
        ImGui::SameLine();
        if (ImGui::Button("Join room"))
        {
            if (!MLobby.JoinRoom(static_cast<std::uint32_t>(std::max(MLobbyRoomId, 0))))
            {
                MLobbyStatus = std::string("join room failed: ") +
                               std::string(Fby::Net::LobbyErrorName(MLobby.Error()));
            }
        }
        ImGui::Text("ready %s  room %u  player %u  started %s  error %s", MLobby.Ready() ? "yes" : "no",
                    MLobby.RoomId(), static_cast<unsigned>(MLobby.PlayerId()),
                    MLobby.MatchStarted() ? "yes" : "no", Fby::Net::LobbyErrorName(MLobby.Error()));
        if (MLobby.Ready())
        {
            ImGui::TextDisabled("ticket signed; call ApplyToSessionConfig() before creating the session");
        }
    }
    if (!MLobbyStatus.empty())
    {
        ImGui::TextDisabled("%s", MLobbyStatus.c_str());
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
