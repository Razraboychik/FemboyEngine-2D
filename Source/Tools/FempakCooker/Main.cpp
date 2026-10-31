// FemboyEngine2D - .fempak cooker.
#include "AudioCook.hpp"
#include "Engine/Assets/CookProgress.hpp"
#include "Engine/Assets/TextureCook.hpp"

#include "Engine/Assets/FempakWriter.hpp"
#include "Engine/Assets/FemArchiver.hpp"
#include "Engine/Assets/LocalizationCompiler.hpp"
#include "Engine/Assets/PakFormat.hpp"
#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/Core/SecureRng.hpp"

#include <toml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
using Fby::Assets::FempakWriter;
using Fby::Assets::NormalizePakPath;

struct CookFile
{
    fs::path source;
    std::string destination;
    // "localization", "texture", "audio", "copy", or "" = infer
    std::string recipe;
};

struct PackPlan
{
    std::vector<CookFile> files;
    std::vector<std::pair<fs::path, std::string>> scans;
    std::string output;
};

// Everything the cook command accepts. The texture fields are forwarded to
struct CookOptions
{
    fs::path root = "assets";
    fs::path output = "assets.fempak";
    fs::path manifest;
    // Optional fixed-record progress sink polled by the editor's ReleaseCooker.
    std::string progressPath;
    Fby::Cook::CookTarget target = Fby::Cook::CookTarget::Desktop;
    Fby::Cook::AstcBlock astcBlock = Fby::Cook::AstcBlock::Block4x4;
    bool srgb = true;
};

void PrintUsage()
{
    std::fprintf(stderr, "fby_fempak - FemboyEngine asset cooker\n"
                         "\n"
                         "usage:\n"
                         "  fby_fempak cook  --root <dir> --out <file.fempak> [--target desktop|mobile]\n"
                         "                   [--astc 4x4|6x6] [--linear] [--manifest <cook.toml>]\n"
                         "                   [--progress <record.bin>]\n"
                         "  fby_fempak pack  <same arguments as cook>\n"
                         "  fby_fempak list <file.fempak>\n"
                         "  fby_fempak extract <file.fempak> <directory>\n"
                         "  fby_fempak secure --in <file.fempak> --out <file.femarc>\n"
                         "                   [--entry game/assets.fempak] [--codec lzma|fby|store]\n"
                         "                   [--cipher none|chacha|aes] [--key <64hex>|--key-env <VAR>|--key-file <path>]\n"
                         "                   [--progress <record.bin>]\n"
                         "\n"
                         "secure wraps the cooked archive in the FemArchiver LZMA+AEAD container.\n"
                         "\n"
                         "textures: PNG/TGA only -> BC7 (desktop) or ASTC (mobile), full mip chain.\n"
                         "audio:    WAV 16/24-bit, Ogg Opus/Vorbis -> cooked *.fpcm; MP3 rejected.\n");
}

[[nodiscard]] std::string ToLower(std::string_view InText)
{
    std::string ScopedOut(InText);
    std::transform(ScopedOut.begin(), ScopedOut.end(), ScopedOut.begin(),
                   [](unsigned char InCharacter) { return static_cast<char>(std::tolower(InCharacter)); });
    return ScopedOut;
}

[[nodiscard]] std::string ExtensionOf(const fs::path& InPath)
{
    return ToLower(InPath.extension().string());
}

// Every format the texture recipe may see. JPEG/BMP/WebP/HDR are routed to the
[[nodiscard]] bool IsImageSource(const fs::path& InPath)
{
    const std::string ScopedExtension = ExtensionOf(InPath);
    return ScopedExtension == ".png" || ScopedExtension == ".tga" || ScopedExtension == ".jpg" ||
           ScopedExtension == ".jpeg" || ScopedExtension == ".bmp" || ScopedExtension == ".webp" ||
           ScopedExtension == ".hdr" || ScopedExtension == ".gif";
}

[[nodiscard]] bool IsAudioSource(const fs::path& InPath)
{
    const std::string ScopedExtension = ExtensionOf(InPath);
    return ScopedExtension == ".wav" || ScopedExtension == ".ogg" || ScopedExtension == ".opus" ||
           ScopedExtension == ".mp3" || ScopedExtension == ".flac";
}

[[nodiscard]] std::string ReplaceExtension(std::string_view InPath, std::string_view InExtension)
{
    const std::size_t ScopedSlash = InPath.find_last_of('/');
    const std::size_t ScopedDot = InPath.find_last_of('.');
    if (ScopedDot == std::string_view::npos || (ScopedSlash != std::string_view::npos && ScopedDot < ScopedSlash))
    {
        return std::string(InPath) + std::string(InExtension);
    }
    return std::string(InPath.substr(0, ScopedDot)) + std::string(InExtension);
}

[[nodiscard]] bool ReadTextFile(const fs::path& InPath, std::string* InText)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    *InText = std::string(std::istreambuf_iterator<char>(ScopedStream), std::istreambuf_iterator<char>());
    return static_cast<bool>(ScopedStream) || ScopedStream.eof();
}

[[nodiscard]] bool ReadBytes(const fs::path& InPath, std::vector<std::byte>* InBytes)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.seekg(0, std::ios::end);
    const std::streamoff ScopedSize = ScopedStream.tellg();
    if (ScopedSize < 0)
    {
        return false;
    }
    ScopedStream.seekg(0, std::ios::beg);
    InBytes->resize(static_cast<std::size_t>(ScopedSize));
    if (ScopedSize > 0)
    {
        ScopedStream.read(reinterpret_cast<char*>(InBytes->data()), static_cast<std::streamsize>(ScopedSize));
    }
    return static_cast<bool>(ScopedStream);
}

