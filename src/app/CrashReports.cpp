#include "CrashReports.h"

#include <algorithm>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp after windows.h
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace aural::crash
{

#if defined(_WIN32)
namespace
{
// Everything the handler needs is prepared at install: inside the handler the
// heap may be corrupt, so it only formats into fixed buffers and calls the OS.
wchar_t folderPath[MAX_PATH] = {};
char versionText[64] = {};

bool writeDump(EXCEPTION_POINTERS* exception, wchar_t* outPath, size_t outSize)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    swprintf_s(outPath,
               outSize,
               L"%s\\Aural-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
               folderPath,
               t.wYear,
               t.wMonth,
               t.wDay,
               t.wHour,
               t.wMinute,
               t.wSecond,
               GetCurrentProcessId());
    const HANDLE file =
        CreateFileW(outPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                                                 MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(),
                                      GetCurrentProcessId(),
                                      file,
                                      type,
                                      exception != nullptr ? &info : nullptr,
                                      nullptr,
                                      nullptr);
    CloseHandle(file);
    // The note beside it: the version and the exception code and address.
    wchar_t notePath[MAX_PATH + 8];
    swprintf_s(notePath, MAX_PATH + 8, L"%s.txt", outPath);
    const HANDLE note =
        CreateFileW(notePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (note != INVALID_HANDLE_VALUE)
    {
        char text[256];
        const int n =
            sprintf_s(text,
                      sizeof(text),
                      "Aural %s\r\nexception 0x%08lX at %p\r\n",
                      versionText,
                      exception != nullptr ? exception->ExceptionRecord->ExceptionCode : 0ul,
                      exception != nullptr ? exception->ExceptionRecord->ExceptionAddress : nullptr);
        DWORD written = 0;
        if (n > 0)
            WriteFile(note, text, static_cast<DWORD>(n), &written, nullptr);
        CloseHandle(note);
    }
    return ok != FALSE;
}

LONG WINAPI onUnhandled(EXCEPTION_POINTERS* exception)
{
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1) == 0) // one report, even if several threads fall over
    {
        wchar_t path[MAX_PATH + 64];
        writeDump(exception, path, MAX_PATH + 64);
    }
    return EXCEPTION_CONTINUE_SEARCH; // Windows Error Reporting still sees it
}
} // namespace

void install(const std::filesystem::path& folder, const std::string& version)
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto wide = folder.wstring();
    wcsncpy_s(folderPath, wide.c_str(), _TRUNCATE);
    strncpy_s(versionText, version.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(&onUnhandled);
}

std::filesystem::path writeReportNow(const std::filesystem::path& folder)
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    wcsncpy_s(folderPath, folder.wstring().c_str(), _TRUNCATE);
    wchar_t path[MAX_PATH + 64];
    if (!writeDump(nullptr, path, MAX_PATH + 64))
        return {};
    return std::filesystem::path(path);
}
#else
void install(const std::filesystem::path&, const std::string&) {}
std::filesystem::path writeReportNow(const std::filesystem::path&)
{
    return {};
}
#endif

std::vector<std::filesystem::path> pending(const std::filesystem::path& folder)
{
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec))
        return found;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
        if (entry.is_regular_file(ec) && entry.path().extension() == ".dmp")
            found.push_back(entry.path());
    std::sort(found.begin(),
              found.end(),
              [](const auto& a, const auto& b)
              {
                  std::error_code e;
                  return std::filesystem::last_write_time(a, e) > std::filesystem::last_write_time(b, e);
              });
    return found;
}

void acknowledge(const std::filesystem::path& folder)
{
    std::error_code ec;
    const auto seen = folder / "Seen";
    std::filesystem::create_directories(seen, ec);
    for (const auto& dump : pending(folder))
    {
        std::filesystem::rename(dump, seen / dump.filename(), ec);
        auto note = dump;
        note += ".txt";
        if (std::filesystem::exists(note, ec))
            std::filesystem::rename(note, seen / note.filename(), ec);
    }
}

} // namespace aural::crash
