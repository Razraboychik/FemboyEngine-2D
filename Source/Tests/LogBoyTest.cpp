// FemboyEngine2D - LogBoy diagnostics logger tests.
#include "Editor/Panels/LogBoyPanel.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

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

void TestHashing()
{
    using Fby::Editor::LogBoySystem;
    // Published FNV-1a 64 vectors: the offset basis and "a".
    FBY_CHECK(LogBoySystem::HashSystem("") == 14695981039346656037ull);
    FBY_CHECK(LogBoySystem::HashSystem("a") == 0xaf63dc4c8601ec8cull);
    FBY_CHECK(LogBoySystem::HashSystem("physics") != 0);
    FBY_CHECK(LogBoySystem::HashSystem("physics") != LogBoySystem::HashSystem("render"));
    FBY_CHECK(LogBoySystem::HashSystem("net") != LogBoySystem::HashSystem("net "));
}

void TestRingOrderAndOverflow()
{
    using Fby::Editor::LogBoyRecord;
    using Fby::Editor::LogBoyRing;

    static LogBoyRing ScopedRing;
    LogBoyRecord ScopedRecord;
    ScopedRecord.sequence = 7;
    FBY_CHECK(ScopedRing.Push(ScopedRecord));
    LogBoyRecord ScopedPopped;
    FBY_CHECK(ScopedRing.Pop(&ScopedPopped));
    FBY_CHECK(ScopedPopped.sequence == 7);
    FBY_CHECK(!ScopedRing.Pop(&ScopedPopped));

    static LogBoyRing ScopedFull;
    const std::uint32_t ScopedCapacity = LogBoyRing::kCapacity;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCapacity + 17; ++ScopedIndex)
    {
        LogBoyRecord ScopedItem;
        ScopedItem.sequence = ScopedIndex;
        (void)ScopedFull.Push(ScopedItem);
    }
    FBY_CHECK(ScopedFull.Available() == ScopedCapacity);
    FBY_CHECK(ScopedFull.Dropped() == 17);
    // The ring drops the new record, never the oldest: a late burst cannot
    FBY_CHECK(ScopedFull.Pop(&ScopedPopped));
    FBY_CHECK(ScopedPopped.sequence == 0);
    ScopedFull.Clear();
    FBY_CHECK(ScopedFull.Available() == 0);
}

void TestSystemFormattingAndRegistry()
{
    using Fby::Editor::kLogBoyTextBytes;
    using Fby::Editor::LogBoyRecord;
    using Fby::Editor::LogBoySeverity;
    using Fby::Editor::LogBoySystem;

    static LogBoySystem ScopedSystem;
    const std::uint64_t ScopedHash = LogBoySystem::HashSystem("physics");
    ScopedSystem.Log(LogBoySeverity::Info, "physics", "step {} took {:.1f} us", 42, 12.5);

    std::array<LogBoyRecord, 8> ScopedBatch{};
    FBY_CHECK(ScopedSystem.Drain(ScopedBatch) == 1);
    FBY_CHECK(ScopedSystem.Total() == 1);
    FBY_CHECK(ScopedBatch[0].systemHash == ScopedHash);
    FBY_CHECK(ScopedBatch[0].severity == static_cast<std::uint8_t>(LogBoySeverity::Info));
    FBY_CHECK(std::strcmp(ScopedBatch[0].text, "step 42 took 12.5 us") == 0);
    FBY_CHECK(ScopedBatch[0].textLength == std::strlen("step 42 took 12.5 us"));
    FBY_CHECK(ScopedSystem.SystemNameFor(ScopedHash) == "physics");
    FBY_CHECK(ScopedSystem.RegistryCount() == 1);
    FBY_CHECK(ScopedSystem.SystemNameFor(0x1234u).empty());

    // Truncation: the head is kept and the last three bytes mark the cut.
    const std::string ScopedLong(300, 'a');
    ScopedSystem.PushText(LogBoySeverity::Critical, ScopedHash, ScopedLong);
    std::array<LogBoyRecord, 8> ScopedTruncated{};
    FBY_CHECK(ScopedSystem.Drain(ScopedTruncated) == 1);
    FBY_CHECK(ScopedTruncated[0].textLength == kLogBoyTextBytes - 1);
    FBY_CHECK(std::strcmp(ScopedTruncated[0].text + kLogBoyTextBytes - 4, "...") == 0);

    // The engine Log() bridge maps levels onto the LogBoy scale.
    FBY_CHECK(Fby::Editor::LogBoySeverityFromLevel(Fby::LogLevel::Debug) == LogBoySeverity::Trace);
    FBY_CHECK(Fby::Editor::LogBoySeverityFromLevel(Fby::LogLevel::Warn) == LogBoySeverity::Warning);
    FBY_CHECK(Fby::Editor::LogBoySeverityFromLevel(Fby::LogLevel::Fatal) == LogBoySeverity::Critical);
}

void TestMultiProducer()
{
    using Fby::Editor::LogBoyRecord;
    using Fby::Editor::LogBoySeverity;
    using Fby::Editor::LogBoySystem;

    static LogBoySystem ScopedSystem;
    constexpr std::uint32_t kThreads = 4;
    constexpr std::uint32_t kPerThread = 250;
    std::array<std::thread, kThreads> ScopedThreads;
    for (std::uint32_t ScopedThread = 0; ScopedThread < kThreads; ++ScopedThread)
    {
        ScopedThreads[ScopedThread] = std::thread(
            [ScopedThread]()
            {
                for (std::uint32_t ScopedIndex = 0; ScopedIndex < kPerThread; ++ScopedIndex)
                {
                    ScopedSystem.Log(LogBoySeverity::Trace, "workers", "thread {} message {}", ScopedThread,
                                     ScopedIndex);
                }
            });
    }
    for (std::thread& ScopedThread : ScopedThreads)
    {
        ScopedThread.join();
    }

    FBY_CHECK(ScopedSystem.Total() == kThreads * kPerThread);
    FBY_CHECK(ScopedSystem.Pending() == kThreads * kPerThread);
    std::vector<LogBoyRecord> ScopedRecords(kThreads * kPerThread);
    FBY_CHECK(ScopedSystem.Drain(ScopedRecords) == ScopedRecords.size());
    for (const LogBoyRecord& ScopedRecord : ScopedRecords)
    {
        FBY_CHECK(std::strncmp(ScopedRecord.text, "thread ", 7) == 0);
        FBY_CHECK(ScopedRecord.textLength == std::strlen(ScopedRecord.text));
        FBY_CHECK(ScopedRecord.systemHash == LogBoySystem::HashSystem("workers"));
    }
}
}

int main()
{
    TestHashing();
    TestRingOrderAndOverflow();
    TestSystemFormattingAndRegistry();
    TestMultiProducer();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "logboy: %u failure(s)\n", GFailures);
        return 1;
    }
    std::printf("logboy: all checks passed\n");
    return 0;
}
