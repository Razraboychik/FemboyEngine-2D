// FemboyEngine2D - LogBoy, the data-oriented diagnostics logger.
#pragma once

#include "Engine/Core/JobSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Time.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace Fby::Editor
{

inline constexpr std::uint32_t kLogBoyTextBytes = 192;
inline constexpr std::uint32_t kLogBoyCapacity = 4096;
inline constexpr std::uint32_t kLogBoyMaxSystems = 64;
inline constexpr std::uint32_t kLogBoySystemNameBytes = 32;
inline constexpr std::uint32_t kLogBoyVisibleRecords = 768;
inline constexpr std::uint32_t kLogBoyClipboardBytes = 24 * 1024;

static_assert((kLogBoyCapacity & (kLogBoyCapacity - 1)) == 0, "the SPSC ring relies on a power-of-two capacity");

enum class LogBoySeverity : std::uint8_t
{
    Trace = 0,
    Info,
    Warning,
    Critical,
    Desync,
    Count,
};

inline constexpr std::uint32_t kLogBoySeverityCount = static_cast<std::uint32_t>(LogBoySeverity::Count);

// Packed 0xAABBGGRR, the same layout ImGui::ColorConvertU32ToFloat4 expects.
[[nodiscard]] inline std::uint32_t LogBoySeverityColor(LogBoySeverity InSeverity) noexcept
{
    switch (InSeverity)
    {
        case LogBoySeverity::Trace:
            return 0xFF998E8Eu;
        case LogBoySeverity::Info:
            return 0xFFFFB662u;
        case LogBoySeverity::Warning:
            return 0xFF3DB3F2u;
        case LogBoySeverity::Critical:
            return 0xFF7A5CFFu;
        case LogBoySeverity::Desync:
            return 0xFFFF7DC7u;
        case LogBoySeverity::Count:
            break;
    }
    return 0xFFFFFFFFu;
}

[[nodiscard]] inline const char* LogBoySeverityName(LogBoySeverity InSeverity) noexcept
{
    switch (InSeverity)
    {
        case LogBoySeverity::Trace:
            return "Trace";
        case LogBoySeverity::Info:
            return "Info";
        case LogBoySeverity::Warning:
            return "Warning";
        case LogBoySeverity::Critical:
            return "Critical";
        case LogBoySeverity::Desync:
            return "Desync";
        case LogBoySeverity::Count:
            break;
    }
    return "?";
}

// Debug folds into Trace, Error/Fatal fold into Critical. Desync is only ever
[[nodiscard]] inline LogBoySeverity LogBoySeverityFromLevel(LogLevel InLevel) noexcept
{
    switch (InLevel)
    {
        case LogLevel::Trace:
        case LogLevel::Debug:
            return LogBoySeverity::Trace;
        case LogLevel::Info:
            return LogBoySeverity::Info;
        case LogLevel::Warn:
            return LogBoySeverity::Warning;
        case LogLevel::Error:
        case LogLevel::Fatal:
            return LogBoySeverity::Critical;
    }
    return LogBoySeverity::Info;
}

struct LogBoyRecord
{
    std::uint64_t sequence = 0;
    std::uint64_t timestampNanoseconds = 0;
    std::uint64_t systemHash = 0;
    std::uint32_t threadNameHash = 0;
    std::uint32_t frame = 0;
    std::uint16_t textLength = 0;
    std::uint8_t severity = 0;
    std::uint8_t reserved = 0;
    char text[kLogBoyTextBytes] = {};
};

static_assert(std::is_trivially_copyable_v<LogBoyRecord>);
static_assert(std::is_standard_layout_v<LogBoyRecord>);

class LogBoyRing
{
public:
    static constexpr std::uint32_t kCapacity = kLogBoyCapacity;

    LogBoyRing() = default;
    LogBoyRing(const LogBoyRing&) = delete;
    LogBoyRing& operator=(const LogBoyRing&) = delete;

    [[nodiscard]] bool Push(const LogBoyRecord& InRecord) noexcept
    {
        const std::uint32_t ScopedHead = MHead.load(std::memory_order_relaxed);
        const std::uint32_t ScopedTail = MTail.load(std::memory_order_acquire);
        if (ScopedHead - ScopedTail >= kCapacity)
        {
            MDropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        MRecords[ScopedHead & (kCapacity - 1)] = InRecord;
        MHead.store(ScopedHead + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool Pop(LogBoyRecord* OutRecord) noexcept
    {
        if (OutRecord == nullptr)
        {
            return false;
        }
        const std::uint32_t ScopedTail = MTail.load(std::memory_order_relaxed);
        const std::uint32_t ScopedHead = MHead.load(std::memory_order_acquire);
        if (ScopedTail == ScopedHead)
        {
            return false;
        }
        *OutRecord = MRecords[ScopedTail & (kCapacity - 1)];
        MTail.store(ScopedTail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::uint32_t Available() const noexcept
    {
        const std::uint32_t ScopedHead = MHead.load(std::memory_order_acquire);
        const std::uint32_t ScopedTail = MTail.load(std::memory_order_acquire);
        return ScopedHead - ScopedTail;
    }
    [[nodiscard]] std::uint64_t Dropped() const noexcept
    {
        return MDropped.load(std::memory_order_relaxed);
    }
    void Clear() noexcept
    {
        LogBoyRecord ScopedRecord{};
        while (Pop(&ScopedRecord))
        {
        }
    }

private:
    alignas(64) std::atomic<std::uint32_t> MHead{0};
    alignas(64) std::atomic<std::uint32_t> MTail{0};
    alignas(64) std::atomic<std::uint64_t> MDropped{0};
    alignas(64) std::array<LogBoyRecord, kCapacity> MRecords{};
};

class LogBoySystem
{
public:
    struct SystemName
    {
        std::atomic<std::uint64_t> hash{0};
        std::atomic<bool> ready{false};
        char name[kLogBoySystemNameBytes] = {};
    };

    LogBoySystem() = default;
    LogBoySystem(const LogBoySystem&) = delete;
    LogBoySystem& operator=(const LogBoySystem&) = delete;

    [[nodiscard]] static constexpr std::uint64_t HashSystem(std::string_view InName) noexcept
    {
        // FNV-1a 64 with the published offset basis and prime, so a tool in any
        std::uint64_t ScopedHash = 14695981039346656037ull;
        for (const char ScopedCharacter : InName)
        {
            ScopedHash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ScopedCharacter));
            ScopedHash *= 1099511628211ull;
        }
        return ScopedHash;
    }

    template <typename... Args>
    void Log(LogBoySeverity InSeverity, std::string_view InSystem, std::format_string<Args...> InFormat,
             Args&&... InArgs)
    {
        const std::uint64_t ScopedHash = HashSystem(InSystem);
        RegisterSystem(ScopedHash, InSystem);
        PushFormatted(InSeverity, ScopedHash, InFormat, std::forward<Args>(InArgs)...);
    }

    template <typename... Args>
    void Log(LogBoySeverity InSeverity, std::uint64_t InSystemHash, std::format_string<Args...> InFormat,
             Args&&... InArgs)
    {
        PushFormatted(InSeverity, InSystemHash, InFormat, std::forward<Args>(InArgs)...);
    }

    // Bridge for the legacy Fby::Log() observer: the message is already
    void PushText(LogBoySeverity InSeverity, std::uint64_t InSystemHash, std::string_view InMessage) noexcept
    {
        LogBoyRecord ScopedRecord = MakeRecord(InSeverity, InSystemHash);
        const bool ScopedTruncated = InMessage.size() > kLogBoyTextBytes - 1;
        const std::size_t ScopedLength = std::min<std::size_t>(
            InMessage.size(), ScopedTruncated ? kLogBoyTextBytes - 4 : kLogBoyTextBytes - 1);
        if (ScopedLength > 0)
        {
            std::memcpy(ScopedRecord.text, InMessage.data(), ScopedLength);
        }
        if (ScopedTruncated)
        {
            std::memcpy(ScopedRecord.text + kLogBoyTextBytes - 4, "...", 3);
            ScopedRecord.text[kLogBoyTextBytes - 1] = '\0';
            ScopedRecord.textLength = static_cast<std::uint16_t>(kLogBoyTextBytes - 1);
        }
        else
        {
            ScopedRecord.text[ScopedLength] = '\0';
            ScopedRecord.textLength = static_cast<std::uint16_t>(ScopedLength);
        }
        Publish(ScopedRecord);
    }

    [[nodiscard]] std::uint32_t Drain(std::span<LogBoyRecord> OutRecords) noexcept
    {
        std::uint32_t ScopedCount = 0;
        while (ScopedCount < OutRecords.size() && MRing.Pop(&OutRecords[ScopedCount]))
        {
            ++ScopedCount;
        }
        return ScopedCount;
    }

    void Clear() noexcept
    {
        MRing.Clear();
    }

    [[nodiscard]] std::uint32_t Pending() const noexcept
    {
        return MRing.Available();
    }
    [[nodiscard]] std::uint64_t Dropped() const noexcept
    {
        return MRing.Dropped();
    }
    [[nodiscard]] std::uint64_t Total() const noexcept
    {
        return MTotal.load(std::memory_order_relaxed);
    }
    void SetFrame(std::uint32_t InFrame) noexcept
    {
        MFrame.store(InFrame, std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t RegistryCount() const noexcept
    {
        return MRegistryCount.load(std::memory_order_acquire);
    }
    [[nodiscard]] const SystemName& RegistryAt(std::uint32_t InIndex) const noexcept
    {
        return MNames[InIndex];
    }
    // Linear scan over at most 64 entries; returns an empty view when unknown.
    [[nodiscard]] std::string_view SystemNameFor(std::uint64_t InHash) const noexcept
    {
        const std::uint32_t ScopedCount = MRegistryCount.load(std::memory_order_acquire);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            const SystemName& ScopedName = MNames[ScopedIndex];
            if (ScopedName.ready.load(std::memory_order_acquire) &&
                ScopedName.hash.load(std::memory_order_relaxed) == InHash)
            {
                return std::string_view(ScopedName.name);
            }
        }
        return {};
    }

private:
    [[nodiscard]] LogBoyRecord MakeRecord(LogBoySeverity InSeverity, std::uint64_t InSystemHash) noexcept
    {
        LogBoyRecord ScopedRecord;
        ScopedRecord.sequence = MNextSequence.fetch_add(1, std::memory_order_relaxed) + 1;
        ScopedRecord.timestampNanoseconds = ::Fby::Core::Clock::NowNanoseconds();
        ScopedRecord.systemHash = InSystemHash;
        ScopedRecord.threadNameHash =
            static_cast<std::uint32_t>(HashSystem(JobSystem::CurrentThreadName()) & 0xFFFFFFFFull);
        ScopedRecord.frame = MFrame.load(std::memory_order_relaxed);
        ScopedRecord.severity = static_cast<std::uint8_t>(InSeverity);
        MTotal.fetch_add(1, std::memory_order_relaxed);
        return ScopedRecord;
    }

    void Publish(LogBoyRecord& InRecord) noexcept
    {
        // Producers are serialized by a ticket gate, which keeps the ring
        const std::uint64_t ScopedTicket = MTicket.fetch_add(1, std::memory_order_acq_rel);
        while (MServing.load(std::memory_order_acquire) != ScopedTicket)
        {
            std::this_thread::yield();
        }
        (void)MRing.Push(InRecord);
        MServing.store(ScopedTicket + 1, std::memory_order_release);
    }

    void RegisterSystem(std::uint64_t InHash, std::string_view InName) noexcept
    {
        const std::uint32_t ScopedCount = MRegistryCount.load(std::memory_order_acquire);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            if (MNames[ScopedIndex].hash.load(std::memory_order_relaxed) == InHash)
            {
                return;
            }
        }
        std::uint32_t ScopedSlot = ScopedCount;
        if (ScopedSlot >= kLogBoyMaxSystems ||
            !MRegistryCount.compare_exchange_strong(ScopedSlot, ScopedSlot + 1, std::memory_order_acq_rel))
        {
            return;
        }
        SystemName& ScopedName = MNames[ScopedSlot];
        ScopedName.hash.store(InHash, std::memory_order_relaxed);
        const std::size_t ScopedLength = std::min<std::size_t>(InName.size(), kLogBoySystemNameBytes - 1);
        if (ScopedLength > 0)
        {
            std::memcpy(ScopedName.name, InName.data(), ScopedLength);
        }
        ScopedName.name[ScopedLength] = '\0';
        ScopedName.ready.store(true, std::memory_order_release);
    }
    template <typename... Args>
    void PushFormatted(LogBoySeverity InSeverity, std::uint64_t InSystemHash, std::format_string<Args...> InFormat,
                       Args&&... InArgs)
    {
        LogBoyRecord ScopedRecord = MakeRecord(InSeverity, InSystemHash);
        const auto ScopedResult =
            std::format_to_n(ScopedRecord.text, kLogBoyTextBytes - 1, InFormat, std::forward<Args>(InArgs)...);
        if (ScopedResult.size > kLogBoyTextBytes - 1)
        {
            // Keep the head, mark the truncation: a silent cut loses the
            std::memcpy(ScopedRecord.text + kLogBoyTextBytes - 4, "...", 3);
            ScopedRecord.text[kLogBoyTextBytes - 1] = '\0';
            ScopedRecord.textLength = static_cast<std::uint16_t>(kLogBoyTextBytes - 1);
        }
        else
        {
            ScopedRecord.text[ScopedResult.size] = '\0';
            ScopedRecord.textLength = static_cast<std::uint16_t>(ScopedResult.size);
        }
        Publish(ScopedRecord);
    }

    LogBoyRing MRing;
    std::atomic<std::uint64_t> MNextSequence{0};
    std::atomic<std::uint64_t> MTotal{0};
    std::atomic<std::uint64_t> MTicket{0};
    std::atomic<std::uint64_t> MServing{0};
    std::atomic<std::uint32_t> MRegistryCount{0};
    std::atomic<std::uint32_t> MFrame{0};
    std::array<SystemName, kLogBoyMaxSystems> MNames{};
};

// LogBoyPanel
class LogBoyPanel
{
public:
    LogBoyPanel() = default;
    LogBoyPanel(const LogBoyPanel&) = delete;
    LogBoyPanel& operator=(const LogBoyPanel&) = delete;

    void Initialize();
    void Shutdown();
    void Draw(bool* InOpen);

    [[nodiscard]] LogBoySystem& System() noexcept
    {
        return MSystem;
    }
    void SetFrame(std::uint32_t InFrame) noexcept
    {
        MSystem.SetFrame(InFrame);
    }
    [[nodiscard]] std::uint32_t VisibleCount() const noexcept
    {
        return MViewCount;
    }
    [[nodiscard]] std::uint64_t TotalSeen() const noexcept
    {
        return MSeenTotal;
    }
    [[nodiscard]] std::uint64_t DesyncCount() const noexcept
    {
        return MDesyncCount;
    }

    // Called by the Log() observer from any thread. Does not touch ImGui.
    void PushFromEngine(LogLevel InLevel, std::string_view InChannel, std::string_view InMessage) noexcept;

private:
    enum class QueryKind : std::uint8_t
    {
        None = 0,
        Text,
        SystemHash,
    };

    void DrainRing();
    void ParseQuery();
    void RefreshRows();
    [[nodiscard]] bool Passes(const LogBoyRecord& InRecord) const noexcept;
    void DrawToolbar();
    void DrawTable();
    [[nodiscard]] const LogBoyRecord& ViewAt(std::uint32_t InIndex) const noexcept;

    LogBoySystem MSystem;
    std::array<LogBoyRecord, kLogBoyVisibleRecords> MView{};
    std::array<std::uint16_t, kLogBoyVisibleRecords> MRowIndex{};
    std::array<bool, kLogBoySeverityCount> MLevelEnabled{true, true, true, true, true};
    std::uint32_t MViewHead = 0;
    std::uint32_t MViewCount = 0;
    std::uint32_t MRowCount = 0;
    std::uint32_t MHiddenCount = 0;
    std::uint64_t MSeenTotal = 0;
    std::uint64_t MDesyncCount = 0;
    std::uint64_t MSystemFilter = 0;
    std::uint64_t MFirstTimestamp = 0;
    double MRateWindowStart = 0.0;
    double MRate = 0.0;
    double MLastRateTime = 0.0;
    std::uint64_t MRateWindowBase = 0;
    char MSearch[128] = {};
    char MClipboard[kLogBoyClipboardBytes] = {};
    QueryKind MQueryKind = QueryKind::None;
    std::uint64_t MQueryHash = 0;
    std::array<char, 128> MQueryLower{};
    bool MPaused = false;
    bool MAutoScroll = true;
    bool MShowFrame = true;
    bool MInstalled = false;
};
}