// Cookers
[[nodiscard]] bool CookTexture(const fs::path& InSource, const CookOptions& InOptions, std::vector<std::byte>* Output,
                               std::string* InError)
{
    Fby::Cook::TextureCookOptions ScopedTextureOptions;
    ScopedTextureOptions.target = InOptions.target;
    ScopedTextureOptions.astcBlock = InOptions.astcBlock;
    ScopedTextureOptions.srgb = InOptions.srgb;

    Fby::Cook::CookedTexture ScopedCooked;
    if (!Fby::Cook::CookTextureFile(InSource, ScopedTextureOptions, &ScopedCooked, InError))
    {
        return false;
    }
    *Output = std::move(ScopedCooked.payload);
    std::printf("  texture  %s -> %ux%u %s x%u mip%s (%zu bytes)\n", InSource.generic_string().c_str(),
                ScopedCooked.width, ScopedCooked.height, Fby::Assets::TextureFormatName(ScopedCooked.format),
                ScopedCooked.mipLevels, ScopedCooked.mipLevels == 1 ? "" : "s", Output->size());
    return true;
}

[[nodiscard]] bool CookAudio(const fs::path& InSource, std::vector<std::byte>* Output, std::string* InError)
{
    if (!Fby::Cook::CookAudioFile(InSource, Output, InError))
    {
        return false;
    }
    if (Output->empty())
    {
        *InError = "audio cooker produced an empty payload";
        return false;
    }
    std::printf("  audio    %s -> %zu bytes\n", InSource.generic_string().c_str(), Output->size());
    return true;
}

[[nodiscard]] bool CookLocalization(const fs::path& InSource, std::vector<std::byte>* Output, std::string* InError)
{
    std::string ScopedText;
    if (!ReadTextFile(InSource, &ScopedText))
    {
        *InError = "cannot read " + InSource.string();
        return false;
    }
    *Output = Fby::Assets::CompileLocalization(ScopedText, InError);
    if (Output->empty())
    {
        return false;
    }
    std::printf("  loc      %s -> %zu bytes\n", InSource.generic_string().c_str(), Output->size());
    return true;
}

// Default recipe
[[nodiscard]] std::string InferRecipe(const fs::path& InRelative)
{
    const std::string ScopedPath = NormalizePakPath(InRelative.generic_string());
    if (ScopedPath.starts_with("loc/") && ExtensionOf(InRelative) == ".toml")
    {
        return "localization";
    }
    if (IsImageSource(InRelative))
    {
        return "texture";
    }
    if (IsAudioSource(InRelative))
    {
        return "audio";
    }
    return "copy";
}

[[nodiscard]] std::string InferDestination(const fs::path& InRelative, std::string_view InRecipe)
{
    const std::string ScopedPath = NormalizePakPath(InRelative.generic_string());
    if (InRecipe == "localization")
    {
        return ReplaceExtension(ScopedPath, ".fbin");
    }
    if (InRecipe == "texture")
    {
        return ReplaceExtension(ScopedPath, ".ftex");
    }
    if (InRecipe == "audio")
    {
        return ReplaceExtension(ScopedPath, ".fpcm");
    }
    return ScopedPath;
}

[[nodiscard]] bool PlanFromDirectory(const fs::path& InRoot, PackPlan* InPlan, std::string* InError)
{
    std::error_code ScopedCode;
    if (!fs::is_directory(InRoot, ScopedCode))
    {
        *InError = "not a directory: " + InRoot.string();
        return false;
    }

    for (const fs::directory_entry& ScopedItem :
         fs::recursive_directory_iterator(InRoot, fs::directory_options::skip_permission_denied, ScopedCode))
    {
        if (!ScopedItem.is_regular_file(ScopedCode))
        {
            continue;
        }
        const fs::path ScopedRelative = fs::relative(ScopedItem.path(), InRoot, ScopedCode);
        if (ScopedCode || ScopedRelative.empty())
        {
            continue;
        }
        const std::string ScopedPath = NormalizePakPath(ScopedRelative.generic_string());
        if (ScopedPath.ends_with(".fempak") || ScopedPath.ends_with(".tmp"))
        {
            continue;
        }
        // Do not ship two generations of the same asset. A cooked file next to
        if (ScopedPath.starts_with("loc/") && ScopedPath.ends_with(".fbin"))
        {
            continue;
        }
        const fs::path ScopedParent = ScopedItem.path().parent_path();
        const fs::path ScopedFilename = ScopedItem.path().filename();
        if (ScopedPath.ends_with(".ftex"))
        {
            const bool ScopedHasSource =
                fs::exists(ScopedParent / ReplaceExtension(ScopedFilename.string(), ".png"), ScopedCode) ||
                fs::exists(ScopedParent / ReplaceExtension(ScopedFilename.string(), ".tga"), ScopedCode);
            if (ScopedHasSource)
            {
                continue;
            }
        }
        if (ScopedPath.ends_with(".fpcm"))
        {
            const bool ScopedHasSource =
                fs::exists(ScopedParent / ReplaceExtension(ScopedFilename.string(), ".wav"), ScopedCode) ||
                fs::exists(ScopedParent / ReplaceExtension(ScopedFilename.string(), ".ogg"), ScopedCode) ||
                fs::exists(ScopedParent / ReplaceExtension(ScopedFilename.string(), ".opus"), ScopedCode) ||
                fs::exists(ScopedParent / ReplaceExtension(ScopedFilename.string(), ".flac"), ScopedCode) ||
                fs::exists(ScopedParent / ReplaceExtension(ScopedFilename.string(), ".mp3"), ScopedCode);
            if (ScopedHasSource)
            {
                continue;
            }
        }

        const std::string ScopedRecipe = InferRecipe(ScopedRelative);
        InPlan->files.push_back(
            CookFile{ScopedItem.path(), InferDestination(ScopedRelative, ScopedRecipe), ScopedRecipe});
    }

    std::sort(InPlan->files.begin(), InPlan->files.end(),
              [](const CookFile& InLeft, const CookFile& InRight) { return InLeft.destination < InRight.destination; });
    return true;
}

