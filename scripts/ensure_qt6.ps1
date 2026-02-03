param(
  [string]$QtVersion = "6.10.1",
  [string]$Arch = "win64_msvc2022_64",
  [string]$InstallRoot = "C:\\Qt"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Ensure-Python {
  $py = Get-Command python -ErrorAction SilentlyContinue
  if (-not $py) {
    throw "Python is required to auto-install Qt (aqtinstall). Install Python and re-run."
  }
  return $py.Source
}

function Ensure-AqtInstall {
  param([string]$PythonExe)
  $args = @("-m", "aqt")
  $prev = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  & $PythonExe @args 2>$null | Out-Null
  $exitCode = $LASTEXITCODE
  $ErrorActionPreference = $prev
  if ($exitCode -eq 0) {
    return
  }
  & $PythonExe -m pip install --user --upgrade aqtinstall
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to install aqtinstall via pip."
  }
}

function Get-QmakePath {
  param([string]$Root, [string]$Version, [string]$ArchName)
  $archDir = $ArchName
  if ($archDir.StartsWith("win64_")) {
    $archDir = $archDir.Substring(6)
  } elseif ($archDir.StartsWith("win32_")) {
    $archDir = $archDir.Substring(6)
  }
  $qmake6 = Join-Path $Root (Join-Path $Version (Join-Path $archDir "bin\\qmake6.exe"))
  if (Test-Path $qmake6) {
    return $qmake6
  }
  $qmake = Join-Path $Root (Join-Path $Version (Join-Path $archDir "bin\\qmake.exe"))
  if (Test-Path $qmake) {
    return $qmake
  }
  return $null
}

if ($qmakePath = Get-QmakePath -Root $InstallRoot -Version $QtVersion -ArchName $Arch) {
  Write-Host "Qt $QtVersion ($Arch) already installed: $qmakePath"
  exit 0
}

$pythonExe = Ensure-Python
Ensure-AqtInstall -PythonExe $pythonExe

Write-Host "Installing Qt $QtVersion ($Arch) to $InstallRoot ..."
& $pythonExe -m aqt install-qt windows desktop $QtVersion $Arch -O $InstallRoot -m qtmultimedia qtimageformats
if ($LASTEXITCODE -ne 0) {
  throw "aqtinstall failed with exit code $LASTEXITCODE"
}

if (-not ($qmakePath = Get-QmakePath -Root $InstallRoot -Version $QtVersion -ArchName $Arch)) {
  throw "Qt installation completed but qmake was not found."
}

$archDir = $Arch
if ($archDir.StartsWith("win64_")) {
  $archDir = $archDir.Substring(6)
} elseif ($archDir.StartsWith("win32_")) {
  $archDir = $archDir.Substring(6)
}
$qmake6Path = Join-Path $InstallRoot (Join-Path $QtVersion (Join-Path $archDir "bin\\qmake6.exe"))
if (-not (Test-Path $qmake6Path) -and (Test-Path $qmakePath)) {
  Copy-Item -Force $qmakePath $qmake6Path
  $qmakePath = $qmake6Path
}

Write-Host "Qt installation complete: $qmakePath"
