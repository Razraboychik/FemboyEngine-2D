// FemboyEngine2D - test helper for the voice pack protocol.
//
// It records every command it receives (optionally into --log <path>) and
// consumes length-prefixed SPEAK payloads, so the engine test can assert the
// pipe contract on any platform without an audio device.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    const char* ScopedLog = nullptr;
    std::string ScopedModelLog;
    for (int ScopedIndex = 1; ScopedIndex < argc; ++ScopedIndex)
    {
        if (std::strcmp(argv[ScopedIndex], "--log") == 0 && ScopedIndex + 1 < argc)
        {
            ScopedLog = argv[++ScopedIndex];
        }
        else if (std::strcmp(argv[ScopedIndex], "--model") == 0 && ScopedIndex + 1 < argc)
        {
            ScopedModelLog = std::string(argv[++ScopedIndex]) + ".log";
        }
    }
    std::FILE* ScopedOutput = ScopedLog != nullptr
                                  ? std::fopen(ScopedLog, "wb")
                                  : (ScopedModelLog.empty() ? stdout : std::fopen(ScopedModelLog.c_str(), "wb"));
    if (ScopedOutput == nullptr)
    {
        return 1;
    }

    char ScopedLine[4096];
    while (std::fgets(ScopedLine, sizeof(ScopedLine), stdin) != nullptr)
    {
        std::fputs(ScopedLine, ScopedOutput);
        std::fflush(ScopedOutput);
        if (std::strncmp(ScopedLine, "SPEAK ", 6) == 0)
        {
            const long long ScopedLength = std::atoll(ScopedLine + 6);
            if (ScopedLength < 0)
            {
                break;
            }
            std::string ScopedText(static_cast<std::size_t>(ScopedLength), '\0');
            if (ScopedLength > 0 && std::fread(&ScopedText[0], 1, static_cast<std::size_t>(ScopedLength), stdin) !=
                                        static_cast<std::size_t>(ScopedLength))
            {
                break;
            }
            std::fwrite(ScopedText.data(), 1, ScopedText.size(), ScopedOutput);
            std::fputc('\n', ScopedOutput);
            (void)std::fgetc(stdin);
            std::fflush(ScopedOutput);
        }
        if (std::strncmp(ScopedLine, "QUIT", 4) == 0)
        {
            break;
        }
    }

    if (ScopedOutput != stdout)
    {
        std::fclose(ScopedOutput);
    }
    return 0;
}
