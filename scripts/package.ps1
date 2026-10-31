<#
.SYNOPSIS
  Build Northlight for release and package it as a portable zip and an installer.

.DESCRIPTION
  Builds the `package` preset (Release, warnings as errors) into build\package,
  runs the tests, stages the app folder, optionally signs the executables, then
  writes to build\package\dist:

    Northlight-<version>-x64.zip         the portable app: unzip and run
    Northlight-<version>-Setup-x64.exe   the Inno Setup installer
    SHA256SUMS.txt                       checksums of both

  The installer is made from the same staged (and signed) files as the zip.
  See docs/building.md, "Packaging".

.EXAMPLE
  .\scripts\package.ps1                         # a dev package, unsigned
  .\scripts\package.ps1 -Label ''               # a final release, from a clean tag
  .\scripts\package.ps1 -Label beta.1 -Sign     # a signed pre-release

.PARAMETER Label
  The version label (CONTRIBUTING.md, "Versions and releases"): 'dev' (the
  default), a pre-release such as 'beta.1', or '' for a final release. Anything
  other than 'dev' needs a working tree with no uncommitted changes, so the
  package is exactly the commit it names.

.PARAMETER Sign
  Sign both executables, the installer and its uninstaller with Azure Trusted
  Signing. Needs -SigningDlib and -SigningMetadata (or the environment variables
  of the same names below) and an Azure sign-in that DefaultAzureCredential finds:
  AZURE_TENANT_ID, AZURE_CLIENT_ID and AZURE_CLIENT_SECRET, or `az login`.

.PARAMETER SigningDlib
  Path to Azure.CodeSigning.Dlib.dll from the Trusted Signing client tools.
  Default: $env:NORTHLIGHT_SIGNING_DLIB.

.PARAMETER SigningMetadata
  Path to the metadata JSON naming the Trusted Signing endpoint, account and
  certificate profile. Default: $env:NORTHLIGHT_SIGNING_METADATA.

.PARAMETER SkipBuild
  Package what is already in build\package (after a build of the same commit).

.PARAMETER SkipTests
  Build without running the tests.

.PARAMETER InnoSetup
  Path to ISCC.exe. Default: $env:NORTHLIGHT_INNO_SETUP, then ISCC on PATH, then
  the usual install folders.

.PARAMETER Jobs
  Compile at most this many files at once (build.ps1 -Jobs).

.PARAMETER LowPriority
  Build and test at Idle priority (build.ps1 -LowPriority).
#>
[CmdletBinding()]
param(
    [string]$Label = 'dev',
    [switch]$Sign,
    [string]$SigningDlib = $env:NORTHLIGHT_SIGNING_DLIB,
    [string]$SigningMetadata = $env:NORTHLIGHT_SIGNING_METADATA,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [string]$InnoSetup = $env:NORTHLIGHT_INNO_SETUP,
    [ValidateRange(0, 256)]
    [int]$Jobs = 0,
    [switch]$LowPriority
)

# As in build.ps1: native tools write progress to stderr, so stay on 'Continue'
# and check $LASTEXITCODE after each native call.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build\package'
$appDir = Join-Path $buildDir 'src\app\northlight_artefacts\Release'
$distDir = Join-Path $buildDir 'dist'
$timestampUrl = 'http://timestamp.acs.microsoft.com'

function Find-InnoSetup {
    if ($InnoSetup) {
        if (Test-Path $InnoSetup) { return (Resolve-Path $InnoSetup).Path }
        throw "ISCC.exe not found at $InnoSetup"
    }
    $onPath = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'))
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw 'Inno Setup 6 (ISCC.exe) was not found. Install it, pass -InnoSetup <path to ISCC.exe>, or set NORTHLIGHT_INNO_SETUP.'
}

function Find-SignTool {
    $onPath = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $found = Get-ChildItem -Path $kits -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq 'x64' } |
        Sort-Object { $_.Directory.Parent.Name } -Descending |
        Select-Object -First 1
    if ($found) { return $found.FullName }
    throw 'signtool.exe was not found; install the Windows SDK (it comes with the Visual Studio C++ workload).'
}

