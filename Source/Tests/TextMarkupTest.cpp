// FemboyEngine2D - rich text markup compiler test.
#include "Engine/Text/TextMarkup.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void CheckString(std::string_view InActual, std::string_view InExpected, const char* InMessage)
{
    if (InActual != InExpected)
    {
        std::fprintf(stderr, "FAIL: %s (actual '%.*s', expected '%.*s')\n", InMessage,
                     static_cast<int>(InActual.size()), InActual.data(), static_cast<int>(InExpected.size()),
                     InExpected.data());
        ++GFailures;
    }
}

void TestBasicStyles()
{
    using namespace Fby;

    auto ScopedBlock = std::make_unique<Text::TextStyledBlock>();
    constexpr std::string_view kMarkup = "Hello [b]bold[/b] and [color=#FF0000]red[/color].";
    Check(Text::CompileTextMarkup(kMarkup, ScopedBlock.get()) == Text::TextMarkupStatus::Ok, "markup compiles");
    CheckString(ScopedBlock->View(), "Hello bold and red.", "markup removes tags");
    Check(ScopedBlock->SpanCount == 5, "markup creates five runs");
    Check(ScopedBlock->StyleCount >= 3, "markup creates style entries");

    bool ScopedSawBold = false;
    bool ScopedSawRed = false;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedBlock->SpanCount; ++ScopedIndex)
    {
        const Text::TextSpan& ScopedSpan = ScopedBlock->Spans[ScopedIndex];
        Check(ScopedSpan.Begin < ScopedSpan.End, "span range is non-empty");
        const Text::TextStyle& ScopedStyle = ScopedBlock->Styles[ScopedSpan.StyleIndex];
        if ((ScopedStyle.Flags & static_cast<std::uint16_t>(Text::TextStyleFlags::Bold)) != 0)
        {
            ScopedSawBold = true;
            CheckString(ScopedBlock->View().substr(ScopedSpan.Begin, ScopedSpan.End - ScopedSpan.Begin), "bold",
                        "bold span text");
        }
        if (ScopedStyle.Color.rgba == Color32::FromBytes(0xFF, 0, 0, 0xFF).rgba)
        {
            ScopedSawRed = true;
            CheckString(ScopedBlock->View().substr(ScopedSpan.Begin, ScopedSpan.End - ScopedSpan.Begin), "red",
                        "red span text");
        }
    }
    Check(ScopedSawBold, "markup has a bold span");
    Check(ScopedSawRed, "markup has a red span");
}

void TestNestingAndSize()
{
    using namespace Fby;

    auto ScopedBlock = std::make_unique<Text::TextStyledBlock>();
    Check(Text::CompileTextMarkup("[b]a[i]b[/i]c[/b]", ScopedBlock.get()) == Text::TextMarkupStatus::Ok,
          "nested markup compiles");
    CheckString(ScopedBlock->View(), "abc", "nested markup text");
    Check(ScopedBlock->SpanCount == 3, "nested markup runs");
    if (ScopedBlock->SpanCount == 3)
    {
        const Text::TextStyle& ScopedOuter = ScopedBlock->Styles[ScopedBlock->Spans[0].StyleIndex];
        const Text::TextStyle& ScopedInner = ScopedBlock->Styles[ScopedBlock->Spans[1].StyleIndex];
        const Text::TextStyle& ScopedAfter = ScopedBlock->Styles[ScopedBlock->Spans[2].StyleIndex];
        Check((ScopedOuter.Flags & static_cast<std::uint16_t>(Text::TextStyleFlags::Bold)) != 0, "outer is bold");
        Check((ScopedInner.Flags & static_cast<std::uint16_t>(Text::TextStyleFlags::Italic)) != 0,
              "inner is italic");
        Check((ScopedAfter.Flags & static_cast<std::uint16_t>(Text::TextStyleFlags::Bold)) != 0,
              "after inner is bold again");
    }

    Check(Text::CompileTextMarkup("[size=24]big[/size]", ScopedBlock.get()) == Text::TextMarkupStatus::Ok,
          "size markup compiles");
    Check(ScopedBlock->Styles[ScopedBlock->Spans[0].StyleIndex].FontSize == 24, "size markup value");
}

void TestActions()
{
    using namespace Fby;

    auto ScopedBlock = std::make_unique<Text::TextStyledBlock>();
    Check(Text::CompileTextMarkup("Go [link=7]there[/link] now", ScopedBlock.get()) ==
              Text::TextMarkupStatus::Ok,
          "link markup compiles");
    CheckString(ScopedBlock->View(), "Go there now", "link markup text");
    Check(ScopedBlock->ActionCount == 1, "link creates one action");
    if (ScopedBlock->ActionCount == 1)
    {
        const Text::TextAction& ScopedAction = ScopedBlock->Actions[0];
        Check(ScopedAction.Begin == 3 && ScopedAction.End == 8, "link byte range");
        Check(ScopedAction.ActionId == 7, "link action id");
        Check(ScopedAction.Kind == Text::TextActionKind::Link, "link action kind");
    }
    Check(ScopedBlock->ActionAt(3) != nullptr, "link hit test inside");
    Check(ScopedBlock->ActionAt(8) == nullptr, "link hit test outside");

    Check(Text::CompileTextMarkup("[link=5][b]x[/b]y[/link]", ScopedBlock.get()) == Text::TextMarkupStatus::Ok,
          "nested link compiles");
    Check(ScopedBlock->ActionCount == 1, "nested link creates one action");
    if (ScopedBlock->ActionCount == 1)
    {
        Check(ScopedBlock->Actions[0].Begin == 0 && ScopedBlock->Actions[0].End == 2, "nested link range");
    }

    Check(Text::CompileTextMarkup("[action=42]run[/action]", ScopedBlock.get()) == Text::TextMarkupStatus::Ok,
          "command markup compiles");
    Check(ScopedBlock->ActionCount == 1 && ScopedBlock->Actions[0].Kind == Text::TextActionKind::Command,
          "command kind");
    Check(Text::CompileTextMarkup("[link=abc]x[/link]", ScopedBlock.get()) == Text::TextMarkupStatus::InvalidMarkup,
          "bad link id rejects");

    Check(Text::CompileTextMarkup("A[icon=5:20:12]B", ScopedBlock.get()) == Text::TextMarkupStatus::Ok,
          "inline icon markup compiles");
    Check(ScopedBlock->View().size() == 5, "inline icon inserts a placeholder");
    Check(ScopedBlock->InlineCount == 1, "inline icon is recorded");
    if (ScopedBlock->InlineCount == 1)
    {
        Check(ScopedBlock->Inlines[0].ByteOffset == 1, "inline icon byte offset");
        Check(ScopedBlock->Inlines[0].Width == 20.0f && ScopedBlock->Inlines[0].Height == 12.0f,
              "inline icon size");
    }
}

