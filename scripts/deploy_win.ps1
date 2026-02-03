param(
    [string]$BuildDir = "build",
    [string]$ExeName = "pakfu.exe",
    [string]$QtBin = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Find-QtBin {
    param([string]$QtBinOverride)

    if ($QtBinOverride -and (Test-Path $QtBinOverride)) {
        return $QtBinOverride
    }

    $qmake = Get-Command qmake6 -ErrorAction SilentlyContinue
    if ($qmake) {
        return Split-Path $qmake.Path -Parent
    }

    $defaultQt = "C:\Qt\6.10.1\msvc2022_64\bin"
    if (Test-Path $defaultQt) {
        return $defaultQt
    }

    throw "Qt bin directory not found. Provide -QtBin or ensure qmake6 is on PATH."
}

$qtBinPath = Find-QtBin -QtBinOverride $QtBin
$windeployqt = Join-Path $qtBinPath "windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt.exe not found in $qtBinPath"
}

$exePath = Join-Path $BuildDir (Join-Path "src" $ExeName)
if (-not (Test-Path $exePath)) {
    throw "Executable not found: $exePath"
}

Write-Host "Using Qt bin: $qtBinPath"
Write-Host "Deploying: $exePath"

& $windeployqt $exePath
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Write-Host "Deployment complete."

