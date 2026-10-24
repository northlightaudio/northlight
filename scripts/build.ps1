<#
.SYNOPSIS
  Configure, build and (optionally) test Northlight from any PowerShell window.

.DESCRIPTION
  Locates Visual Studio with vswhere, imports the x64 developer environment, and
  runs the given CMake preset. Equivalent to running `cmake --preset <name>` from a
  "Developer PowerShell for VS" prompt.

.EXAMPLE
  .\scripts\build.ps1                       # windows-debug, build only
  .\scripts\build.ps1 -Preset windows-release
  .\scripts\build.ps1 -Test                 # build then run ctest
  .\scripts\build.ps1 -Run                  # build then launch northlight.exe
  .\scripts\build.ps1 -Test -LowPriority -Jobs 4   # stay out of the way of a game
  .\scripts\build.ps1 -Preset windows-release -Test -Label perf   # the UI paint-time budgets

.PARAMETER Target
  Build only this CMake target (northlight_juce_tests, Northlight, ...).

.PARAMETER Label
  With -Test, run only the tests with this CTest label (perf: the UI paint-time
  budgets, registered in a Release build; docs/ui-performance.md).

.PARAMETER Jobs
  Compile at most this many files at once. 0 (the default) uses every core.

.PARAMETER LowPriority
  Run the build and tests at Idle priority, so other programs get the CPU first.
  The compilers, linker and test runs inherit it; this shell's priority is put
  back when the script ends.
#>
[CmdletBinding()]
param(
    [ValidateSet('windows-debug', 'windows-relwithdebinfo', 'windows-release', 'ci')]
    [string]$Preset = 'windows-debug',
    [switch]$Test,
    [string]$Target = '',
    [string]$Label = '',
    [switch]$Run,
    [switch]$Clean,
    [ValidateRange(0, 256)]
    [int]$Jobs = 0,
    [switch]$LowPriority
)

# Native tools (git, cmake, ninja) write progress to stderr. Under 'Stop', Windows
# PowerShell 5.1 turns that into a terminating error when output is redirected, so we
# stay on 'Continue' and check $LASTEXITCODE after each native call instead.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; is Visual Studio installed?' }

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'No Visual Studio with the C++ x64 toolset was found.' }

# Import the developer environment into this process.
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
$envDump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "env:$($matches[1])" -Value $matches[2]
    }
}

$buildDir = Join-Path $root "build\$Preset"
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Removing $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

# Windows starts child processes at Idle priority when their parent runs at Idle, so
# lowering this process covers cmake, ninja, cl, link and the test binaries.
$self = Get-Process -Id $PID
$previousPriority = $self.PriorityClass
if ($LowPriority) { $self.PriorityClass = 'Idle' }

$parallel = @('--parallel')
if ($Jobs -gt 0) { $parallel += "$Jobs" }

Push-Location $root
try {
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }

    $targets = @()
    if ($Target) { $targets = @('--target', $Target) }
    cmake --build --preset $Preset @parallel @targets
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

    if ($Test) {
        $labels = @()
        if ($Label) { $labels = @('-L', $Label) }
        ctest --preset $Preset @labels
        if ($LASTEXITCODE -ne 0) { throw "tests failed ($LASTEXITCODE)" }
    }

    if ($Run) {
        # The app itself runs at normal priority: its audio threads need it.
        $self.PriorityClass = $previousPriority
        $exe = Get-ChildItem -Path $buildDir -Recurse -Filter 'Northlight.exe' | Select-Object -First 1
        if (-not $exe) { throw 'Northlight.exe not found in build output' }
        & $exe.FullName
    }
}
finally {
    $self.PriorityClass = $previousPriority
    Pop-Location
}
