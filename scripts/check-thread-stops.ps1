# Fails when code in src can kill a thread. juce::Thread::stopThread with a time
# limit kills the thread when it misses it, and juce::ThreadPool's destructor gives
# each thread 500 ms. A thread killed while it holds a lock leaves it held for
# ever; killed while it starts or ends, it holds Windows' loader lock, and every
# later DLL load or unload (quitting included) waits for ever. Stop threads with
# stopThread(-1) and use engine::BackgroundQueue for background jobs
# (docs/decisions.md).
param([string]$Root = (Join-Path $PSScriptRoot ".."))

$src = Join-Path $Root "src"
$patterns = @('stopThread\s*\(\s*[0-9]', 'juce::ThreadPool\b', 'TerminateThread\s*\(', 'killThread\s*\(')
$failures = @()
Get-ChildItem -Path $src -Recurse -Include *.cpp, *.h | ForEach-Object {
    $file = $_
    $lineNumber = 0
    Get-Content $file.FullName | ForEach-Object {
        $lineNumber++
        $code = ($_ -split '//', 2)[0] # comments may name what they avoid
        foreach ($pattern in $patterns) {
            if ($code -match $pattern) {
                $failures += ("{0}:{1}: {2}" -f $file.FullName, $lineNumber, $_.Trim())
                break
            }
        }
    }
}
if ($failures.Count -gt 0) {
    Write-Host "Code that can kill a thread (use stopThread(-1) or engine::BackgroundQueue):"
    $failures | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "Nothing in src kills a thread."
exit 0
