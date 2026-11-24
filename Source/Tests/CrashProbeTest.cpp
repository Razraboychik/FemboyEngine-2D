// FemboyEngine2D - crash marker probe: a child dies, the parent checks the marker.
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/StoragePaths.hpp"
#include "Engine/Ops/Ops.hpp"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#endif

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
void MarkerHook(const char* InDirectory, const char* InReason) noexcept
{
    Fby::Ops::WriteCrashMarker(InDirectory, InReason);
}
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--child") == 0)
    {
        Fby::Core::InstallCrashHandler("crash_probe");
        Fby::Core::SetCrashIncidentHook(&MarkerHook);
#if defined(_WIN32)
        ::RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#else
        std::raise(SIGSEGV);
#endif
        return 2;
    }
    const std::filesystem::path ScopedStorage = std::filesystem::absolute("build/crash_probe_storage");
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedStorage, ScopedError);
#if defined(_WIN32)
    _putenv_s("FBY_STORAGE_DIR", ScopedStorage.string().c_str());
#else
    setenv("FBY_STORAGE_DIR", ScopedStorage.string().c_str(), 1);
#endif
    const std::string ScopedCommand = std::string("\"") + argv[0] + "\" --child";
    (void)std::system(ScopedCommand.c_str());
    for (const std::filesystem::directory_entry& ScopedEntry :
         std::filesystem::recursive_directory_iterator(Fby::Core::CrashRoot(), ScopedError))
    {
        if (ScopedEntry.is_regular_file() && ScopedEntry.path().filename() == "incident-request.txt")
        {
            return 0;
        }
    }
    return 1;
}