[[nodiscard]] bool PlanFromManifest(const fs::path& InManifest, const fs::path& InRoot, PackPlan* InPlan,
                                    std::string* InError)
{
    std::string ScopedText;
    if (!ReadTextFile(InManifest, &ScopedText))
    {
        *InError = "cannot read manifest " + InManifest.string();
        return false;
    }

    toml::table ScopedDocument;
    try
    {
        ScopedDocument = toml::parse(ScopedText, InManifest.string());
    }
    catch (const toml::parse_error& ScopedParseError)
    {
        *InError = std::string("manifest parse error: ") + std::string(ScopedParseError.description());
        return false;
    }

    if (const toml::table* ScopedPack = ScopedDocument["pack"].as_table())
    {
        if (const auto* ScopedOutput = (*ScopedPack)["output"].as_string())
        {
            InPlan->output = ScopedOutput->get();
        }
    }

    const auto ScopedResolve = [&InRoot, &InManifest](std::string_view InPath)
    {
        fs::path ScopedCandidate(InPath);
        if (ScopedCandidate.is_absolute())
        {
            return ScopedCandidate;
        }
        // Relative to the manifest first; fall back to the pack root.
        const fs::path ScopedBesideManifest = InManifest.parent_path() / ScopedCandidate;
        std::error_code ScopedCode;
        if (fs::exists(ScopedBesideManifest, ScopedCode))
        {
            return ScopedBesideManifest;
        }
        return InRoot / ScopedCandidate;
    };

    if (const toml::array* ScopedFiles = ScopedDocument["file"].as_array())
    {
        for (const toml::node& ScopedNode : *ScopedFiles)
        {
            const toml::table* ScopedEntry = ScopedNode.as_table();
            if (ScopedEntry == nullptr)
            {
                continue;
            }
            const auto* ScopedPath = (*ScopedEntry)["path"].as_string();
            if (ScopedPath == nullptr)
            {
                *InError = "manifest [[file]] is missing 'path'";
                return false;
            }
            const fs::path ScopedSource = ScopedResolve(ScopedPath->get());
            std::string ScopedRecipe;
            if (const auto* ScopedValue = (*ScopedEntry)["cook"].as_string())
            {
                ScopedRecipe = ScopedValue->get();
            }
            else
            {
                ScopedRecipe = InferRecipe(fs::relative(ScopedSource, InRoot));
            }
            std::string ScopedDestination;
            if (const auto* ScopedValue = (*ScopedEntry)["as"].as_string())
            {
                ScopedDestination = NormalizePakPath(ScopedValue->get());
            }
            else
            {
                ScopedDestination = InferDestination(fs::relative(ScopedSource, InRoot), ScopedRecipe);
            }
            InPlan->files.push_back(CookFile{ScopedSource, ScopedDestination, ScopedRecipe});
        }
    }

    if (const toml::array* ScopedScans = ScopedDocument["scan"].as_array())
    {
        for (const toml::node& ScopedNode : *ScopedScans)
        {
            const toml::table* ScopedEntry = ScopedNode.as_table();
            if (ScopedEntry == nullptr)
            {
                continue;
            }
            const auto* ScopedPath = (*ScopedEntry)["path"].as_string();
            if (ScopedPath == nullptr)
            {
                *InError = "manifest [[scan]] is missing 'path'";
                return false;
            }
            std::string ScopedPrefix;
            if (const auto* ScopedValue = (*ScopedEntry)["as"].as_string())
            {
                ScopedPrefix = NormalizePakPath(ScopedValue->get());
            }
            InPlan->scans.emplace_back(ScopedResolve(ScopedPath->get()), ScopedPrefix);
        }
    }

    if (InPlan->files.empty() && InPlan->scans.empty())
    {
        *InError = "manifest contains no [[file]] or [[scan]] entries";
        return false;
    }
    return true;
}

class ProgressWriter
{
public:
    ProgressWriter() = default;
    ~ProgressWriter()
    {
        Close();
    }

    ProgressWriter(const ProgressWriter&) = delete;
    ProgressWriter& operator=(const ProgressWriter&) = delete;

    void Open(const char* InPath) noexcept
    {
        if (InPath == nullptr || InPath[0] == '\0')
        {
            return;
        }
        MFile = std::fopen(InPath, "wb");
        if (MFile == nullptr)
        {
            return;
        }
        MStart = std::chrono::steady_clock::now();
        MRecord = Fby::Assets::CookProgressRecord{};
        std::memcpy(MRecord.magic, Fby::Assets::kCookProgressMagic, sizeof(MRecord.magic));
        MRecord.version = Fby::Assets::kCookProgressVersion;
        Publish();
    }

    void SetCipher(std::uint32_t InCipher) noexcept
    {
        MCipher = InCipher;
        MEncrypted = InCipher != 0;
    }

    void Update(Fby::Assets::CookProgressStage InStage, std::uint32_t InCompleted, std::uint32_t InTotal,
                std::string_view InAsset, std::uint64_t InRawBytes, std::uint64_t InStoredBytes,
                std::uint32_t InSpeed, const char* InMessage = nullptr) noexcept
    {
        if (MFile == nullptr)
        {
            return;
        }
        MRecord.stage = static_cast<std::uint32_t>(InStage);
        MRecord.completed = InCompleted;
        MRecord.total = InTotal;
        Fby::Assets::CookProgressSetText(MRecord.asset, sizeof(MRecord.asset), InAsset);
        MRecord.rawBytes = InRawBytes;
        MRecord.storedBytes = InStoredBytes;
        MRecord.bytesPerSecond = InSpeed;
        MLastSpeed = InSpeed;
        MRecord.cipher = MCipher;
        const bool ScopedTerminal = InStage == Fby::Assets::CookProgressStage::Succeeded ||
                                    InStage == Fby::Assets::CookProgressStage::Failed ||
                                    InStage == Fby::Assets::CookProgressStage::Cancelled;
        MRecord.flags = (ScopedTerminal ? 0u : Fby::Assets::CookProgressActive) |
                        (MEncrypted ? Fby::Assets::CookProgressEncrypted : 0u) |
                        ((InStage == Fby::Assets::CookProgressStage::Failed) ? Fby::Assets::CookProgressFailed : 0u);
        MRecord.elapsedMicroseconds = ElapsedMicroseconds();
        Fby::Assets::CookProgressSetText(MRecord.message, sizeof(MRecord.message), InMessage);
        Publish();
    }