# The signtool arguments for Trusted Signing, before the files to sign.
function Get-SignArguments {
    return @('sign', '/fd', 'SHA256', '/tr', $timestampUrl, '/td', 'SHA256',
             '/dlib', $SigningDlib, '/dmdf', $SigningMetadata)
}

function Invoke-Sign([string[]]$files) {
    & $script:signTool @(Get-SignArguments) @files
    if ($LASTEXITCODE -ne 0) { throw "signing failed ($LASTEXITCODE)" }
}

function Assert-Signed([string[]]$files) {
    & $script:signTool verify /pa /q @files
    if ($LASTEXITCODE -ne 0) { throw "signature check failed ($LASTEXITCODE)" }
}

# ---------------------------------------------------------------------------
# Checks before the long build
# ---------------------------------------------------------------------------
$iscc = Find-InnoSetup

if ($Sign) {
    if (-not $SigningDlib -or -not (Test-Path $SigningDlib)) {
        throw 'Signing needs -SigningDlib (or NORTHLIGHT_SIGNING_DLIB): the path to Azure.CodeSigning.Dlib.dll.'
    }
    if (-not $SigningMetadata -or -not (Test-Path $SigningMetadata)) {
        throw 'Signing needs -SigningMetadata (or NORTHLIGHT_SIGNING_METADATA): the Trusted Signing metadata JSON.'
    }
    $SigningDlib = (Resolve-Path $SigningDlib).Path
    $SigningMetadata = (Resolve-Path $SigningMetadata).Path
    $script:signTool = Find-SignTool
}

Push-Location $root
try {
    $commit = (git rev-parse --short=7 HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'git rev-parse failed; is this a git checkout?' }
    $changes = git status --porcelain --untracked-files=no
    if ($Label -ne 'dev' -and $changes) {
        throw "A '$Label' package must be built from a clean commit (it names the source it came from). Commit or stash the changes first, or use -Label dev."
    }
}
finally {
    Pop-Location
}

