// FemboyEngine2D - Chrome trace export for the frame profiler.
#pragma once

#include "Engine/Core/Profiler.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace Fby::Core
{

// Escapes the characters JSON requires inside a string. Names come from code, but
// a plugin can register one, so the export never trusts them.
[[nodiscard]] inline std::string JsonEscape(std::string_view InValue)
{
    std::string ScopedResult;
    ScopedResult.reserve(InValue.size());
    for (const char ScopedCharacter : InValue)
    {
        switch (ScopedCharacter)
        {
        case '"':
            ScopedResult += "\\\"";
            break;
        case '\\':
            ScopedResult += "\\\\";
            break;
        case '\n':
            ScopedResult += "\\n";
            break;
        case '\r':
            ScopedResult += "\\r";
            break;
        case '\t':
            ScopedResult += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ScopedCharacter) < 0x20 ||
                static_cast<unsigned char>(ScopedCharacter) == 0x7F)
            {
                char ScopedEscape[8] = {};
                std::snprintf(ScopedEscape, sizeof(ScopedEscape), "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(ScopedCharacter)));
                ScopedResult += ScopedEscape;
            }
            else
            {
                ScopedResult += ScopedCharacter;
            }
            break;
        }
    }
    return ScopedResult;
}

// Serializes the retained samples as Chrome Trace Event JSON. Every sample is a
// complete ("X") event with a microsecond start and duration, so the trace opens
// in chrome://tracing and in Perfetto.
[[nodiscard]] inline std::string ToChromeTraceJson(const Profiler& InProfiler,
                                                   std::span<const std::string_view> InCategoryNames,
                                                   std::uint32_t InProcessId = 1,
                                                   std::uint32_t InThreadId = 0)
{
    std::string ScopedJson = "{\"traceEvents\":[";
    bool ScopedFirst = true;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InProfiler.SampleCount(); ++ScopedIndex)
    {
        const ProfileSample ScopedSample = InProfiler.SampleAt(ScopedIndex);
        if (ScopedSample.Category >= InCategoryNames.size() || InCategoryNames[ScopedSample.Category].empty())
        {
            continue;
        }
        const double ScopedStartMicroseconds = static_cast<double>(ScopedSample.StartNanoseconds) / 1000.0;
        const double ScopedDurationMicroseconds = static_cast<double>(ScopedSample.DurationNanoseconds) / 1000.0;
        char ScopedBuffer[160];
        const int ScopedWritten = std::snprintf(ScopedBuffer, sizeof(ScopedBuffer),
                                                "%s{\"name\":\"%s\",\"cat\":\"frame\",\"ph\":\"X\","
                                                "\"ts\":%.3f,\"dur\":%.3f,\"pid\":%u,\"tid\":%u}",
                                                ScopedFirst ? "" : ",",
                                                JsonEscape(InCategoryNames[ScopedSample.Category]).c_str(),
                                                ScopedStartMicroseconds, ScopedDurationMicroseconds, InProcessId,
                                                InThreadId);
        if (ScopedWritten > 0)
        {
            const std::size_t ScopedLength =
                std::min<std::size_t>(static_cast<std::size_t>(ScopedWritten), sizeof(ScopedBuffer) - 1u);
            ScopedJson.append(ScopedBuffer, ScopedLength);
            ScopedFirst = false;
        }
    }
    ScopedJson += "]}";
    return ScopedJson;
}

}
