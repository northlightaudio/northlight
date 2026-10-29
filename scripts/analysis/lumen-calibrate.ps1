<#
.SYNOPSIS
  Renders Lumen at a reference set's notes and velocities, measures both and
  prints how Lumen differs, by register and strike (docs/plans/electric-piano.md,
  section 4).

.DESCRIPTION
  The reference listing is a CSV of file,note,velocity beside the reference WAVs
  (ep-measure.py's "files" input). Lumen's notes are rendered by the hidden
  "[.calibrate]" unit test of the Release build, each struck and held for 14 s.
  -Set overrides Lumen's parameters for the render ("index=value,...", the
  indices of plugins::lumen::Param), to try a setting without a build.

.EXAMPLE
  .\scripts\analysis\lumen-calibrate.ps1 -Listing H:\Tools\jRhodes3d-wav\jRhodes3d-mono-wav\notes.csv
  .\scripts\analysis\lumen-calibrate.ps1 -Listing <notes.csv> -Set "1=30,8=45"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Listing,
    [string]$Set = '',
    [string]$WorkDir = (Join-Path $env:TEMP 'lumen-calibrate')
)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tests = Join-Path $root 'build\windows-release\tests\unit\northlight_unit_tests.exe'
if (-not (Test-Path $tests)) { throw "Build the Release unit tests first: .\scripts\build.ps1 -Preset windows-release" }
$measure = Join-Path $PSScriptRoot 'ep-measure.py'
$renders = Join-Path $WorkDir 'lumen'
New-Item -ItemType Directory -Force $renders | Out-Null
Get-ChildItem $renders -Filter *.wav | Remove-Item -Force

python -I $measure files $Listing --out (Join-Path $WorkDir 'reference.csv')
if ($LASTEXITCODE -ne 0) { throw 'measuring the reference failed' }
$env:NORTHLIGHT_CALIBRATE_LISTING = $Listing
$env:NORTHLIGHT_CALIBRATE_DIR = $renders
$env:NORTHLIGHT_CALIBRATE_SET = $Set
& $tests '[.calibrate]' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'rendering Lumen failed' }
python -I $measure files (Join-Path $renders 'notes.csv') --out (Join-Path $WorkDir 'lumen.csv')
python -I $measure summary (Join-Path $WorkDir 'reference.csv') (Join-Path $WorkDir 'lumen.csv')
