<#
.SYNOPSIS
  Configure, build and (optionally) test Aural from any PowerShell window.

.DESCRIPTION
  Locates Visual Studio with vswhere, imports the x64 developer environment, and
  runs the given CMake preset. Equivalent to running `cmake --preset <name>` from a
  "Developer PowerShell for VS" prompt.

.EXAMPLE
  .\scripts\build.ps1                       # windows-debug, build only
  .\scripts\build.ps1 -Preset windows-release
  .\scripts\build.ps1 -Test                 # build then run ctest
  .\scripts\build.ps1 -Run                  # build then launch aural.exe
#>
[CmdletBinding()]
param(
    [ValidateSet('windows-debug', 'windows-relwithdebinfo', 'windows-release', 'ci')]
    [string]$Preset = 'windows-debug',
    [switch]$Test,
    [switch]$Run,
    [switch]$Clean
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

Push-Location $root
try {
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }

    cmake --build --preset $Preset --parallel
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

    if ($Test) {
        ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "tests failed ($LASTEXITCODE)" }
    }

    if ($Run) {
        $exe = Get-ChildItem -Path $buildDir -Recurse -Filter 'Aural.exe' | Select-Object -First 1
        if (-not $exe) { throw 'Aural.exe not found in build output' }
        & $exe.FullName
    }
}
finally {
    Pop-Location
}
