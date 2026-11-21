#include "Engine/Assets/AsepriteImporter.hpp"

#include <cctype>
#include <cstdlib>
#include <utility>

namespace Fby::Assets
{
namespace
{
struct Json
{
    enum class Kind : std::uint8_t
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Kind Type = Kind::Null;
    bool Boolean = false;
    double Number = 0.0;
    std::string Text;
    std::vector<Json> Array;
    std::vector<std::pair<std::string, Json>> Object;
};

void SetError(std::string* InError, std::string_view InMessage)
{
    if (InError != nullptr)
    {
        InError->assign(InMessage);
    }
}

class Reader
{
public:
    explicit Reader(std::string_view InText) : MText(InText) {}

    [[nodiscard]] bool Parse(Json* OutValue, std::string* InError)
    {
        Skip();
        if (!Value(OutValue, 0) || (Skip(), MPosition != MText.size()))
        {
            SetError(InError, "Aseprite JSON is invalid");
            return false;
        }
        return true;
    }

private:
    void Skip() noexcept
    {
        while (MPosition < MText.size() && std::isspace(static_cast<unsigned char>(MText[MPosition])) != 0)
        {
            ++MPosition;
        }
    }

    bool String(std::string* OutText)
    {
        if (MPosition >= MText.size() || MText[MPosition] != '"')
        {
            return false;
        }
        ++MPosition;
        while (MPosition < MText.size())
        {
            const char ScopedChar = MText[MPosition++];
            if (ScopedChar == '"')
            {
                return true;
            }
            if (ScopedChar == '\\')
            {
                if (MPosition >= MText.size())
                {
                    return false;
                }
                const char ScopedEscape = MText[MPosition++];
                switch (ScopedEscape)
                {
                    case '"':
                    case '\\':
                    case '/':
                        OutText->push_back(ScopedEscape);
                        break;
                    case 'b':
                        OutText->push_back('\b');
                        break;
                    case 'f':
                        OutText->push_back('\f');
                        break;
                    case 'n':
                        OutText->push_back('\n');
                        break;
                    case 'r':
                        OutText->push_back('\r');
                        break;
                    case 't':
                        OutText->push_back('\t');
                        break;
                    default:
                        return false;
                }
                continue;
            }
            if (static_cast<unsigned char>(ScopedChar) < 0x20)
            {
                return false;
            }
            OutText->push_back(ScopedChar);
        }
        return false;
    }

    bool Number(double* OutNumber)
    {
        const std::size_t ScopedStart = MPosition;
        while (MPosition < MText.size())
        {
            const char ScopedChar = MText[MPosition];
            if (std::isdigit(static_cast<unsigned char>(ScopedChar)) != 0 || ScopedChar == '-' || ScopedChar == '+' ||
                ScopedChar == '.' || ScopedChar == 'e' || ScopedChar == 'E')
            {
                ++MPosition;
                continue;
            }
            break;
        }
        if (ScopedStart == MPosition)
        {
            return false;
        }
        const std::string ScopedText(MText.substr(ScopedStart, MPosition - ScopedStart));
        char* ScopedEnd = nullptr;
        const double ScopedValue = std::strtod(ScopedText.c_str(), &ScopedEnd);
        if (ScopedEnd == nullptr || *ScopedEnd != '\0')
        {
            return false;
        }
        *OutNumber = ScopedValue;
        return true;
    }

