// FemboyEngine2D - shared .fblend authoring file codec implementation.
#include "Editor/Panels/DirectionalBlendFile.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace Fby::Editor::DirectionalBlendFile
{
namespace
{

[[nodiscard]] std::string TrimLeadingSpaces(const std::string& InText)
{
    const std::size_t ScopedStart = InText.find_first_not_of(" \t");
    return ScopedStart == std::string::npos ? std::string{} : InText.substr(ScopedStart);
}

// Newlines and tabs break the line-oriented file, so the writer folds them
// into spaces instead of rejecting a valid clip path.
[[nodiscard]] std::string SingleLine(const std::string& InText)
{
    std::string ScopedResult = InText;
    for (char& ScopedCharacter : ScopedResult)
    {
        if (ScopedCharacter == '\n' || ScopedCharacter == '\r' || ScopedCharacter == '\t')
        {
            ScopedCharacter = ' ';
        }
    }
    return ScopedResult;
}
}

bool Save(const std::filesystem::path& InPath, const Animation::DirectionalBlendSpaceDocument& InDocument,
          const PathArray& InPaths, std::string* OutError)
{
    if (InPath.empty())
    {
        if (OutError != nullptr)
        {
            *OutError = "no file path";
        }
        return false;
    }
    std::error_code ScopedError;
    if (!InPath.parent_path().empty())
    {
        std::filesystem::create_directories(InPath.parent_path(), ScopedError);
    }
    std::ofstream ScopedFile(InPath, std::ios::trunc);
    if (!ScopedFile)
    {
        if (OutError != nullptr)
        {
            *OutError = "cannot open " + InPath.string();
        }
        return false;
    }
    ScopedFile << "FBYBLEND 1\n";
    ScopedFile << "topology " << (InDocument.Topology == Animation::DirectionalBlendTopology::Ring ? "ring" : "plane")
               << "\n";
    ScopedFile << "normalize " << (InDocument.NormalizeWeights ? 1 : 0) << "\n";
    ScopedFile << std::setprecision(9);
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < InDocument.AnchorCount; ++ScopedIndex)
    {
        const Animation::DirectionalBlendAnchor& ScopedAnchor = InDocument.Anchors[ScopedIndex];
        ScopedFile << "anchor " << ScopedAnchor.Position.X << ' ' << ScopedAnchor.Position.Y << ' '
                   << ScopedAnchor.ClipId << ' ' << ScopedAnchor.WeightScale << ' ' << (ScopedAnchor.Enabled ? 1 : 0)
                   << ' ' << SingleLine(InPaths[ScopedIndex]) << "\n";
    }
    if (!ScopedFile)
    {
        if (OutError != nullptr)
        {
            *OutError = "write failed";
        }
        return false;
    }
    return true;
}

bool Load(const std::filesystem::path& InPath, Animation::DirectionalBlendSpaceDocument* OutDocument,
          PathArray* OutPaths, std::string* OutError)
{
    if (OutDocument == nullptr)
    {
        if (OutError != nullptr)
        {
            *OutError = "no document target";
        }
        return false;
    }
    std::ifstream ScopedFile(InPath);
    if (!ScopedFile)
    {
        if (OutError != nullptr)
        {
            *OutError = "cannot open " + InPath.string();
        }
        return false;
    }
    std::string ScopedLine;
    if (!std::getline(ScopedFile, ScopedLine) || ScopedLine != "FBYBLEND 1")
    {
        if (OutError != nullptr)
        {
            *OutError = "missing FBYBLEND header";
        }
        return false;
    }
    Animation::DirectionalBlendSpaceDocument ScopedDocument;
    PathArray ScopedPaths{};
    while (std::getline(ScopedFile, ScopedLine))
    {
        if (ScopedLine.empty() || ScopedLine[0] == '#')
        {
            continue;
        }
        std::istringstream ScopedStream(ScopedLine);
        std::string ScopedKey;
        ScopedStream >> ScopedKey;
        if (ScopedKey == "topology")
        {
            std::string ScopedValue;
            ScopedStream >> ScopedValue;
            if (ScopedValue == "ring")
            {
                ScopedDocument.Topology = Animation::DirectionalBlendTopology::Ring;
            }
            else if (ScopedValue == "plane")
            {
                ScopedDocument.Topology = Animation::DirectionalBlendTopology::Plane;
            }
            else
            {
                if (OutError != nullptr)
                {
                    *OutError = "unknown topology " + ScopedValue;
                }
                return false;
            }
        }
        else if (ScopedKey == "normalize")
        {
            int ScopedValue = 0;
            ScopedStream >> ScopedValue;
            ScopedDocument.NormalizeWeights = ScopedValue != 0;
        }
        else if (ScopedKey == "anchor")
        {
            if (ScopedDocument.AnchorCount >= Animation::kMaxDirectionalAnchors)
            {
                if (OutError != nullptr)
                {
                    *OutError = "too many anchors in file";
                }
                return false;
            }
            float ScopedX = 0.0f;
            float ScopedY = 0.0f;
            float ScopedScale = 1.0f;
            std::uint32_t ScopedClip = 0;
            int ScopedEnabled = 1;
            if (!(ScopedStream >> ScopedX >> ScopedY >> ScopedClip >> ScopedScale >> ScopedEnabled))
            {
                if (OutError != nullptr)
                {
                    *OutError = "malformed anchor line";
                }
                return false;
            }
            std::string ScopedPath;
            std::getline(ScopedStream, ScopedPath);
            Animation::DirectionalBlendAnchor& ScopedAnchor = ScopedDocument.Anchors[ScopedDocument.AnchorCount];
            ScopedAnchor.Position = {ScopedX, ScopedY};
            ScopedAnchor.ClipId = ScopedClip;
            ScopedAnchor.WeightScale = ScopedScale;
            ScopedAnchor.Enabled = ScopedEnabled != 0;
            ScopedPaths[ScopedDocument.AnchorCount] = TrimLeadingSpaces(ScopedPath);
            ++ScopedDocument.AnchorCount;
        }
    }
    if (!Animation::ValidateDirectionalBlendSpace(ScopedDocument))
    {
        if (OutError != nullptr)
        {
            *OutError = "file document is invalid";
        }
        return false;
    }
    *OutDocument = ScopedDocument;
    if (OutPaths != nullptr)
    {
        *OutPaths = ScopedPaths;
    }
    return true;
}
}
