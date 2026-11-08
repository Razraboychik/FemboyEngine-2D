// FemboyEngine2D - TXT document slots, pagination and VFS error paths.
#include "Engine/Documents/Document.hpp"

#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/ThreadAffinity.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

void TestTextDocument()
{
    const std::filesystem::path ScopedRoot = std::filesystem::current_path() / "_fby_document_test";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot, ScopedError);

    {
        std::ofstream ScopedFile(ScopedRoot / "book.txt", std::ios::binary | std::ios::trunc);
        const char ScopedBody[] = "\xEF\xBB\xBFTitle\r\nFirst line\r\nSecond line\r\nThird line";
        ScopedFile.write(ScopedBody, static_cast<std::streamsize>(sizeof(ScopedBody) - 1));
    }
    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedVfsError;
    FBY_CHECK(ScopedVfs.MountDirectory(ScopedRoot.string(), 0, &ScopedVfsError));

    Fby::Documents::DocumentSystem ScopedDocuments;
    ScopedDocuments.SetVfs(&ScopedVfs);
    ScopedDocuments.SetLinesPerPage(2);

    Fby::Documents::DocumentError ScopedErrorCode = Fby::Documents::DocumentError::None;
    const Fby::Documents::DocumentHandle ScopedHandle = ScopedDocuments.Open("book.txt", &ScopedErrorCode);
    FBY_CHECK(ScopedHandle.IsValid());
    FBY_CHECK(ScopedErrorCode == Fby::Documents::DocumentError::None);

    Fby::Documents::DocumentInfo ScopedInfo{};
    FBY_CHECK(ScopedDocuments.GetInfo(ScopedHandle, &ScopedInfo));
    FBY_CHECK(ScopedInfo.PageCount == 2);
    FBY_CHECK(ScopedInfo.LineCount == 4);
    FBY_CHECK(std::strcmp(ScopedInfo.Title.data(), "Title") == 0);

    std::array<char, 128> ScopedPage{};
    std::uint32_t ScopedBytes = 0;
    FBY_CHECK(ScopedDocuments.PageText(ScopedHandle, 0, ScopedPage, &ScopedBytes));
    FBY_CHECK(ScopedBytes > 0);
    FBY_CHECK(std::string_view(ScopedPage.data(), ScopedBytes).find("First line") != std::string_view::npos);
    FBY_CHECK(std::string_view(ScopedPage.data(), ScopedBytes).find("\r") == std::string_view::npos);

    std::array<char, 4> ScopedTiny{};
    ScopedBytes = 0;
    FBY_CHECK(ScopedDocuments.PageText(ScopedHandle, 0, ScopedTiny, &ScopedBytes));
    FBY_CHECK(ScopedBytes > ScopedTiny.size());

    FBY_CHECK(!ScopedDocuments.PageText(ScopedHandle, 2, ScopedPage, &ScopedBytes));
    FBY_CHECK(ScopedDocuments.FindPage(ScopedHandle, "Third") == 1);
    FBY_CHECK(ScopedDocuments.FindPage(ScopedHandle, "missing") == Fby::Documents::kInvalidDocumentIndex);

    const Fby::Documents::DocumentHandle ScopedStale = ScopedHandle;
    FBY_CHECK(ScopedDocuments.Close(ScopedHandle));
    FBY_CHECK(!ScopedDocuments.GetInfo(ScopedStale, &ScopedInfo));

    // A reused slot gets a new generation; the stale handle stays invalid.
    const Fby::Documents::DocumentHandle ScopedReopened = ScopedDocuments.Open("book.txt", &ScopedErrorCode);
    FBY_CHECK(ScopedReopened.IsValid());
    FBY_CHECK(ScopedReopened.Index == ScopedStale.Index);
    FBY_CHECK(ScopedReopened.Generation != ScopedStale.Generation);
    FBY_CHECK(!ScopedDocuments.GetInfo(ScopedStale, &ScopedInfo));
    FBY_CHECK(ScopedDocuments.Close(ScopedReopened));

    // Error paths.
    FBY_CHECK(!ScopedDocuments.Open("missing.txt", &ScopedErrorCode).IsValid());
    FBY_CHECK(ScopedErrorCode == Fby::Documents::DocumentError::NotFound);
    FBY_CHECK(!ScopedDocuments.Open("book.pdf", &ScopedErrorCode).IsValid());
    FBY_CHECK(ScopedErrorCode == Fby::Documents::DocumentError::Unsupported);
    FBY_CHECK(!ScopedDocuments.Open("book.bin", &ScopedErrorCode).IsValid());
    FBY_CHECK(ScopedErrorCode == Fby::Documents::DocumentError::Unsupported);

    std::array<Fby::Documents::DocumentHandle, Fby::Documents::kMaxDocuments> ScopedHandles{};
    for (Fby::Documents::DocumentHandle& ScopedItem : ScopedHandles)
    {
        ScopedItem = ScopedDocuments.Open("book.txt", &ScopedErrorCode);
        FBY_CHECK(ScopedItem.IsValid());
    }
    FBY_CHECK(!ScopedDocuments.Open("book.txt", &ScopedErrorCode).IsValid());
    FBY_CHECK(ScopedErrorCode == Fby::Documents::DocumentError::TooManyOpen);
    for (const Fby::Documents::DocumentHandle ScopedItem : ScopedHandles)
    {
        FBY_CHECK(ScopedDocuments.Close(ScopedItem));
    }

    std::filesystem::remove_all(ScopedRoot, ScopedError);
}
}

int main()
{
    Fby::BindThreadRole(Fby::ThreadRole::Document);
    FBY_CHECK(Fby::CurrentThreadRole() == Fby::ThreadRole::Document);
    FBY_CHECK(std::string_view(Fby::ThreadRoleName(Fby::ThreadRole::Document)) == "Document");
    FBY_CHECK(Fby::RequireThreadRole(Fby::ThreadRole::Document, "document test"));
    Fby::BindThreadRole(Fby::ThreadRole::Unknown);
    FBY_CHECK(Fby::RequireMainThread("document test"));
    TestTextDocument();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u document test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("document tests passed\n");
    return 0;
}