    bool Value(Json* OutValue, std::uint32_t InDepth)
    {
        if (InDepth > 32)
        {
            return false;
        }
        Skip();
        if (MPosition >= MText.size())
        {
            return false;
        }
        const char ScopedChar = MText[MPosition];
        if (ScopedChar == '{')
        {
            ++MPosition;
            OutValue->Type = Json::Kind::Object;
            Skip();
            if (MPosition < MText.size() && MText[MPosition] == '}')
            {
                ++MPosition;
                return true;
            }
            while (true)
            {
                Skip();
                std::string ScopedKey;
                if (!String(&ScopedKey))
                {
                    return false;
                }
                Skip();
                if (MPosition >= MText.size() || MText[MPosition] != ':')
                {
                    return false;
                }
                ++MPosition;
                Json ScopedValue;
                if (!Value(&ScopedValue, InDepth + 1))
                {
                    return false;
                }
                OutValue->Object.emplace_back(std::move(ScopedKey), std::move(ScopedValue));
                Skip();
                if (MPosition < MText.size() && MText[MPosition] == ',')
                {
                    ++MPosition;
                    continue;
                }
                if (MPosition < MText.size() && MText[MPosition] == '}')
                {
                    ++MPosition;
                    return true;
                }
                return false;
            }
        }
        if (ScopedChar == '[')
        {
            ++MPosition;
            OutValue->Type = Json::Kind::Array;
            Skip();
            if (MPosition < MText.size() && MText[MPosition] == ']')
            {
                ++MPosition;
                return true;
            }
            while (true)
            {
                Json ScopedValue;
                if (!Value(&ScopedValue, InDepth + 1))
                {
                    return false;
                }
                OutValue->Array.push_back(std::move(ScopedValue));
                Skip();
                if (MPosition < MText.size() && MText[MPosition] == ',')
                {
                    ++MPosition;
                    continue;
                }
                if (MPosition < MText.size() && MText[MPosition] == ']')
                {
                    ++MPosition;
                    return true;
                }
                return false;
            }
        }
        if (ScopedChar == '"')
        {
            OutValue->Type = Json::Kind::String;
            return String(&OutValue->Text);
        }
        if (MText.substr(MPosition, 4) == "true")
        {
            MPosition += 4;
            OutValue->Type = Json::Kind::Bool;
            OutValue->Boolean = true;
            return true;
        }
        if (MText.substr(MPosition, 5) == "false")
        {
            MPosition += 5;
            OutValue->Type = Json::Kind::Bool;
            return true;
        }
        if (MText.substr(MPosition, 4) == "null")
        {
            MPosition += 4;
            OutValue->Type = Json::Kind::Null;
            return true;
        }
        OutValue->Type = Json::Kind::Number;
        return Number(&OutValue->Number);
    }

