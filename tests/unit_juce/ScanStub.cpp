// A stand-in for northlight_plugin_scan that misbehaves on purpose, so the host side
// of the scanner can be tested against a crash and a hang without a broken plugin.
// The mode comes from the NORTHLIGHT_SCAN_STUB_MODE environment variable (the host
// passes only the file on the command line):
//
//   crash   dies with a non-zero exit code (what a crash looks like to the host)
//   hang    never finishes (like a plugin hanging)
//   junk    exits 0 with output that is not a result
//   ok      a well-formed empty result (the default)

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int, char*[])
{
    const char* env = std::getenv("NORTHLIGHT_SCAN_STUB_MODE");
    const char* mode = env != nullptr ? env : "ok";
    if (std::strcmp(mode, "crash") == 0)
        std::_Exit(3); // dies with a non-zero code and no dialog, as the host sees a crash
    if (std::strcmp(mode, "hang") == 0)
        for (;;)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    if (std::strcmp(mode, "junk") == 0)
    {
        std::printf("this is not xml\n");
        return 0;
    }
    std::printf("loading noise from the plugin\n<<NORTHLIGHT-SCAN>>\n<plugins/>\n<<END-NORTHLIGHT-SCAN>>\n");
    return 0;
}
