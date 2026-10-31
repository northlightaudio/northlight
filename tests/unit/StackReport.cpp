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
// failing test never waits on either.
//
// A hung thread may hold a lock the symbol library needs (the loader's or the
// heap's): on CI, loading symbols once blocked for the rest of the run. So the
// report comes in two passes. The first uses only the system's unwinder and a list
// of modules taken when the first test started, and prints each frame as
// module+offset (llvm-symbolizer turns that into a line, given the executable and
// its PDB). The second names the functions and lines with dbghelp on a thread of
// its own; if that blocks, the process ends anyway after symbolSeconds.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp, psapi and tlhelp32 after windows.h
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

namespace
{
constexpr int defaultHangSeconds = 240;
constexpr DWORD symbolSeconds = 30;
constexpr int maxFrames = 48;
constexpr int maxThreads = 128;
constexpr int maxModules = 256;

// The running test case: its name, and when it started (0 between test cases).
char runningName[512] = {};
std::atomic<ULONGLONG> runningSince{0};
// Only the first failure reports: a crash inside a report must not start another.
std::atomic<bool> reporting{false};

// The modules loaded when the first test started, so the first pass names a frame's
// module without asking the loader.
struct Module
{
    DWORD64 base = 0;
    DWORD64 size = 0;
    char name[64] = {};
};
Module modules[maxModules];
int moduleCount = 0;

// Every walked thread's frames, kept for the second pass.
struct Stack
{
    DWORD threadId = 0;
    bool failed = false; // the thread that crashed or aborted
    int count = 0;
    DWORD64 frames[maxFrames] = {};
};
Stack stacks[maxThreads];
int stackCount = 0;

void print(const char* text)
{
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}

void listModules()
{
    HMODULE handles[maxModules];
    DWORD bytes = 0;
    if (!EnumProcessModules(GetCurrentProcess(), handles, sizeof(handles), &bytes))
        return;
    const int count = std::min(maxModules, static_cast<int>(bytes / sizeof(HMODULE)));
    for (int i = 0; i < count; ++i)
    {
        MODULEINFO info = {};
        if (!GetModuleInformation(GetCurrentProcess(), handles[i], &info, sizeof(info)))
            continue;
        auto& m = modules[moduleCount++];
        m.base = reinterpret_cast<DWORD64>(info.lpBaseOfDll);
        m.size = info.SizeOfImage;
        GetModuleBaseNameA(GetCurrentProcess(), handles[i], m.name, sizeof(m.name));
    }
}

void printRawFrame(DWORD64 address)
{
    char line[160];
    for (int i = 0; i < moduleCount; ++i)
        if (address >= modules[i].base && address < modules[i].base + modules[i].size)
        {
            std::snprintf(line,
                          sizeof(line),
                          "    %s+0x%llx\n",
                          modules[i].name,
                          static_cast<unsigned long long>(address - modules[i].base));
            print(line);
            return;
        }
    std::snprintf(line, sizeof(line), "    0x%016llx\n", static_cast<unsigned long long>(address));
    print(line);
}

// Walks a stack from `context` with the system's unwinder alone (no dbghelp).
void unwind(CONTEXT context, Stack& stack)
{
#if defined(_M_X64)
    for (stack.count = 0; stack.count < maxFrames && context.Rip != 0; ++stack.count)
    {
        stack.frames[stack.count] = context.Rip;
        DWORD64 imageBase = 0;
        if (const auto* function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr))
        {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,
                             imageBase,
                             context.Rip,
                             const_cast<PRUNTIME_FUNCTION>(function),
                             &context,
                             &handlerData,
                             &establisher,
                             nullptr);
        }
        else
        {
            // A leaf function: the return address is on top of the stack.
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += 8;
        }
    }
#else
    (void)context;
    stack.count = 0;
#endif
}

void printRawStack(const Stack& stack)
{
    char header[96];
    std::snprintf(header,
                  sizeof(header),
                  "\n  thread %lu%s:\n",
                  stack.threadId,
                  stack.failed ? " (the one that failed)" : "");
    print(header);
    for (int i = 0; i < stack.count; ++i)
        printRawFrame(stack.frames[i]);
}