    void Archive(const Fby::Assets::ArchiveProgress& InProgress) noexcept
    {
        if (MFile == nullptr)
        {
            return;
        }
        if (InProgress.index != MLastIndex)
        {
            MLastIndex = InProgress.index;
            if (InProgress.stage == Fby::Assets::ArchiveProgressStage::Compressing)
            {
                MRawTotal += InProgress.rawBytes;
                MStoredTotal += InProgress.storedBytes;
            }
        }
        if (InProgress.stage != Fby::Assets::ArchiveProgressStage::Compressing)
        {
            MRawTotal = InProgress.rawBytes;
            MStoredTotal = InProgress.storedBytes;
        }
        std::uint32_t ScopedSpeed = 0;
        const std::uint64_t ScopedElapsed = ElapsedMicroseconds();
        if (ScopedElapsed != 0 && MRawTotal != 0)
        {
            const std::uint64_t ScopedRate = (MRawTotal * 1000000ull) / ScopedElapsed;
            ScopedSpeed = ScopedRate > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(ScopedRate);
        }
        Fby::Assets::CookProgressStage ScopedStage = Fby::Assets::CookProgressStage::Compressing;
        if (InProgress.stage == Fby::Assets::ArchiveProgressStage::Encrypting)
        {
            ScopedStage = Fby::Assets::CookProgressStage::Encrypting;
        }
        else if (InProgress.stage == Fby::Assets::ArchiveProgressStage::Done)
        {
            ScopedStage = Fby::Assets::CookProgressStage::Succeeded;
        }
        const char* ScopedMessage = ScopedStage == Fby::Assets::CookProgressStage::Encrypting ? "encrypting"
                                                                                            : "LZMA / FbyLZ";
        Update(ScopedStage, InProgress.index, InProgress.total, InProgress.path, MRawTotal, MStoredTotal, ScopedSpeed,
               ScopedMessage);
    }

    void Complete(std::uint32_t InTotal, std::string_view InAsset) noexcept
    {
        Update(Fby::Assets::CookProgressStage::Succeeded, InTotal, InTotal, InAsset, MRawTotal, MStoredTotal, MLastSpeed,
               "complete");
    }

    void Fail(const char* InMessage) noexcept
    {
        Update(Fby::Assets::CookProgressStage::Failed, MRecord.completed, MRecord.total, MRecord.asset, MRecord.rawBytes,
               MRecord.storedBytes, 0, InMessage);
    }

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return MFile != nullptr;
    }

private:
    [[nodiscard]] std::uint64_t ElapsedMicroseconds() const noexcept
    {
        const auto ScopedNow = std::chrono::steady_clock::now();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(ScopedNow - MStart).count());
    }

    void Publish() noexcept
    {
        if (MFile == nullptr)
        {
            return;
        }
        ++MSequence;
        MRecord.sequence = MSequence;
        (void)std::fseek(MFile, 0, SEEK_SET);
        (void)std::fwrite(&MRecord, sizeof(MRecord), 1, MFile);
        (void)std::fflush(MFile);
    }

    void Close() noexcept
    {
        if (MFile != nullptr)
        {
            (void)std::fclose(MFile);
            MFile = nullptr;
        }
    }

    std::FILE* MFile = nullptr;
    Fby::Assets::CookProgressRecord MRecord{};
    std::chrono::steady_clock::time_point MStart{};
    std::uint32_t MSequence = 0;
    std::uint32_t MLastIndex = 0;
    std::uint32_t MCipher = 0;
    std::uint32_t MLastSpeed = 0;
    std::uint64_t MRawTotal = 0;
    std::uint64_t MStoredTotal = 0;
    bool MEncrypted = false;
};

void ArchiveProgressThunk(void* InUser, const Fby::Assets::ArchiveProgress& InProgress) noexcept
{
    static_cast<ProgressWriter*>(InUser)->Archive(InProgress);
}

