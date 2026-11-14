// FemboyEngine2D - minimal RFC 6455 WebSocket client (ws:// only).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Net
{

enum class WebSocketOpcode : std::uint8_t
{
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

enum class WebSocketState : std::uint8_t
{
    Closed = 0,
    Open,
    Closing,
    Error,
};

struct WebSocketMessage
{
    WebSocketOpcode Opcode = WebSocketOpcode::Text;
    std::string Data;
};

struct WebSocketStats
{
    std::uint64_t bytesSent = 0;
    std::uint64_t bytesReceived = 0;
    std::uint64_t messagesSent = 0;
    std::uint64_t messagesReceived = 0;
    std::uint64_t pings = 0;
};

// Calculates Sec-WebSocket-Accept for a Sec-WebSocket-Key. Public so a local
// test server can validate the same handshake without linking private code.
[[nodiscard]] bool MakeWebSocketAccept(std::string_view InKey, std::string* OutAccept);

// Blocking connect + non-blocking frame pump. Plain ws:// is supported; wss://
// is rejected until a TLS transport is added. The socket stays non-blocking
// after Connect() so the caller can Poll() from the frame loop.
class WebSocketClient
{
public:
    static constexpr std::size_t kMaxMessageBytes = 1u << 20;

    WebSocketClient() = default;
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;
    WebSocketClient(WebSocketClient&& InOther) noexcept;
    WebSocketClient& operator=(WebSocketClient&& InOther) noexcept;

    [[nodiscard]] bool Connect(std::string_view InUrl, std::string* InError = nullptr,
                               std::uint32_t InTimeoutMs = 5000) noexcept;
    void Close() noexcept;

    [[nodiscard]] bool SendText(std::string_view InText, std::string* InError = nullptr) noexcept;
    [[nodiscard]] bool SendBinary(std::span<const std::byte> InData, std::string* InError = nullptr) noexcept;
    // Drains bytes from the socket and advances the frame parser.
    [[nodiscard]] bool Poll(std::string* InError = nullptr) noexcept;
    // Returns false when no complete message is queued yet.
    [[nodiscard]] bool Receive(WebSocketMessage* OutMessage, std::string* InError = nullptr) noexcept;

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return MState == WebSocketState::Open;
    }
    [[nodiscard]] WebSocketState State() const noexcept
    {
        return MState;
    }
    [[nodiscard]] const std::string& LastError() const noexcept
    {
        return MLastError;
    }
    [[nodiscard]] const WebSocketStats& Stats() const noexcept
    {
        return MStats;
    }

private:
    static constexpr std::uintptr_t kInvalidHandle = static_cast<std::uintptr_t>(-1);

    [[nodiscard]] bool SendFrame(WebSocketOpcode InOpcode, std::span<const std::byte> InPayload,
                                 std::string* InError) noexcept;
    [[nodiscard]] bool SendAll(std::span<const std::byte> InData, std::string* InError) noexcept;
    [[nodiscard]] bool ParseFrames(std::string* InError) noexcept;
    [[nodiscard]] bool Fail(std::string InMessage, std::string* InError) noexcept;
    void Reset() noexcept;

    std::uintptr_t MHandle = kInvalidHandle;
    WebSocketState MState = WebSocketState::Closed;
    std::string MLastError;
    std::string MBuffer;
    std::vector<WebSocketMessage> MQueue;
    WebSocketOpcode MFragmentOpcode = WebSocketOpcode::Text;
    std::string MFragmentData;
    bool MFragmentActive = false;
    WebSocketStats MStats{};
};

}