void TestInlineKinds()
{
    using namespace Fby;

    auto ScopedBlock = std::make_unique<Text::TextStyledBlock>();
    Check(Text::CompileTextMarkup("[border=20:10]", ScopedBlock.get()) == Text::TextMarkupStatus::Ok,
          "border inline compiles");
    Check(ScopedBlock->InlineCount == 1 && ScopedBlock->Inlines[0].Kind == Text::TextInlineKind::Border,
          "border inline kind");

    Check(Text::CompileTextMarkup("[progress=50:20:8]", ScopedBlock.get()) == Text::TextMarkupStatus::Ok,
          "progress inline compiles");
    Check(ScopedBlock->Inlines[0].Kind == Text::TextInlineKind::Progress &&
              ScopedBlock->Inlines[0].Progress == 0.5f,
          "progress inline value");

    Check(Text::CompileTextMarkup("[separator=20:2]", ScopedBlock.get()) == Text::TextMarkupStatus::Ok,
          "separator inline compiles");
    Check(ScopedBlock->Inlines[0].Kind == Text::TextInlineKind::Separator, "separator inline kind");

    Check(Text::CompileTextMarkup("[button=7:20:10]", ScopedBlock.get()) == Text::TextMarkupStatus::Ok,
          "button inline compiles");
    Check(ScopedBlock->Inlines[0].Kind == Text::TextInlineKind::Button && ScopedBlock->Inlines[0].Id == 7,
          "button inline id");
}

void TestConditions()
{
    using namespace Fby;

    auto ScopedBlock = std::make_unique<Text::TextStyledBlock>();
    constexpr std::string_view kMarkup = "A[if=0]yes[/if]B[ifnot=0]no[/if]";
    Check(Text::CompileTextMarkup(kMarkup, ScopedBlock.get(), 0) == Text::TextMarkupStatus::Ok,
          "condition markup compiles with flag clear");
    CheckString(ScopedBlock->View(), "ABno", "condition clear path");
    Check(Text::CompileTextMarkup(kMarkup, ScopedBlock.get(), 1) == Text::TextMarkupStatus::Ok,
          "condition markup compiles with flag set");
    CheckString(ScopedBlock->View(), "AyesB", "condition set path");

    Check(Text::CompileTextMarkup("X[if=0]a[if=1]b[/if]c[/if]Y", ScopedBlock.get(), 1) ==
              Text::TextMarkupStatus::Ok,
          "nested conditions compile");
    CheckString(ScopedBlock->View(), "XacY", "nested condition path");

    Check(Text::CompileTextMarkup("[if=0][b]hidden[/b][icon=1:4:4][/if]shown", ScopedBlock.get(), 0) ==
              Text::TextMarkupStatus::Ok,
          "skipped rich content compiles");
    CheckString(ScopedBlock->View(), "shown", "skipped rich content is removed");
    Check(ScopedBlock->InlineCount == 0, "skipped inline is not recorded");

    Check(Text::CompileTextMarkup("[if=0]unclosed", ScopedBlock.get(), 0) == Text::TextMarkupStatus::InvalidMarkup,
          "unclosed condition rejects");
}

void TestErrors()
{
    using namespace Fby;

    auto ScopedBlock = std::make_unique<Text::TextStyledBlock>();
    Check(Text::CompileTextMarkup("[unknown]x[/unknown]", ScopedBlock.get()) ==
              Text::TextMarkupStatus::UnsupportedTag,
          "unsupported tag rejects");
    Check(Text::CompileTextMarkup("[b]unclosed", ScopedBlock.get()) == Text::TextMarkupStatus::InvalidMarkup,
          "unclosed tag rejects");
    Check(Text::CompileTextMarkup("[/b]", ScopedBlock.get()) == Text::TextMarkupStatus::InvalidMarkup,
          "unmatched close rejects");
    Check(Text::CompileTextMarkup("[color=red]x[/color]", ScopedBlock.get()) == Text::TextMarkupStatus::InvalidMarkup,
          "bad color rejects");
}
}

int main()
{
    TestBasicStyles();
    TestNestingAndSize();
    TestActions();
    TestInlineKinds();
    TestConditions();
    TestErrors();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text markup test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text markup tests: ok\n");
    return 0;
}
