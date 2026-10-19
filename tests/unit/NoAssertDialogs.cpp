// Unattended test runs must never block on a dialog: route CRT assertion and error
// reports to stderr so a failed assertion aborts the process with a message instead.
#if defined(_MSC_VER)
#include <crtdbg.h>

namespace
{
struct NoAssertDialogs
{
    NoAssertDialogs()
    {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    }
} const noAssertDialogs;
} // namespace
#endif