[[nodiscard]] bool ParseCookArguments(int InArgc, char** InArgv, CookOptions* InOptions)
{    for (int ScopedI = 2; ScopedI < InArgc; ++ScopedI)
    {
        const std::string_view ScopedArgument = InArgv[ScopedI];
        if (ScopedArgument == "--root" && ScopedI + 1 < InArgc)
        {
            InOptions->root = InArgv[++ScopedI];
        }
        else if (ScopedArgument == "--out" && ScopedI + 1 < InArgc)
        {
            InOptions->output = InArgv[++ScopedI];
        }
        else if (ScopedArgument == "--manifest" && ScopedI + 1 < InArgc)
        {
            InOptions->manifest = InArgv[++ScopedI];
        }
        else if (ScopedArgument == "--target" && ScopedI + 1 < InArgc)
        {
            const std::string ScopedTarget = ToLower(InArgv[++ScopedI]);
            if (ScopedTarget == "desktop")
            {
                InOptions->target = Fby::Cook::CookTarget::Desktop;
            }
            else if (ScopedTarget == "mobile")
            {
                InOptions->target = Fby::Cook::CookTarget::Mobile;
            }
            else
            {
                std::fprintf(stderr, "fempak: --target must be 'desktop' or 'mobile' (got '%s')\n",
                             ScopedTarget.c_str());
                return false;
            }
        }
        else if (ScopedArgument == "--astc" && ScopedI + 1 < InArgc)
        {
            const std::string ScopedBlock = ToLower(InArgv[++ScopedI]);
            if (ScopedBlock == "4x4" || ScopedBlock == "4")
            {
                InOptions->astcBlock = Fby::Cook::AstcBlock::Block4x4;
            }
            else if (ScopedBlock == "6x6" || ScopedBlock == "6")
            {
                InOptions->astcBlock = Fby::Cook::AstcBlock::Block6x6;
            }
            else
            {
                std::fprintf(stderr, "fempak: --astc must be '4x4' or '6x6' (got '%s')\n", ScopedBlock.c_str());
                return false;
            }
        }
        else if (ScopedArgument == "--linear")
        {
            InOptions->srgb = false;
        }
        else if (ScopedArgument == "--progress" && ScopedI + 1 < InArgc)
        {
            InOptions->progressPath = InArgv[++ScopedI];
        }
        else
        {
            std::fprintf(stderr, "unknown argument: %s\n", InArgv[ScopedI]);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RunCook(int InArgc, char** InArgv)
{
    CookOptions ScopedOptions;
    if (!ParseCookArguments(InArgc, InArgv, &ScopedOptions))
    {
        return false;
    }

    std::string ScopedError;
    PackPlan ScopedPlan;
    if (ScopedOptions.manifest.empty())
    {
        if (!PlanFromDirectory(ScopedOptions.root, &ScopedPlan, &ScopedError))
        {
            std::fprintf(stderr, "fempak: %s\n", ScopedError.c_str());
            return false;
        }
    }
    else
    {
        // Scan first, then apply manifest overrides: the writer's "last add
        if (!PlanFromDirectory(ScopedOptions.root, &ScopedPlan, &ScopedError))
        {
            std::fprintf(stderr, "fempak: %s\n", ScopedError.c_str());
            return false;
        }
        if (!PlanFromManifest(ScopedOptions.manifest, ScopedOptions.root, &ScopedPlan, &ScopedError))
        {
            std::fprintf(stderr, "fempak: %s\n", ScopedError.c_str());
            return false;
        }
        if (!ScopedPlan.output.empty())
        {
            ScopedOptions.output = ScopedPlan.output;
        }
    }

    const char* ScopedTargetName = ScopedOptions.target == Fby::Cook::CookTarget::Mobile ? "mobile" : "desktop";
    std::printf("fempak: cooking %zu files (target %s%s)\n", ScopedPlan.files.size(), ScopedTargetName,
                ScopedOptions.srgb ? "" : ", linear");

    ProgressWriter ScopedProgress;
    ScopedProgress.Open(ScopedOptions.progressPath.c_str());
    const auto ScopedTotal = static_cast<std::uint32_t>(ScopedPlan.files.size());
    ScopedProgress.Update(Fby::Assets::CookProgressStage::Planning, 0, ScopedTotal, "planning", 0, 0, 0,
                          "planning");

    FempakWriter ScopedWriter;
    std::size_t ScopedCooked = 0;
    std::uint32_t ScopedIndex = 0;
    std::uint64_t ScopedRawTotal = 0;
    for (const CookFile& ScopedFile : ScopedPlan.files)
    {
        std::error_code ScopedSizeError;
        const auto ScopedFileSize = fs::file_size(ScopedFile.source, ScopedSizeError);
        const std::uint64_t ScopedRawBytes = ScopedSizeError ? 0ull : static_cast<std::uint64_t>(ScopedFileSize);
        ScopedRawTotal += ScopedRawBytes;
        ScopedProgress.Update(Fby::Assets::CookProgressStage::Cooking, ScopedIndex, ScopedTotal,
                              ScopedFile.source.generic_string(), ScopedRawTotal, 0, 0,
                              ScopedFile.recipe.empty() ? "copy" : ScopedFile.recipe.c_str());

        std::vector<std::byte> ScopedBytes;
        if (ScopedFile.recipe == "localization")
        {
            if (!CookLocalization(ScopedFile.source, &ScopedBytes, &ScopedError))
            {
                std::fprintf(stderr, "fempak: %s: %s\n", ScopedFile.source.string().c_str(), ScopedError.c_str());
                return false;
            }
            ++ScopedCooked;
        }
        else if (ScopedFile.recipe == "texture")
        {
            if (!CookTexture(ScopedFile.source, ScopedOptions, &ScopedBytes, &ScopedError))
            {
                std::fprintf(stderr, "fempak: %s: %s\n", ScopedFile.source.string().c_str(), ScopedError.c_str());
                return false;
            }
            ++ScopedCooked;
        }
        else if (ScopedFile.recipe == "audio")
        {
            if (!CookAudio(ScopedFile.source, &ScopedBytes, &ScopedError))
            {
                std::fprintf(stderr, "fempak: %s: %s\n", ScopedFile.source.string().c_str(), ScopedError.c_str());
                return false;
            }
            ++ScopedCooked;
        }
        else if (ScopedFile.recipe == "copy")
        {
            if (!ReadBytes(ScopedFile.source, &ScopedBytes))
            {
                std::fprintf(stderr, "fempak: cannot read %s\n", ScopedFile.source.string().c_str());
                return false;
            }
        }
        else
        {
            std::fprintf(stderr, "fempak: unknown recipe '%s' for %s\n", ScopedFile.recipe.c_str(),
                         ScopedFile.source.string().c_str());
            return false;
        }
        ScopedWriter.Add(ScopedFile.destination, std::move(ScopedBytes));
        ++ScopedIndex;
        ScopedProgress.Update(Fby::Assets::CookProgressStage::Cooking, ScopedIndex, ScopedTotal,
                              ScopedFile.source.generic_string(), ScopedRawTotal, 0, 0, "cooked");
    }

    for (const auto& [ScopedDirectory, ScopedPrefix] : ScopedPlan.scans)
    {
        if (!ScopedWriter.AddDirectory(ScopedDirectory.string(), ScopedPrefix, &ScopedError))
        {
            std::fprintf(stderr, "fempak: %s\n", ScopedError.c_str());
            return false;
        }
    }

    if (ScopedWriter.Empty())
    {
        std::fprintf(stderr, "fempak: nothing to cook under %s\n", ScopedOptions.root.string().c_str());
        return false;
    }

    ScopedProgress.Update(Fby::Assets::CookProgressStage::Packing, ScopedTotal, ScopedTotal, "assets.fempak",
                          ScopedRawTotal, 0, 0, "serializing archive");

    const std::vector<std::byte> ScopedArchive = ScopedWriter.Build(&ScopedError);
    if (ScopedArchive.empty())
    {
        ScopedProgress.Fail(ScopedError.empty() ? "archive build failed" : ScopedError.c_str());
        std::fprintf(stderr, "fempak: %s\n", ScopedError.empty() ? "build failed" : ScopedError.c_str());
        return false;
    }
    if (!Fby::Assets::WriteFileAtomically(ScopedOptions.output.string(), ScopedArchive, &ScopedError))
    {
        ScopedProgress.Fail(ScopedError.c_str());
        std::fprintf(stderr, "fempak: %s\n", ScopedError.c_str());
        return false;
    }

    ScopedProgress.Update(Fby::Assets::CookProgressStage::Succeeded, ScopedTotal, ScopedTotal, "assets.fempak",
                          ScopedRawTotal, ScopedArchive.size(), 0, "cooked");
    std::printf("fempak: %zu entries, %zu bytes, %zu cooked -> %s\n", ScopedWriter.Size(), ScopedArchive.size(),
                ScopedCooked, ScopedOptions.output.string().c_str());
    return true;
}

// Inspection commands. They share the reader logic with the runtime, but do not
[[nodiscard]] std::optional<std::vector<std::byte>> ReadArchive(const char* InPath, std::string* InError)
{
    std::vector<std::byte> ScopedBytes;
    if (!ReadBytes(InPath, &ScopedBytes) || ScopedBytes.size() < sizeof(Fby::Assets::PakHeader))
    {
        *InError = "cannot read archive";
        return std::nullopt;
    }
    return ScopedBytes;
}

struct ListedEntry
{
    std::string path;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

[[nodiscard]] bool ReadIndex(std::span<const std::byte> InBytes, std::vector<ListedEntry>* InEntries,
                             std::string* InError)
{
    using namespace Fby::Assets;
    if (InBytes.size() < sizeof(PakHeader))
    {
        *InError = "archive is smaller than its header";
        return false;
    }
    PakHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kPakMagic, sizeof(kPakMagic)) != 0)
    {
        *InError = "bad magic";
        return false;
    }
    if (ScopedHeader.version != kPakVersion)
    {
        *InError = "unsupported version";
        return false;
    }
    if (!IsRangeInside(ScopedHeader.entryTableOffset,
                       static_cast<std::uint64_t>(ScopedHeader.entryCount) * sizeof(PakEntry), InBytes.size()) ||
        !IsRangeInside(ScopedHeader.stringTableOffset, 0, InBytes.size()) ||
        !IsRangeInside(ScopedHeader.dataOffset, 0, InBytes.size()))
    {
        *InError = "corrupt index";
        return false;
    }
    InEntries->reserve(ScopedHeader.entryCount);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedHeader.entryCount; ++ScopedI)
    {
        PakEntry ScopedEntry{};
        std::memcpy(&ScopedEntry, InBytes.data() + ScopedHeader.entryTableOffset + ScopedI * sizeof(PakEntry),
                    sizeof(ScopedEntry));
        const std::uint64_t ScopedPathPosition = ScopedHeader.stringTableOffset + ScopedEntry.pathOffset;
        if (!IsRangeInside(ScopedPathPosition, ScopedEntry.pathSize, InBytes.size()) ||
            !IsRangeInside(ScopedEntry.offset, ScopedEntry.size, InBytes.size()))
        {
            *InError = "entry points outside the archive";
            return false;
        }
        InEntries->push_back(ListedEntry{
            std::string(reinterpret_cast<const char*>(InBytes.data() + ScopedPathPosition), ScopedEntry.pathSize),
            ScopedEntry.offset, ScopedEntry.size});
    }
    return true;
}

[[nodiscard]] int RunList(int InArgc, char** InArgv)
{
    if (InArgc != 3)
    {
        return 2;
    }
    std::string ScopedError;
    const auto ScopedArchive = ReadArchive(InArgv[2], &ScopedError);
    if (!ScopedArchive)
    {
        std::fprintf(stderr, "fempak: %s\n", ScopedError.c_str());
        return 1;
    }
    std::vector<ListedEntry> ScopedEntries;
    if (!ReadIndex(*ScopedArchive, &ScopedEntries, &ScopedError))
    {
        std::fprintf(stderr, "fempak: %s: %s\n", InArgv[2], ScopedError.c_str());
        return 1;
    }
    for (const ListedEntry& ScopedEntry : ScopedEntries)
    {
        std::printf("%10llu  %s\n", static_cast<unsigned long long>(ScopedEntry.size), ScopedEntry.path.c_str());
    }
    std::printf("%zu entries, %zu bytes\n", ScopedEntries.size(), ScopedArchive->size());
    return 0;
}

[[nodiscard]] bool IsSafeRelativePath(std::string_view InPath)
{
    if (InPath.empty() || InPath.starts_with("/") || InPath.find(':') != std::string_view::npos)
    {
        return false;
    }
    std::size_t ScopedStart = 0;
    while (ScopedStart <= InPath.size())
    {
        const std::size_t ScopedEnd = InPath.find('/', ScopedStart);
        const std::string_view ScopedSegment = InPath.substr(
            ScopedStart, ScopedEnd == std::string_view::npos ? InPath.size() - ScopedStart : ScopedEnd - ScopedStart);
        if (ScopedSegment == "..")
        {
            return false;
        }
        if (ScopedEnd == std::string_view::npos)
        {
            break;
        }
        ScopedStart = ScopedEnd + 1;
    }
    return true;
}

[[nodiscard]] int RunExtract(int InArgc, char** InArgv)
{
    if (InArgc != 4)
    {
        return 2;
    }
    std::string ScopedError;
    const auto ScopedArchive = ReadArchive(InArgv[2], &ScopedError);
    if (!ScopedArchive)
    {
        std::fprintf(stderr, "fempak: %s\n", ScopedError.c_str());
        return 1;
    }
    std::vector<ListedEntry> ScopedEntries;
    if (!ReadIndex(*ScopedArchive, &ScopedEntries, &ScopedError))
    {
        std::fprintf(stderr, "fempak: %s: %s\n", InArgv[2], ScopedError.c_str());
        return 1;
    }

    const fs::path ScopedOutput = InArgv[3];
    std::error_code ScopedCode;
    for (const ListedEntry& ScopedEntry : ScopedEntries)
    {
        if (!IsSafeRelativePath(ScopedEntry.path))
        {
            std::fprintf(stderr, "fempak: refusing unsafe path '%s'\n", ScopedEntry.path.c_str());
            return 1;
        }
        const fs::path ScopedTarget = ScopedOutput / fs::path(ScopedEntry.path);
        fs::create_directories(ScopedTarget.parent_path(), ScopedCode);
        std::ofstream ScopedStream(ScopedTarget, std::ios::binary | std::ios::trunc);
        if (!ScopedStream)
        {
            std::fprintf(stderr, "fempak: cannot write %s\n", ScopedTarget.string().c_str());
            return 1;
        }
        ScopedStream.write(reinterpret_cast<const char*>(ScopedArchive->data() + ScopedEntry.offset),
                           static_cast<std::streamsize>(ScopedEntry.size));
    }
    std::printf("fempak: extracted %zu entries to %s\n", ScopedEntries.size(), ScopedOutput.string().c_str());
    return 0;
}
[[nodiscard]] bool ParseHexBytes(std::string_view InText, std::span<std::byte> OutBytes)
{
    if (InText.size() != OutBytes.size() * 2)
    {
        return false;
    }
    auto ScopedNibble = [](char InValue) -> int
    {
        if (InValue >= '0' && InValue <= '9')
        {
            return InValue - '0';
        }
        if (InValue >= 'a' && InValue <= 'f')
        {
            return InValue - 'a' + 10;
        }
        if (InValue >= 'A' && InValue <= 'F')
        {
            return InValue - 'A' + 10;
        }
        return -1;
    };
    for (std::size_t ScopedI = 0; ScopedI < OutBytes.size(); ++ScopedI)
    {
        const int ScopedHigh = ScopedNibble(InText[ScopedI * 2]);
        const int ScopedLow = ScopedNibble(InText[ScopedI * 2 + 1]);
        if (ScopedHigh < 0 || ScopedLow < 0)
        {
            return false;
        }
        OutBytes[ScopedI] = static_cast<std::byte>((ScopedHigh << 4) | ScopedLow);
    }
    return true;
}

// Wraps one .fempak into the encrypted distribution container. This is the only
[[nodiscard]] bool ReadAeadKeyFile(const char* InPath, Fby::Crypto::AeadKey* OutKey)
{
    if (InPath == nullptr || OutKey == nullptr)
    {
        return false;
    }
    std::FILE* ScopedFile = std::fopen(InPath, "rb");
    if (ScopedFile == nullptr)
    {
        return false;
    }
    std::array<unsigned char, 96> ScopedBytes{};
    const std::size_t ScopedRead = std::fread(ScopedBytes.data(), 1, ScopedBytes.size(), ScopedFile);
    (void)std::fclose(ScopedFile);
    if (ScopedRead == Fby::Crypto::kAeadKeyBytes)
    {
        std::memcpy(OutKey->bytes.data(), ScopedBytes.data(), Fby::Crypto::kAeadKeyBytes);
        return true;
    }
    // Accept a text file too (64 hex digits plus optional whitespace).
    std::string ScopedText;
    ScopedText.reserve(ScopedRead);
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedRead; ++ScopedIndex)
    {
        const char ScopedCharacter = static_cast<char>(ScopedBytes[ScopedIndex]);
        if (ScopedCharacter == ' ' || ScopedCharacter == '\r' || ScopedCharacter == '\n' ||
            ScopedCharacter == '\t')
        {
            continue;
        }
        ScopedText.push_back(ScopedCharacter);
    }
    return ParseHexBytes(ScopedText, OutKey->bytes);
}

