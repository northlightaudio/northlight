<#
.SYNOPSIS
  Runs Northlight's UI frame benchmarks and compares them with this machine's
  baseline (docs/ui-performance.md, layer 3).

.DESCRIPTION
  Each benchmark runs the app in a window (the frames must reach a screen, so not
  --offscreen; do not run it while the machine is busy with a game), prints its
  percentiles and quits. The script collects every "p50 ... p95/p99 ..." line,
  writes them to build/<preset>/perf-results.json, and compares the medians and
  the p95/p99s with perf/baselines/<computer>.<preset>.json (a baseline is only
  comparable with the same build on the same machine): one more than 30% plus
  0.5 ms over its baseline fails the run. Max values are reported, never judged
  (one hitch decides them).

  .\scripts\perf.ps1                          # the ci build, compared with its baseline
  .\scripts\perf.ps1 -Update                  # write this machine's baseline from this run
  .\scripts\perf.ps1 -Preset windows-release  # another build (its exe must exist)
#>
param(
    [string]$Preset = "ci",
    [switch]$Update,
    [double]$Tolerance = 0.30,
    [double]$SlackMs = 0.5
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$config = @{ "windows-release" = "Release"; "windows-relwithdebinfo" = "RelWithDebInfo"; "ci" = "RelWithDebInfo"; "windows-debug" = "Debug" }[$Preset]
$exe = Join-Path $root "build\$Preset\src\app\northlight_artefacts\$config\Northlight.exe"
if (-not (Test-Path $exe)) { throw "No build at $exe; run .\scripts\build.ps1 -Preset $Preset first" }

$work = Join-Path $env:TEMP "northlight-perf"
New-Item -ItemType Directory -Force $work | Out-Null
$demo = Join-Path $work "demo.northlight"
$common = @("--no-splash", "--master-gain", "-120")

# name -> arguments
$benchmarks = [ordered]@{
    "ui"           = @("--demo-song", $demo, "--benchmark-ui", "10")
    "panels"       = @("--demo-song", $demo, "--benchmark-panels", "6")
    "strip-drag"   = @("--demo-song", $demo, "--benchmark-strip-drag", "5")
    "hover-patina" = @("--demo-song", $demo, "--insert", "northlight.patina", "--insert-editor", "--play", "--benchmark-hover", "5")
    "hover-corona" = @("--demo-song", $demo, "--insert", "northlight.amp", "--insert-editor", "--play", "--benchmark-hover", "5")
    "hover-tape"   = @("--demo-song", $demo, "--insert", "northlight.tapeecho", "--insert-editor", "--play", "--benchmark-hover", "5")
    "hover-orbit"  = @("--demo-song", $demo, "--insert", "northlight.orbit", "--insert-editor", "--play", "--benchmark-hover", "5")
    "hover-pedal"  = @("--demo-song", $demo, "--insert", "northlight.flare", "--insert-editor", "--play", "--benchmark-hover", "5")
}

$results = [ordered]@{}
foreach ($name in $benchmarks.Keys) {
    Write-Host "running $name ..."
    # Each in its own process with a time limit, so one that never quits cannot hang the run.
    $log = Join-Path $work "$name.txt"
    $quoted = (@($common) + @($benchmarks[$name]) | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join " "
    $p = Start-Process $exe -ArgumentList $quoted -RedirectStandardOutput $log -PassThru -NoNewWindow
    if (-not $p.WaitForExit(120000)) { Stop-Process -Id $p.Id -Force; Write-Warning "$name did not finish in 2 minutes" }
    $out = if (Test-Path $log) { Get-Content $log -Raw } else { "" }
    foreach ($line in $out -split "`n") {
        if ($line -match '^(?<label>[^:]+):.*?p50 (?<p50>[\d.]+)%? p(?<q>95|99) (?<pq>[\d.]+)%? max (?<max>[\d.]+)') {
            $key = "$name/$($Matches.label.Trim())"
            $results[$key] = [ordered]@{ p50 = [double]$Matches.p50; "p$($Matches.q)" = [double]$Matches.pq; max = [double]$Matches.max }
        }
    }
    if (-not ($results.Keys | Where-Object { $_ -like "$name/*" })) { Write-Warning "$name printed no percentiles:`n$out" }
}

$resultsFile = Join-Path $root "build\$Preset\perf-results.json"
$results | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $resultsFile
$baselineFile = Join-Path $root "perf\baselines\$($env:COMPUTERNAME.ToLower()).$Preset.json"

if ($Update) {
    New-Item -ItemType Directory -Force (Split-Path $baselineFile) | Out-Null
    $results | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $baselineFile
    Write-Host "wrote the baseline $baselineFile"
    $results.GetEnumerator() | ForEach-Object { "{0,-46} {1}" -f $_.Key, (($_.Value.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }) -join "  ") }
    exit 0
}
if (-not (Test-Path $baselineFile)) {
    Write-Warning "No baseline for this machine ($baselineFile): run with -Update to make one. This run:"
    $results.GetEnumerator() | ForEach-Object { "{0,-46} {1}" -f $_.Key, (($_.Value.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }) -join "  ") }
    exit 0
}

$baseline = Get-Content $baselineFile -Raw | ConvertFrom-Json
$failed = 0
foreach ($key in $results.Keys) {
    $now = $results[$key]
    $was = $baseline.$key
    foreach ($metric in $now.Keys) {
        $value = $now[$metric]
        $base = if ($was) { $was.$metric } else { $null }
        $judged = $metric -ne "max" -and $null -ne $base
        $limit = if ($judged) { $base * (1 + $Tolerance) + $SlackMs } else { $null }
        $verdict = if (-not $judged) { "" } elseif ($value -gt $limit) { $failed++; "SLOWER (limit $([math]::Round($limit, 2)))" } else { "ok" }
        "{0,-46} {1,-4} {2,8} (baseline {3,8}) {4}" -f $key, $metric, $value, $(if ($null -ne $base) { $base } else { "-" }), $verdict
    }
}
if ($failed -gt 0) { Write-Error "$failed figure(s) over their baseline; see above (a deliberate change: rerun with -Update and commit the baseline)" }
Write-Output "all within the baseline"
