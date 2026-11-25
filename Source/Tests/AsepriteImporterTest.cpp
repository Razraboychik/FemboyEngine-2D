#include "Engine/Assets/AsepriteImporter.hpp"

#include <cstdio>
#include <string>

namespace
{
std::uint32_t GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

constexpr const char* kSheet = R"({
  "frames": {
    "idle 0": {"frame": {"x": 0, "y": 0, "w": 16, "h": 16}, "duration": 120},
    "idle 1": {"frame": {"x": 16, "y": 0, "w": 16, "h": 16}, "duration": 80}
  },
  "meta": {
    "image": "hero.png",
    "size": {"w": 64, "h": 32},
    "frameTags": [
      {"name": "idle", "from": 0, "to": 1, "direction": "forward"}
    ]
  }
})";
}

int main()
{
    Fby::Assets::AsepriteSheet ScopedSheet;
    std::string ScopedError;
    Check(Fby::Assets::ParseAsepriteJson(kSheet, &ScopedSheet, &ScopedError), "Aseprite sheet parses");
    Check(ScopedSheet.Width == 64 && ScopedSheet.Height == 32, "sheet size");
    Check(ScopedSheet.ImagePath == "hero.png", "image path");
    Check(ScopedSheet.Frames.size() == 2, "frame count");
    Check(ScopedSheet.Frames[0].Name == "idle 0" && ScopedSheet.Frames[1].X == 16, "frame fields");
    Check(ScopedSheet.Frames[0].DurationMs == 120, "frame duration");
    Check(ScopedSheet.Tags.size() == 1 && ScopedSheet.Tags[0].From == 0 && ScopedSheet.Tags[0].To == 1,
          "frame tag");

    const std::string ScopedBadBounds = R"({"frames":{"x":{"frame":{"x":60,"y":0,"w":16,"h":16}}},
        "meta":{"image":"x.png","size":{"w":64,"h":32}}})";
    Check(!Fby::Assets::ParseAsepriteJson(ScopedBadBounds, &ScopedSheet, &ScopedError),
          "frame outside the sheet is rejected");
    Check(!Fby::Assets::ParseAsepriteJson("{", &ScopedSheet, &ScopedError), "invalid JSON is rejected");
    Check(!Fby::Assets::ParseAsepriteJson(R"({"frames":{},"meta":{}})", &ScopedSheet, &ScopedError),
          "empty sheet is rejected");

    Fby::SpriteSheetComponent ScopedSpriteSheet;
    std::uint32_t ScopedStart = 0;
    float ScopedDuration = 0.0f;
    Check(Fby::Assets::AsepriteToSpriteSheet(ScopedSheet, 0, 2, &ScopedSpriteSheet, &ScopedStart, &ScopedDuration,
                                             &ScopedError),
          "uniform Aseprite sheet converts");
    Check(ScopedSpriteSheet.Columns == 4 && ScopedSpriteSheet.Rows == 2 && ScopedStart == 0,
          "sprite sheet grid and start cell");
    Check(ScopedDuration > 0.09f && ScopedDuration < 0.11f, "frame duration averages");

    const std::string ScopedNonUniform = R"({"frames":{
        "a":{"frame":{"x":0,"y":0,"w":16,"h":16},"duration":100},
        "b":{"frame":{"x":17,"y":0,"w":16,"h":16},"duration":100}},
        "meta":{"image":"x.png","size":{"w":64,"h":32}}})";
    Fby::Assets::AsepriteSheet ScopedBadSheet;
    Check(Fby::Assets::ParseAsepriteJson(ScopedNonUniform, &ScopedBadSheet, &ScopedError), "non-grid JSON parses");
    Check(!Fby::Assets::AsepriteToSpriteSheet(ScopedBadSheet, 0, 2, &ScopedSpriteSheet, &ScopedStart, &ScopedDuration,
                                              &ScopedError),
          "non-grid Aseprite sheet is rejected");

    std::uint32_t ScopedParsed = 0;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 5000; ++ScopedIteration)
    {
        if (Fby::Assets::ParseAsepriteJson(kSheet, &ScopedSheet, &ScopedError))
        {
            ++ScopedParsed;
        }
    }
    Check(ScopedParsed == 5000, "Aseprite parse stress");

    if (GFailures == 0)
    {
        std::printf("aseprite_importer_test: all checks passed\n");
        return 0;
    }
    return 1;
}