[[nodiscard]] int RunSecure(int InArgc, char** InArgv)
{
    std::string ScopedInput;
    std::string ScopedOutput;
    std::string ScopedEntry = "game/assets.fempak";
    std::string ScopedCodecName;
    std::string ScopedCipherName = "chacha";
    std::string ScopedKeyHex;
    std::string ScopedKeyEnv;
    std::string ScopedKeyFile;
    std::string ScopedNonceHex;
    std::string ScopedProgressPath;
    for (int ScopedI = 2; ScopedI < InArgc; ++ScopedI)
    {
        const std::string_view ScopedArgument = InArgv[ScopedI];
        const auto ScopedNext = [&]() -> std::string
        {
            if (ScopedI + 1 >= InArgc)
            {
                return {};
            }
            return InArgv[++ScopedI];
        };
        if (ScopedArgument == "--in")
        {
            ScopedInput = ScopedNext();
        }
        else if (ScopedArgument == "--out")
        {
            ScopedOutput = ScopedNext();
        }
        else if (ScopedArgument == "--entry")
        {
            ScopedEntry = ScopedNext();
        }
        else if (ScopedArgument == "--codec")
        {
            ScopedCodecName = ScopedNext();
        }
        else if (ScopedArgument == "--cipher")
        {
            ScopedCipherName = ScopedNext();
        }
        else if (ScopedArgument == "--key")
        {
            ScopedKeyHex = ScopedNext();
        }
        else if (ScopedArgument == "--key-env")
        {
            ScopedKeyEnv = ScopedNext();
        }
        else if (ScopedArgument == "--key-file")
        {
            ScopedKeyFile = ScopedNext();
        }
        else if (ScopedArgument == "--nonce")
        {
            ScopedNonceHex = ScopedNext();
        }
        else if (ScopedArgument == "--progress")
        {
            ScopedProgressPath = ScopedNext();
        }
        else
        {
            std::fprintf(stderr, "fempak: unknown secure argument '%s'\n", InArgv[ScopedI]);
            return 2;
        }
    }
    if (ScopedInput.empty() || ScopedOutput.empty() || ScopedEntry.empty())
    {
        PrintUsage();
        return 2;
    }
    std::vector<std::byte> ScopedPayload;
    if (!ReadBytes(ScopedInput.c_str(), &ScopedPayload) || ScopedPayload.empty())
    {
        std::fprintf(stderr, "fempak: cannot read %s\n", ScopedInput.c_str());
        return 1;
    }
    std::printf("fempak: secure %s (%zu bytes) -> %s\n", ScopedInput.c_str(), ScopedPayload.size(),
                ScopedOutput.c_str());

    Fby::Assets::ArchiveBuildOptions ScopedOptions{};
    if (ScopedCodecName == "store")
    {
        ScopedOptions.codec = Fby::Assets::ArchiveCodec::Store;
    }
    else if (ScopedCodecName == "lzma")
    {
        ScopedOptions.codec = Fby::Assets::ArchiveCodec::Lzma;
    }
    else
    {
        ScopedOptions.codec = Fby::Assets::LzmaSdkAvailable() ? Fby::Assets::ArchiveCodec::Lzma
                                                              : Fby::Assets::ArchiveCodec::FbyLz;
    }
    if (ScopedCipherName == "none")
    {
        ScopedOptions.cipher = Fby::Assets::ArchiveCipher::None;
    }
    else if (ScopedCipherName == "aes")
    {
        ScopedOptions.cipher = Fby::Assets::ArchiveCipher::Aes256Gcm;
    }
    else
    {
        ScopedOptions.cipher = Fby::Assets::ArchiveCipher::ChaCha20Poly1305;
    }
    if (ScopedKeyHex.empty() && !ScopedKeyEnv.empty())
    {
        const char* ScopedEnvironmentKey = std::getenv(ScopedKeyEnv.c_str());
        if (ScopedEnvironmentKey != nullptr)
        {
            ScopedKeyHex = ScopedEnvironmentKey;
        }
    }
    Fby::Crypto::AeadKey ScopedKey{};
    if (ScopedOptions.cipher != Fby::Assets::ArchiveCipher::None)
    {
        if (ScopedKeyHex.empty() && !ScopedKeyFile.empty())
        {
            if (!ReadAeadKeyFile(ScopedKeyFile.c_str(), &ScopedKey))
            {
                std::fprintf(stderr, "fempak: cannot read key file %s\n", ScopedKeyFile.c_str());
                return 2;
            }
        }
        // A successfully read key file already filled ScopedKey; only the hex value needs parsing.
        const bool ScopedKeyFromFile = ScopedKeyHex.empty() && !ScopedKeyFile.empty();
        if (!ScopedKeyFromFile)
        {
            if (ScopedKeyHex.empty() || !ParseHexBytes(ScopedKeyHex, ScopedKey.bytes))
            {
                std::fprintf(stderr, "fempak: encrypted archives need --key, --key-env or --key-file\n");
                return 2;
            }
        }
        ScopedOptions.key = &ScopedKey;
        if (!ScopedNonceHex.empty() && !ParseHexBytes(ScopedNonceHex, ScopedOptions.nonce.bytes))
        {
            std::fprintf(stderr, "fempak: --nonce must be 24 hex characters\n");
            return 2;
        }
        if (ScopedNonceHex.empty() && !Fby::SecureRng::Fill(ScopedOptions.nonce.bytes))
        {
            std::fprintf(stderr, "fempak: OS entropy source is unavailable\n");
            return 1;
        }
    }
    std::vector<std::byte> ScopedScratch(32u << 20u);
    ScopedOptions.scratch = ScopedScratch;

    ProgressWriter ScopedProgress;
    ScopedProgress.Open(ScopedProgressPath.c_str());
    ScopedProgress.SetCipher(ScopedOptions.cipher == Fby::Assets::ArchiveCipher::Aes256Gcm
                                 ? 2u
                                 : (ScopedOptions.cipher == Fby::Assets::ArchiveCipher::ChaCha20Poly1305 ? 1u : 0u));
    ScopedProgress.Update(Fby::Assets::CookProgressStage::Compressing, 0, 1, ScopedEntry, ScopedPayload.size(), 0, 0,
                          "LZMA");
    ScopedOptions.progress = &ArchiveProgressThunk;
    ScopedOptions.progressUser = &ScopedProgress;

    Fby::Assets::ArchiveInput ScopedEntryInput{ScopedEntry, std::span<const std::byte>(ScopedPayload)};
    std::vector<std::byte> ScopedArchive(Fby::Assets::FemArchiver::MaxArchiveBytes(
        std::span<const Fby::Assets::ArchiveInput>(&ScopedEntryInput, 1), ScopedOptions.codec));
    std::size_t ScopedUsed = 0;
    const Fby::Assets::ArchiveStatus ScopedStatus =
        Fby::Assets::FemArchiver::Build(std::span<const Fby::Assets::ArchiveInput>(&ScopedEntryInput, 1), ScopedArchive,
                                        ScopedOptions, &ScopedUsed);
    if (ScopedStatus != Fby::Assets::ArchiveStatus::Ok)
    {
        ScopedProgress.Fail(Fby::Assets::ArchiveStatusName(ScopedStatus));
        std::fprintf(stderr, "fempak: FemArchiver failed: %s\n", Fby::Assets::ArchiveStatusName(ScopedStatus));
        return 1;
    }
    ScopedArchive.resize(ScopedUsed);
    if (!Fby::Assets::WriteFileAtomically(ScopedOutput, ScopedArchive, nullptr))
    {
        ScopedProgress.Fail("cannot write archive");
        std::fprintf(stderr, "fempak: cannot write %s\n", ScopedOutput.c_str());
        return 1;
    }
    ScopedProgress.Complete(1, ScopedEntry);
    std::printf("fempak: wrote %zu bytes (codec %u, cipher %u)\n", ScopedUsed,
                static_cast<unsigned>(ScopedOptions.codec), static_cast<unsigned>(ScopedOptions.cipher));
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 2;
    }
    const std::string_view ScopedCommand = argv[1];
    if (ScopedCommand == "cook" || ScopedCommand == "pack")
    {
        return RunCook(argc, argv) ? 0 : 1;
    }
    if (ScopedCommand == "list")
    {
        return RunList(argc, argv);
    }
    if (ScopedCommand == "extract")
    {
        return RunExtract(argc, argv);
    }
    if (ScopedCommand == "secure")
    {
        return RunSecure(argc, argv);
    }
    PrintUsage();
    return 2;
}