# ---------------------------------------------------------------------------
# Build and test
# ---------------------------------------------------------------------------
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Preset package -Define "NORTHLIGHT_VERSION_LABEL=$Label" `
        -Jobs $Jobs -LowPriority:$LowPriority
    if (-not $SkipTests) {
        # The paint-time budgets (label perf) are timed against this machine, so
        # they are left to scripts\build.ps1 -Label perf; everything else runs.
        # Tests that run in real time (label paced) need a free core when they
        # wake, so they run one at a time after the rest, as in CI.
        # A failed test runs once more before the package fails. Some JUCE test
        # programs hang now and then while JUCE shuts down (docs/building.md,
        # "Tests"); CI runs every test with no second try, so a hang still shows
        # there, and a test that fails twice still fails the package.
        $retry = @('--repeat', 'until-pass:2')
        $self = Get-Process -Id $PID
        $previousPriority = $self.PriorityClass
        if ($LowPriority) { $self.PriorityClass = 'Idle' }
        Push-Location $root
        try {
            ctest --preset package -LE 'perf|paced' @retry
            if ($LASTEXITCODE -ne 0) { throw "tests failed ($LASTEXITCODE)" }
            ctest --preset package -L paced --parallel 1 @retry
            if ($LASTEXITCODE -ne 0) { throw "paced tests failed ($LASTEXITCODE)" }
        }
        finally {
            $self.PriorityClass = $previousPriority
            Pop-Location
        }
    }
}

# ---------------------------------------------------------------------------
# Version
# ---------------------------------------------------------------------------
$cache = Join-Path $buildDir 'CMakeCache.txt'
if (-not (Test-Path $cache)) { throw "No build in $buildDir; run without -SkipBuild first." }
$cacheText = Get-Content $cache
$number = ($cacheText | Select-String '^CMAKE_PROJECT_VERSION:STATIC=(.*)$').Matches[0].Groups[1].Value
$builtLabel = ($cacheText | Select-String '^NORTHLIGHT_VERSION_LABEL:STRING=(.*)$').Matches[0].Groups[1].Value
if ($builtLabel -ne $Label) {
    throw "build\package was built with label '$builtLabel', not '$Label'; build again without -SkipBuild."
}
# As version::full() writes it, but only a dev build names its commit here: a
# release or pre-release is its tag.
$version = $number
if ($Label) { $version += "-$Label" }
if ($Label -eq 'dev') { $version += "+$commit" }
Write-Host "Packaging Northlight $version"

# ---------------------------------------------------------------------------
# Stage the app folder
# ---------------------------------------------------------------------------
if (-not (Test-Path (Join-Path $appDir 'Northlight.exe'))) { throw "Northlight.exe not found in $appDir" }

$stageRoot = Join-Path $buildDir 'stage'
$stage = Join-Path $stageRoot "Northlight-$version"
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item -Path (Join-Path $appDir '*') -Destination $stage -Recurse -Exclude '*.pdb', '*.ilk', '*.lib', '*.exp'

$executables = @(Get-ChildItem -Path $stage -Filter *.exe -Recurse | ForEach-Object { $_.FullName })
if ($Sign) {
    Write-Host 'Signing the executables'
    Invoke-Sign $executables
    Assert-Signed $executables
}
else {
    Write-Host 'Not signing (pass -Sign to sign)'
}

if (Test-Path $distDir) { Remove-Item -Recurse -Force $distDir }
New-Item -ItemType Directory -Force $distDir | Out-Null

# ---------------------------------------------------------------------------
# Portable zip: one top-level folder, Northlight-<version>
# ---------------------------------------------------------------------------
$zipName = "Northlight-$version-x64.zip"
# Entries are added one by one because Windows PowerShell's CreateFromDirectory
# writes backslashes into entry names, which other unzip tools take as part of
# the file name; the zip format wants forward slashes.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open((Join-Path $distDir $zipName), 'Create')
try {
    $stageParent = (Split-Path -Parent $stage).Length + 1
    foreach ($file in Get-ChildItem -Path $stage -File -Recurse) {
        $entry = $file.FullName.Substring($stageParent).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entry,
            [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
}
finally {
    $zip.Dispose()
}
Write-Host "Wrote $zipName"

# ---------------------------------------------------------------------------
# Installer
# ---------------------------------------------------------------------------
$setupName = "Northlight-$version-Setup-x64"
$icon = Join-Path $buildDir 'src\app\northlight_artefacts\JuceLibraryCode\icon.ico'
if (-not (Test-Path $icon)) { throw "The app icon was not found at $icon" }

$isccArgs = @('/Q',
    "/DAppVersion=$version",
    "/DNumericVersion=$number",
    "/DStageDir=$stage",
    "/DOutputDir=$distDir",
    "/DOutputName=$setupName",
    "/DIconFile=$icon")
if ($Sign) {
    # Inno Setup runs this for the installer and the uninstaller; $q is a quote
    # and $f the quoted file name.
    $signCommand = (@('$q' + $script:signTool + '$q') + (Get-SignArguments | ForEach-Object {
        if ($_ -match '\s') { '$q' + $_ + '$q' } else { $_ } }) + @('$f')) -join ' '
    $isccArgs += '/DSign'
    $isccArgs += "/Snorthlight=$signCommand"
}
$isccArgs += (Join-Path $root 'installer\Northlight.iss')

& $iscc @isccArgs
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed ($LASTEXITCODE)" }
$setup = Join-Path $distDir "$setupName.exe"
if ($Sign) { Assert-Signed @($setup) }
Write-Host "Wrote $setupName.exe"

# ---------------------------------------------------------------------------
# Checksums, in the format `sha256sum -c` reads
# ---------------------------------------------------------------------------
$sums = Get-ChildItem -Path $distDir -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name |
    ForEach-Object { '{0}  {1}' -f (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLowerInvariant(), $_.Name }
[System.IO.File]::WriteAllLines((Join-Path $distDir 'SHA256SUMS.txt'), [string[]]$sums)

Write-Host ''
Write-Host "Done: $distDir"
Get-ChildItem $distDir | ForEach-Object { Write-Host ('  {0,-48} {1,8:N1} MB' -f $_.Name, ($_.Length / 1MB)) }
