# Fails when src/ui carries a literal colour. The design system (docs/design/design-system.md)
# requires every colour to come from the token file through ui::Theme; this is
# the CI guard. Theme.cpp (the loader) is the one place allowed to build colours.
# Sizes are checked screen by screen as each is restyled (see docs/design/interaction.md).
param([string]$Root = (Join-Path $PSScriptRoot ".."))

$ui = Join-Path $Root "src\ui"
$patterns = @('fromRGB\s*\(', 'Colour\s*\(\s*0x', 'Colours::(?!transparentBlack)', 'fromString\s*\(\s*"#', 'fromFloatRGBA\s*\(')
$failures = @()
Get-ChildItem -Path $ui -Recurse -Include *.cpp, *.h | Where-Object { $_.Name -ne 'Theme.cpp' } | ForEach-Object {
    $file = $_
    $lineNumber = 0
    Get-Content $file.FullName | ForEach-Object {
        $lineNumber++
        foreach ($pattern in $patterns) {
            if ($_ -match $pattern) {
                $failures += ("{0}:{1}: {2}" -f $file.FullName, $lineNumber, $_.Trim())
                break
            }
        }
    }
}
if ($failures.Count -gt 0) {
    Write-Host "Literal colours in src/ui (use ui::Theme tokens):"
    $failures | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "No literal colours in src/ui."
exit 0
