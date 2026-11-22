// FemboyEngine2D - runs the editor through its automation hooks and checks
// that startup, project open, frame loop and shutdown all reached completion.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "editor_smoke_runner: expected editor path and project path\n");
        return 1;
    }
    const std::string ScopedEditor = argv[1];
    const std::string ScopedProject = argv[2];
    const std::string ScopedLog = argv[3];
    std::remove(ScopedLog.c_str());

    std::string ScopedCommand;
#if defined(_WIN32)
    ScopedCommand = "cmd /c \"set FBY_EDITOR_QUIT_AFTER_FRAMES=5&& set FBY_EDITOR_SMOKE_PROJECT=" +
                    ScopedProject + "&& \"" + ScopedEditor + "\" > \"" + ScopedLog +
                    "\" 2>&1\"";
#else
    ScopedCommand = "FBY_EDITOR_QUIT_AFTER_FRAMES=5 FBY_EDITOR_SMOKE_PROJECT=\"" + ScopedProject +
                    "\" \"" + ScopedEditor + "\" > \"" + ScopedLog + "\" 2>&1";
#endif
    const int ScopedResult = std::system(ScopedCommand.c_str());
    std::ifstream ScopedStream(ScopedLog);
    const std::string ScopedOutput((std::istreambuf_iterator<char>(ScopedStream)), std::istreambuf_iterator<char>());
    const bool ScopedStarted = ScopedOutput.find("auto-quit after 5 frames") != std::string::npos;
    const bool ScopedStopped = ScopedOutput.find("shutdown complete") != std::string::npos;
    const bool ScopedOpened = ScopedOutput.find("smoke: project opened") != std::string::npos;
    if (!ScopedStarted || !ScopedStopped || !ScopedOpened)
    {
        std::fprintf(stderr, "editor_smoke_runner: shell=%d started=%d project=%d stopped=%d\n", ScopedResult,
                     ScopedStarted ? 1 : 0, ScopedOpened ? 1 : 0, ScopedStopped ? 1 : 0);
        return 1;
    }
    std::printf("editor_smoke: started, opened the project, rendered five frames and stopped\n");
    return 0;
}
