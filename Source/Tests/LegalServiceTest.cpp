// FemboyEngine2D - legal manifest and first-run acceptance test.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Legal/LegalService.hpp"

#include <cstdio>
#include <string>

int main()
{
    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    if (!ScopedVfs.MountDirectory("assets", 0, &ScopedError))
    {
        std::fprintf(stderr, "FAIL: mount assets: %s\n", ScopedError.c_str());
        return 1;
    }
    Fby::Legal::LegalService ScopedLegal;
    if (!ScopedLegal.LoadFromVfs(ScopedVfs, "Legal", &ScopedError))
    {
        std::fprintf(stderr, "FAIL: load legal manifest: %s\n", ScopedError.c_str());
        return 1;
    }
    if (ScopedLegal.Manifest().Id != "base-game-eula" || ScopedLegal.Manifest().Documents.size() != 2)
    {
        std::fprintf(stderr, "FAIL: manifest identity or document count\n");
        return 1;
    }
    if (ScopedLegal.Text("en").find("END USER LICENSE AGREEMENT") == std::string_view::npos ||
        ScopedLegal.Text("ru").empty() || ScopedLegal.Text("ru") == ScopedLegal.Text("en"))
    {
        std::fprintf(stderr, "FAIL: localized legal text\n");
        return 1;
    }
    const Fby::Legal::LegalAcceptance ScopedEmpty;
    if (!ScopedLegal.NeedsAcceptance(ScopedEmpty))
    {
        std::fprintf(stderr, "FAIL: empty acceptance must prompt\n");
        return 1;
    }
    const Fby::Legal::LegalAcceptance ScopedAccepted = ScopedLegal.Accept("2026-11-24T00:00:00Z", "ru-RU");
    if (ScopedLegal.NeedsAcceptance(ScopedAccepted) || ScopedAccepted.Language != "ru")
    {
        std::fprintf(stderr, "FAIL: accepted manifest still prompts\n");
        return 1;
    }
    Fby::Legal::LegalAcceptance ScopedVersionChanged = ScopedAccepted;
    ScopedVersionChanged.Version = "2.0.0";
    if (!ScopedLegal.NeedsAcceptance(ScopedVersionChanged))
    {
        std::fprintf(stderr, "FAIL: version change must prompt again\n");
        return 1;
    }
    const std::string ScopedPath = "build/legal_acceptance_test.toml";
    if (!Fby::Legal::LegalService::SaveAcceptance(ScopedPath, ScopedAccepted, &ScopedError))
    {
        std::fprintf(stderr, "FAIL: save acceptance: %s\n", ScopedError.c_str());
        return 1;
    }
    Fby::Legal::LegalAcceptance ScopedLoaded;
    if (!Fby::Legal::LegalService::LoadAcceptance(ScopedPath, &ScopedLoaded, &ScopedError) ||
        ScopedLoaded.ContentHash != ScopedAccepted.ContentHash || ScopedLoaded.Version != "1.0.0")
    {
        std::fprintf(stderr, "FAIL: load acceptance: %s\n", ScopedError.c_str());
        return 1;
    }
    std::printf("legal_service: OK (%zu documents, hash %016llX)\n", ScopedLegal.Manifest().Documents.size(),
                static_cast<unsigned long long>(ScopedLegal.Manifest().ContentHash));
    return 0;
}