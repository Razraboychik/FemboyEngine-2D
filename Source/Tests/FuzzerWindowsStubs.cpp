// FemboyEngine2D - minimal Windows COE shims for the prebuilt libFuzzer runtime.
//
// The clang 23 Windows fuzzer runtime links UBSan's continue-on-error support,
// which expects the MSVC ASan COE implementation. The settings fuzzer does not
// use continue-on-error, so these no-op definitions satisfy the linker without
// pulling a second sanitizer runtime into the process.
namespace __coe_win
{
bool ContinueOnError()
{
    return false;
}

void RawWrite(const char*)
{
}
}