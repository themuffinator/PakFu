param(
  [string]$BuildDir = "builddir",
  [string]$OutDir = "dist",
  [string]$Version = ""
)

$ErrorActionPreference = "Stop"

if (-not $Version) {
  $Version = (Get-Content VERSION).Trim()
}

$exe = Join-Path $BuildDir "src\\pakfu.exe"
if (!(Test-Path $exe)) {
  throw "pakfu.exe not found at $exe"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$staging = Join-Path $OutDir "pakfu-win64-$Version"
if (Test-Path $staging) {
  Remove-Item -Recurse -Force $staging
}
New-Item -ItemType Directory -Force -Path $staging | Out-Null

$windeploy = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if (-not $windeploy) {
  $ensure = Join-Path $PSScriptRoot "ensure_qt6.ps1"
  if (Test-Path $ensure) {
    Write-Host "windeployqt.exe not found. Attempting Qt6 auto-install..." -ForegroundColor Yellow
    & $ensure
    if ($LASTEXITCODE -ne 0) {
      exit $LASTEXITCODE
    }
    $windeploy = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
  }
  if (-not $windeploy) {
    throw "windeployqt.exe not found on PATH."
  }
}

Copy-Item $exe $staging
& $windeploy.Source --release --compiler-runtime --no-translations --dir $staging $exe

$assets = Join-Path (Get-Location) "assets"
if (Test-Path $assets) {
  Copy-Item -Recurse -Force $assets (Join-Path $staging "assets")
}

$zip = Join-Path $OutDir "pakfu-win64-$Version.zip"
if (Test-Path $zip) {
  Remove-Item $zip
}

Compress-Archive -Path "$staging\\*" -DestinationPath $zip
Write-Host "Packaged: $zip"
