// FemboyEngine2D - IME-committed text enters a TextInput at the caret.
#include "Engine/UI/PinkUI.hpp"

#include <cstdio>
#include <string>

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
}

int main()
{
    using namespace Fby;
    using namespace Fby::Ui;

    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{320.0f, 100.0f});
    WidgetDesc ScopedDesc;
    ScopedDesc.kind = WidgetKind::TextInput;
    ScopedDesc.flags = static_cast<std::uint32_t>(WidgetFlags::Visible) |
                       static_cast<std::uint32_t>(WidgetFlags::Enabled);
    const std::int32_t ScopedInput = ScopedCanvas.AddWidget(ScopedDesc);

    Check(ScopedCanvas.SetText(ScopedInput, "ac"), "text input accepts a caption");
    Check(ScopedCanvas.SetCaret(ScopedInput, 1, false), "caret moves between the characters");
    Check(ScopedCanvas.InsertText(ScopedInput, "b"), "committed IME text inserts");
    Check(ScopedCanvas.Text(ScopedInput) == "abc", "the committed text lands at the caret");
    Check(ScopedCanvas.Caret(ScopedInput) == 2, "the caret advances past the inserted byte");

    // A multi-byte UTF-8 commit is inserted as a byte sequence.
    Check(ScopedCanvas.InsertText(ScopedInput, "\xE6\x97\xA5"), "a multi-byte commit inserts");
    Check(ScopedCanvas.Text(ScopedInput) == "ab\xE6\x97\xA5" "c", "the UTF-8 sequence is kept intact");
    Check(ScopedCanvas.Caret(ScopedInput) == 5, "the caret counts bytes");

    // Selecting a range and committing replaces it, as IME confirmation does.
    ScopedCanvas.SelectAll(ScopedInput);
    Check(ScopedCanvas.InsertText(ScopedInput, "x"), "committing over a selection replaces it");
    Check(ScopedCanvas.Text(ScopedInput) == "x", "the selection is replaced");
    Check(ScopedCanvas.Caret(ScopedInput) == 1, "the caret sits after the replacement");

    // Newlines are ignored by the single-line field.
    Check(ScopedCanvas.InsertText(ScopedInput, "y\nz"), "a commit with a newline is accepted");
    Check(ScopedCanvas.Text(ScopedInput) == "xy z" || ScopedCanvas.Text(ScopedInput) == "xyz",
          "the newline is dropped or spaced, never kept");

    if (GFailures == 0)
    {
        std::printf("pinkui_ime: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_ime: %d failure(s)\n", GFailures);
    return 1;
}
