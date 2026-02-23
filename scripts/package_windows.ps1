param(
  [string]$BuildDir = "builddir",
  [string]$OutDir = "dist",
  [string]$Version = "",
  [string]$Arch = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-Arch {
  param([string]$Raw)
  $value = ($Raw ?? "").Trim().ToLowerInvariant()
  switch ($value) {
    { $_ -in @("amd64", "x86_64", "x64") } { return "x64" }
    { $_ -in @("arm64", "aarch64") } { return "arm64" }
    default { return ($value -ne "" ? $value : "x64") }
  }
}

function Convert-ToMsiVersion {
  param([string]$RawVersion)
  $parts = $RawVersion.Split('.') | ForEach-Object { [int]$_ }
  if ($parts.Count -lt 3) {
    throw "MSI version requires at least 3 numeric segments: $RawVersion"
  }

  $major = [Math]::Min($parts[0], 255)
  $minor = [Math]::Min($parts[1], 255)
  $patch = $parts[2]
  $build = if ($parts.Count -ge 4) { $parts[3] } else { 0 }

  # MSI supports only three version fields; fold nightly build into the third field.
  $combined = ($patch * 1000) + $build
  if ($combined -gt 65535) {
    $combined = ($patch * 100) + ($build % 100)
  }
  $combined = [Math]::Min($combined, 65535)

  return "$major.$minor.$combined"
}

function Resolve-WixTool {
  param([string]$Name)

  $cmd = Get-Command $Name -ErrorAction SilentlyContinue
  if ($cmd) {
    return $cmd.Source
  }

  $common = @(
    "C:\Program Files (x86)\WiX Toolset v3.14\bin\$Name",
    "C:\Program Files (x86)\WiX Toolset v3.11\bin\$Name"
  )
  foreach ($candidate in $common) {
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  return $null
}

function Ensure-WixTools {
  $candle = Resolve-WixTool -Name "candle.exe"
  $light = Resolve-WixTool -Name "light.exe"
  $heat = Resolve-WixTool -Name "heat.exe"
  if ($candle -and $light -and $heat) {
    return @{
      Candle = $candle
      Light = $light
      Heat = $heat
    }
  }

  if (-not (Get-Command choco.exe -ErrorAction SilentlyContinue)) {
    throw "WiX tools are missing and Chocolatey is unavailable. Install WiX Toolset v3."
  }

  Write-Host "Installing WiX Toolset via Chocolatey..." -ForegroundColor Yellow
  choco install wixtoolset --no-progress -y

  $candle = Resolve-WixTool -Name "candle.exe"
  $light = Resolve-WixTool -Name "light.exe"
  $heat = Resolve-WixTool -Name "heat.exe"
  if (-not ($candle -and $light -and $heat)) {
    throw "Unable to locate WiX tools (candle/light/heat) after installation."
  }

  return @{
    Candle = $candle
    Light = $light
    Heat = $heat
  }
}

if (-not $Version) {
  $Version = (Get-Content VERSION).Trim()
}
if (-not $Arch) {
  $Arch = $env:PROCESSOR_ARCHITECTURE
}
$Arch = Normalize-Arch -Raw $Arch

$exe = Join-Path $BuildDir "src\pakfu.exe"
if (-not (Test-Path $exe)) {
  throw "pakfu.exe not found at $exe"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$portableDirName = "pakfu-$Version-windows-$Arch-portable"
$portableDir = Join-Path $OutDir $portableDirName
if (Test-Path $portableDir) {
  Remove-Item -Recurse -Force $portableDir
}
New-Item -ItemType Directory -Force -Path $portableDir | Out-Null

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

Copy-Item $exe (Join-Path $portableDir "pakfu.exe")
& $windeploy.Source --release --compiler-runtime --no-translations --dir $portableDir $exe

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$assets = Join-Path $repoRoot "assets"
if (Test-Path $assets) {
  Copy-Item -Recurse -Force $assets (Join-Path $portableDir "assets")
}

$portableZip = Join-Path $OutDir "pakfu-$Version-windows-$Arch-portable.zip"
if (Test-Path $portableZip) {
  Remove-Item -Force $portableZip
}
Compress-Archive -Path "$portableDir\*" -DestinationPath $portableZip
Write-Host "Packaged portable archive: $portableZip"

$wix = Ensure-WixTools
$msiVersion = Convert-ToMsiVersion -RawVersion $Version
$installerMsi = Join-Path $OutDir "pakfu-$Version-windows-$Arch-installer.msi"
if (Test-Path $installerMsi) {
  Remove-Item -Force $installerMsi
}

$wixWork = Join-Path $OutDir "wix-$Version-$Arch"
if (Test-Path $wixWork) {
  Remove-Item -Recurse -Force $wixWork
}
New-Item -ItemType Directory -Force -Path $wixWork | Out-Null

$productWxs = @'
<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product
    Id="*"
    Name="PakFu"
    Language="1033"
    Version="$(var.ProductVersion)"
    Manufacturer="PakFu"
    UpgradeCode="{B2136F77-3913-43F8-AC3E-9B37631B9F7D}">
    <Package InstallerVersion="500" Compressed="yes" InstallScope="perUser" />
    <MajorUpgrade DowngradeErrorMessage="A newer version of [ProductName] is already installed." />
    <MediaTemplate EmbedCab="yes" />
    <Feature Id="PakFuFeature" Title="PakFu" Level="1">
      <ComponentGroupRef Id="AppFiles" />
    </Feature>
  </Product>

  <Fragment>
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="LocalAppDataFolder">
        <Directory Id="INSTALLFOLDER" Name="PakFu" />
      </Directory>
    </Directory>
  </Fragment>
</Wix>
'@

$productWxsPath = Join-Path $wixWork "Product.wxs"
$appFilesWxsPath = Join-Path $wixWork "AppFiles.wxs"
Set-Content -Path $productWxsPath -Value $productWxs -Encoding UTF8

& $wix.Heat dir $portableDir `
  -nologo `
  -ag `
  -srd `
  -scom `
  -sreg `
  -sfrag `
  -cg AppFiles `
  -dr INSTALLFOLDER `
  -var var.SourceDir `
  -out $appFilesWxsPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Push-Location $wixWork
& $wix.Candle -nologo "-dSourceDir=$portableDir" "-dProductVersion=$msiVersion" Product.wxs AppFiles.wxs
if ($LASTEXITCODE -ne 0) {
  Pop-Location
  exit $LASTEXITCODE
}
& $wix.Light -nologo -spdb -out $installerMsi Product.wixobj AppFiles.wixobj
if ($LASTEXITCODE -ne 0) {
  Pop-Location
  exit $LASTEXITCODE
}
Pop-Location

if (Test-Path $portableDir) {
  Remove-Item -Recurse -Force $portableDir
}
if (Test-Path $wixWork) {
  Remove-Item -Recurse -Force $wixWork
}

Write-Host "Packaged installer: $installerMsi"
