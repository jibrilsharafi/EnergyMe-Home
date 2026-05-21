# manual/check-translations.ps1
#
# Reports whether translated manual files are still in sync with their
# English sources. Each translated file must begin with a comment of the form:
#
#   <!-- translation
#   source: 01-installation.md
#   source-commit: <commit-sha-when-translated>
#   translated: YYYY-MM-DD
#   -->
#
# Usage:
#   pwsh manual/check-translations.ps1
#
# Exit code: 0 if all translations are in sync, 1 if any are stale or missing
# provenance metadata.

$ErrorActionPreference = 'Stop'

$repoRoot = (git rev-parse --show-toplevel).Trim()
$manualDir = Join-Path $repoRoot 'manual'

$languageDirs = Get-ChildItem $manualDir -Directory |
    Where-Object { $_.Name -match '^[a-z]{2}$' }

if (-not $languageDirs) {
    Write-Host "No language subdirectories found under manual/." -ForegroundColor Yellow
    exit 0
}

$drift = @()
$missing = @()

foreach ($langDir in $languageDirs) {
    Write-Host "Checking $($langDir.Name)..." -ForegroundColor Cyan

    Get-ChildItem $langDir.FullName -Filter '*.md' | ForEach-Object {
        $translated = $_
        $content = Get-Content $translated.FullName -Raw

        if ($content -match '<!--\s*translation\s*\r?\n\s*source:\s*(\S+)\s*\r?\n\s*source-commit:\s*([0-9a-f]{7,40})\s*\r?\n') {
            $sourceName = $matches[1]
            $recordedSha = $matches[2]
            $sourcePath = Join-Path $manualDir $sourceName

            if (-not (Test-Path $sourcePath)) {
                $missing += "$($langDir.Name)/$($translated.Name): source $sourceName not found"
                return
            }

            $currentSha = (git log -1 --format=%H -- $sourcePath).Trim()
            if (-not $currentSha) {
                $missing += "$($langDir.Name)/$($translated.Name): cannot resolve current commit for $sourceName"
                return
            }

            if ($currentSha.Substring(0, $recordedSha.Length) -ne $recordedSha) {
                $drift += [pscustomobject]@{
                    Language     = $langDir.Name
                    File         = $translated.Name
                    Source       = $sourceName
                    Recorded     = $recordedSha
                    Current      = $currentSha.Substring(0, 7)
                }
            }
        } else {
            $missing += "$($langDir.Name)/$($translated.Name): no provenance header found"
        }
    }
}

if ($drift) {
    Write-Host ""
    Write-Host "Translations out of sync with English source:" -ForegroundColor Yellow
    $drift | Format-Table -AutoSize | Out-String | Write-Host
}

if ($missing) {
    Write-Host ""
    Write-Host "Translations with missing or unreadable provenance:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" }
}

if (-not $drift -and -not $missing) {
    Write-Host ""
    Write-Host "All translations are in sync." -ForegroundColor Green
    exit 0
}

exit 1