// Another thread: suspended for the walk, then let go.
void walkOtherThread(DWORD threadId)
{
    if (stackCount >= maxThreads)
        return;
    const HANDLE thread =
        OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
    if (thread == nullptr)
        return;
    auto& stack = stacks[stackCount];
    stack.threadId = threadId;
    if (SuspendThread(thread) != static_cast<DWORD>(-1))
    {
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &context))
            unwind(context, stack);
        ResumeThread(thread);
        ++stackCount;
    }
    CloseHandle(thread);
}

// The second pass: names each frame kept by the first.
DWORD WINAPI symbolise(void*)
{
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    if (!SymInitialize(process, nullptr, TRUE))
        return 0;
    print("\n*** The same stacks with names:\n");
    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    for (int s = 0; s < stackCount; ++s)
    {
        char header[96];
        std::snprintf(header,
                      sizeof(header),
                      "\n  thread %lu%s:\n",
                      stacks[s].threadId,
                      stacks[s].failed ? " (the one that failed)" : "");
        print(header);
        for (int i = 0; i < stacks[s].count; ++i)
        {
            const auto address = stacks[s].frames[i];
            std::memset(storage, 0, sizeof(storage));
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
                    std::snprintf(line,
                                  sizeof(line),
                                  "    %s  %s(%lu)\n",
                                  symbol->Name,
                                  source.FileName,
                                  source.LineNumber);
                else
                    std::snprintf(line,
                                  sizeof(line),
                                  "    %s+0x%llx\n",
                                  symbol->Name,
                                  static_cast<unsigned long long>(offset));
                print(line);
            }
            else
                printRawFrame(address);
        }
    }
    return 0;
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
    std::snprintf(header,
                  sizeof(header),
                  "\n*** \"%s\" %s. Every thread's stack (module+offset):\n",
                  runningName,
                  what);
    print(header);

    const DWORD self = GetCurrentThreadId();
    if (context != nullptr)
    {
        auto& stack = stacks[stackCount++];
        stack.threadId = self;
        stack.failed = true;
        unwind(*context, stack);
    }
    const DWORD processId = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 entry = {};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
            if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != self)
                walkOtherThread(entry.th32ThreadID);
        CloseHandle(snapshot);
    }
    for (int s = 0; s < stackCount; ++s)
        printRawStack(stacks[s]);

    // Names, if the symbol library can load them in time.
    if (const HANDLE namer = CreateThread(nullptr, 0, &symbolise, nullptr, 0, nullptr))
    {
        if (WaitForSingleObject(namer, symbolSeconds * 1000) == WAIT_TIMEOUT)
            print("\n*** Naming the frames did not finish: a hung thread holds a lock the symbol library "
                  "needs.\n");
        CloseHandle(namer);
    }
    print("\n*** End of the report.\n");
    TerminateProcess(GetCurrentProcess(), exitCode);
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

// Returns once the watchdog runs. A thread still starting holds the loader's lock,
// and a test that suspends every other thread (MiniDumpWriteDump on its own process
// does) would then wait on that lock for ever, with the watchdog frozen too: on CI
// the crash report test once timed out that way, with no report.
void startWatchdog(int seconds)
{
    const HANDLE running = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // Detached: when the process ends the OS ends this thread with it.
    std::thread(
        [seconds, running]
        {
            const auto limit = static_cast<ULONGLONG>(seconds) * 1000;
            char what[64];
            std::snprintf(what, sizeof(what), "is still running after %d s: it has hung", seconds);
            SetEvent(running);
            for (;;)
            {
                Sleep(1000);
                const auto since = runningSince.load(std::memory_order_acquire);
                if (since != 0 && GetTickCount64() - since > limit)
                    report(what, nullptr, 3);
            }
        })
        .detach();
    // Closed only once set: after a timeout the thread may still set it.
    if (running != nullptr && WaitForSingleObject(running, 10 * 1000) == WAIT_OBJECT_0)
        CloseHandle(running);
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
            listModules();
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