    std::string_view MText;
    std::size_t MPosition = 0;
};

[[nodiscard]] const Json* Member(const Json& InObject, std::string_view InName) noexcept
{
    if (InObject.Type != Json::Kind::Object)
    {
        return nullptr;
    }
    for (const auto& [ScopedKey, ScopedValue] : InObject.Object)
    {
        if (ScopedKey == InName)
        {
            return &ScopedValue;
        }
    }
    return nullptr;
}

[[nodiscard]] bool IntField(const Json& InObject, std::string_view InName, std::int32_t* OutValue) noexcept
{
    const Json* ScopedValue = Member(InObject, InName);
    if (ScopedValue == nullptr || ScopedValue->Type != Json::Kind::Number)
    {
        return false;
    }
    *OutValue = static_cast<std::int32_t>(ScopedValue->Number);
    return true;
}

[[nodiscard]] bool TextField(const Json& InObject, std::string_view InName, std::string* OutValue) noexcept
{
    const Json* ScopedValue = Member(InObject, InName);
    if (ScopedValue == nullptr || ScopedValue->Type != Json::Kind::String)
    {
        return false;
    }
    *OutValue = ScopedValue->Text;
    return true;
}
}

bool ParseAsepriteJson(std::string_view InJson, AsepriteSheet* OutSheet, std::string* InError)
{
    if (OutSheet == nullptr || InJson.empty())
    {
        SetError(InError, "Aseprite JSON is empty");
        return false;
    }
    Json ScopedRoot;
    Reader ScopedReader(InJson);
    if (!ScopedReader.Parse(&ScopedRoot, InError) || ScopedRoot.Type != Json::Kind::Object)
    {
        return false;
    }
    const Json* ScopedFrames = Member(ScopedRoot, "frames");
    const Json* ScopedMeta = Member(ScopedRoot, "meta");
    if (ScopedFrames == nullptr || ScopedMeta == nullptr)
    {
        SetError(InError, "Aseprite JSON needs frames and meta");
        return false;
    }

    AsepriteSheet ScopedSheet;
    if (const Json* ScopedSize = Member(*ScopedMeta, "size"); ScopedSize != nullptr)
    {
        std::int32_t ScopedWidth = 0;
        std::int32_t ScopedHeight = 0;
        if (IntField(*ScopedSize, "w", &ScopedWidth) && IntField(*ScopedSize, "h", &ScopedHeight) &&
            ScopedWidth > 0 && ScopedHeight > 0)
        {
            ScopedSheet.Width = static_cast<std::uint32_t>(ScopedWidth);
            ScopedSheet.Height = static_cast<std::uint32_t>(ScopedHeight);
        }
    }
    (void)TextField(*ScopedMeta, "image", &ScopedSheet.ImagePath);

    const auto ScopedAddFrame = [&ScopedSheet](const std::string& InName, const Json& InFrame,
                                               std::string* InLocalError) -> bool
    {
        const Json* ScopedRect = Member(InFrame, "frame");
        if (ScopedRect == nullptr)
        {
            SetError(InLocalError, "Aseprite frame has no rectangle");
            return false;
        }
        AsepriteFrame ScopedValue;
        ScopedValue.Name = InName;
        std::int32_t ScopedX = 0;
        std::int32_t ScopedY = 0;
        std::int32_t ScopedWidth = 0;
        std::int32_t ScopedHeight = 0;
        if (!IntField(*ScopedRect, "x", &ScopedX) || !IntField(*ScopedRect, "y", &ScopedY) ||
            !IntField(*ScopedRect, "w", &ScopedWidth) || !IntField(*ScopedRect, "h", &ScopedHeight) ||
            ScopedWidth <= 0 || ScopedHeight <= 0)
        {
            SetError(InLocalError, "Aseprite frame rectangle is invalid");
            return false;
        }
        ScopedValue.X = ScopedX;
        ScopedValue.Y = ScopedY;
        ScopedValue.Width = static_cast<std::uint32_t>(ScopedWidth);
        ScopedValue.Height = static_cast<std::uint32_t>(ScopedHeight);
        if (const Json* ScopedDuration = Member(InFrame, "duration");
            ScopedDuration != nullptr && ScopedDuration->Type == Json::Kind::Number && ScopedDuration->Number > 0.0)
        {
            ScopedValue.DurationMs = static_cast<std::uint32_t>(ScopedDuration->Number);
        }
        ScopedSheet.Frames.push_back(std::move(ScopedValue));
        return true;
    };

    if (ScopedFrames->Type == Json::Kind::Object)
    {
        for (const auto& [ScopedName, ScopedFrame] : ScopedFrames->Object)
        {
            if (!ScopedAddFrame(ScopedName, ScopedFrame, InError))
            {
                return false;
            }
        }
    }
    else if (ScopedFrames->Type == Json::Kind::Array)
    {
        for (const Json& ScopedFrame : ScopedFrames->Array)
        {
            std::string ScopedName;
            (void)TextField(ScopedFrame, "filename", &ScopedName);
            if (!ScopedAddFrame(ScopedName, ScopedFrame, InError))
            {
                return false;
            }
        }
    }
    else
    {
        SetError(InError, "Aseprite frames must be an object or array");
        return false;
    }
    if (ScopedSheet.Frames.empty() || ScopedSheet.Frames.size() > 4096)
    {
        SetError(InError, "Aseprite sheet has no frames or too many frames");
        return false;
    }
    for (const AsepriteFrame& ScopedFrame : ScopedSheet.Frames)
    {
        if (ScopedSheet.Width != 0 &&
            (ScopedFrame.X < 0 || ScopedFrame.Y < 0 ||
             static_cast<std::uint32_t>(ScopedFrame.X) + ScopedFrame.Width > ScopedSheet.Width ||
             static_cast<std::uint32_t>(ScopedFrame.Y) + ScopedFrame.Height > ScopedSheet.Height))
        {
            SetError(InError, "Aseprite frame escapes the sheet");
            return false;
        }
    }

    if (const Json* ScopedTags = Member(*ScopedMeta, "frameTags");
        ScopedTags != nullptr && ScopedTags->Type == Json::Kind::Array)
    {
        for (const Json& ScopedTag : ScopedTags->Array)
        {
            AsepriteTag ScopedValue;
            std::int32_t ScopedFrom = 0;
            std::int32_t ScopedTo = 0;
            (void)TextField(ScopedTag, "name", &ScopedValue.Name);
            if (!IntField(ScopedTag, "from", &ScopedFrom) || !IntField(ScopedTag, "to", &ScopedTo) ||
                ScopedFrom < 0 || ScopedTo < ScopedFrom ||
                static_cast<std::size_t>(ScopedTo) >= ScopedSheet.Frames.size())
            {
                SetError(InError, "Aseprite frame tag is invalid");
                return false;
            }
            ScopedValue.From = static_cast<std::uint32_t>(ScopedFrom);
            ScopedValue.To = static_cast<std::uint32_t>(ScopedTo);
            if (const Json* ScopedDirection = Member(ScopedTag, "direction");
                ScopedDirection != nullptr && ScopedDirection->Type == Json::Kind::String)
            {
                ScopedValue.Forward = ScopedDirection->Text != "reverse";
            }
            ScopedSheet.Tags.push_back(std::move(ScopedValue));
        }
    }
    *OutSheet = std::move(ScopedSheet);
    return true;
}

bool AsepriteToSpriteSheet(const AsepriteSheet& InSheet, std::uint32_t InFirstFrame, std::uint32_t InFrameCount,
                           SpriteSheetComponent* OutSheet, std::uint32_t* OutStartFrame, float* OutFrameDuration,
                           std::string* InError)
{
    if (OutSheet == nullptr || OutStartFrame == nullptr || OutFrameDuration == nullptr || InFrameCount == 0 ||
        InFirstFrame >= InSheet.Frames.size() || InFrameCount > InSheet.Frames.size() - InFirstFrame ||
        InSheet.Width == 0 || InSheet.Height == 0)
    {
        SetError(InError, "Aseprite clip range is invalid");
        return false;
    }
    const AsepriteFrame& ScopedFirst = InSheet.Frames[InFirstFrame];
    if (ScopedFirst.Width == 0 || ScopedFirst.Height == 0 || InSheet.Width % ScopedFirst.Width != 0 ||
        InSheet.Height % ScopedFirst.Height != 0)
    {
        SetError(InError, "Aseprite sheet is not grid-aligned");
        return false;
    }
    const std::uint32_t ScopedColumns = InSheet.Width / ScopedFirst.Width;
    const std::uint32_t ScopedRows = InSheet.Height / ScopedFirst.Height;
    std::uint64_t ScopedDuration = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrameCount; ++ScopedIndex)
    {
        const AsepriteFrame& ScopedFrame = InSheet.Frames[InFirstFrame + ScopedIndex];
        if (ScopedFrame.Width != ScopedFirst.Width || ScopedFrame.Height != ScopedFirst.Height ||
            ScopedFrame.X % static_cast<std::int32_t>(ScopedFirst.Width) != 0 ||
            ScopedFrame.Y % static_cast<std::int32_t>(ScopedFirst.Height) != 0)
        {
            SetError(InError, "Aseprite frames do not share one cell grid");
            return false;
        }
        ScopedDuration += ScopedFrame.DurationMs;
    }
    const std::uint32_t ScopedCell =
        static_cast<std::uint32_t>(ScopedFirst.Y / static_cast<std::int32_t>(ScopedFirst.Height)) * ScopedColumns +
        static_cast<std::uint32_t>(ScopedFirst.X / static_cast<std::int32_t>(ScopedFirst.Width));
    SpriteSheetComponent ScopedSheet;
    ScopedSheet.Columns = ScopedColumns;
    ScopedSheet.Rows = ScopedRows;
    *OutSheet = ScopedSheet;
    *OutStartFrame = ScopedCell;
    *OutFrameDuration = static_cast<float>(ScopedDuration) / (1000.0f * static_cast<float>(InFrameCount));
    return true;
}
}
