// A test that hangs, crashes or aborts must say where. CTest only reports "Timeout"
// or "SegFault", and an abort can leave the process waiting on Windows Error
// Reporting until CTest kills it, so this prints the stack of every thread, names
// the test case, and ends the process with a failure at once:
//
// - when one test case runs longer than a test ever should. CTest's own limit is
//   300 s (CMakePresets.json); the default here is 240 s, and
//   NORTHLIGHT_TEST_HANG_SECONDS changes it (0 turns the watchdog off);
// - on a crash (an exception no one handles), after Catch2 has reported it;
// - on abort(), as a failed real-time check does (engine/rt/RealtimeGuard.cpp).
//
// Error dialogs and the report to Windows Error Reporting are turned off, so a
// failing test never waits on either. The report runs after the failure, so it
// touches nothing the other threads might hold beyond what printing a stack needs:
// each thread is suspended only while its own stack is walked.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp and tlhelp32 after windows.h
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <dbghelp.h>
#include <tlhelp32.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#pragma comment(lib, "dbghelp.lib")

namespace
{
constexpr int defaultHangSeconds = 240;
constexpr int maxFrames = 48;

// The running test case: its name, and when it started (0 between test cases).
char runningName[512] = {};
std::atomic<ULONGLONG> runningSince{0};
// Only the first failure reports: a crash inside a report must not start another.
std::atomic<bool> reporting{false};

void print(const char* text)
{
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}

void printFrame(HANDLE process, DWORD64 address)
{
    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 offset = 0;
    char line[1024];
    if (SymFromAddr(process, address, &offset, symbol))
    {
        IMAGEHLP_LINE64 source = {};
        source.SizeOfStruct = sizeof(source);
        DWORD column = 0;
        if (SymGetLineFromAddr64(process, address, &column, &source))
            std::snprintf(
                line, sizeof(line), "    %s  %s(%lu)\n", symbol->Name, source.FileName, source.LineNumber);
        else
            std::snprintf(
                line, sizeof(line), "    %s+0x%llx\n", symbol->Name, static_cast<unsigned long long>(offset));
    }
    else
        std::snprintf(line, sizeof(line), "    0x%016llx\n", static_cast<unsigned long long>(address));
    print(line);
}

// Walks one thread's stack from `context` (which the walk changes).
void walk(HANDLE process, HANDLE thread, CONTEXT context)
{
    STACKFRAME64 frame = {};
#if defined(_M_X64)
    constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_ARM64)
    constexpr DWORD machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context.Pc;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrStack.Offset = context.Sp;
#else
    constexpr DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < maxFrames; ++i)
    {
        if (!StackWalk64(machine,
                         process,
                         thread,
                         &frame,
                         &context,
                         nullptr,
                         SymFunctionTableAccess64,
                         SymGetModuleBase64,
                         nullptr) ||
            frame.AddrPC.Offset == 0)
            break;
        printFrame(process, frame.AddrPC.Offset);
    }
}

void printThreadHeader(DWORD threadId, const char* note)
{
    char header[160];
    std::snprintf(header, sizeof(header), "\n  thread %lu%s:\n", threadId, note);
    print(header);
}

// Another thread: suspended for the walk, then let go.
void printOtherThread(HANDLE process, DWORD threadId)
{
    const HANDLE thread =
        OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
    if (thread == nullptr)
        return;
    printThreadHeader(threadId, "");
    if (SuspendThread(thread) != static_cast<DWORD>(-1))
    {
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &context))
            walk(process, thread, context);
        ResumeThread(thread);
    }
    CloseHandle(thread);
}

// Prints `what`, the failing thread's stack from `context` when there is one, and
// every other thread's; then ends the process with `exitCode`.
[[noreturn]] void report(const char* what, const CONTEXT* context, UINT exitCode)
{
    if (reporting.exchange(true))
    {
        // Another thread is reporting: let it finish.
        Sleep(INFINITE);
    }
    char header[768];
    std::snprintf(header, sizeof(header), "\n*** \"%s\" %s. Every thread's stack:\n", runningName, what);
    print(header);

    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(process, nullptr, TRUE);

    const DWORD self = GetCurrentThreadId();
    if (context != nullptr)
    {
        printThreadHeader(self, " (the one that failed)");
        walk(process, GetCurrentThread(), *context);
    }
    const DWORD processId = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 entry = {};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
            if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != self)
                printOtherThread(process, entry.th32ThreadID);
        CloseHandle(snapshot);
    }
    print("\n*** End of the report.\n");
    TerminateProcess(process, exitCode);
    std::_Exit(static_cast<int>(exitCode));
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* exception)
{
    char what[96];
    std::snprintf(what,
                  sizeof(what),
                  "crashed with exception 0x%08lX",
                  static_cast<unsigned long>(exception->ExceptionRecord->ExceptionCode));
    report(what, exception->ContextRecord, exception->ExceptionRecord->ExceptionCode);
}

void onAbort(int)
{
    CONTEXT context = {};
    RtlCaptureContext(&context);
    report("called abort()", &context, 3);
}

int hangSeconds()
{
    char value[32] = {};
    const DWORD length = GetEnvironmentVariableA("NORTHLIGHT_TEST_HANG_SECONDS", value, sizeof(value));
    if (length == 0 || length >= sizeof(value))
        return defaultHangSeconds;
    return std::atoi(value);
}

void startWatchdog(int seconds)
{
    // Detached: when the process ends the OS ends this thread with it.
    std::thread(
        [seconds]
        {
            const auto limit = static_cast<ULONGLONG>(seconds) * 1000;
            char what[64];
            std::snprintf(what, sizeof(what), "is still running after %d s: it has hung", seconds);
            for (;;)
            {
                Sleep(1000);
                const auto since = runningSince.load(std::memory_order_acquire);
                if (since != 0 && GetTickCount64() - since > limit)
                    report(what, nullptr, 3);
            }
        })
        .detach();
}

// Installed before main, so a failure in any test, or in static set-up, reports.
struct InstallHandlers
{
    InstallHandlers()
    {
        SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        SetUnhandledExceptionFilter(&onCrash);
        std::signal(SIGABRT, &onAbort);
    }
} const installHandlers;

class StackReport final : public Catch::EventListenerBase
{
public:
    using EventListenerBase::EventListenerBase;

    void testCaseStarting(const Catch::TestCaseInfo& info) override
    {
        if (!started_)
        {
            started_ = true;
            if (const int seconds = hangSeconds(); seconds > 0)
                startWatchdog(seconds);
        }
        runningSince.store(0, std::memory_order_release);
        std::snprintf(runningName, sizeof(runningName), "%s", std::string(info.name).c_str());
        runningSince.store(GetTickCount64(), std::memory_order_release);
    }

    void testCaseEnded(const Catch::TestCaseStats&) override
    {
        runningSince.store(0, std::memory_order_release);
    }

private:
    bool started_ = false;
};
} // namespace

CATCH_REGISTER_LISTENER(StackReport)
#endif
