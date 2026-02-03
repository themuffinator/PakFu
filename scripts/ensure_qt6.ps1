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
  & $PythonExe @args 2>$null
  if ($LASTEXITCODE -eq 0) {
    return
  }
  & $PythonExe -m pip install --user --upgrade aqtinstall
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to install aqtinstall via pip."
  }
}

function QtInstalled {
  param([string]$Root, [string]$Version, [string]$ArchName)
  $qmake = Join-Path $Root (Join-Path $Version (Join-Path $ArchName "bin\\qmake6.exe"))
  return Test-Path $qmake
}

if (QtInstalled -Root $InstallRoot -Version $QtVersion -ArchName $Arch) {
  Write-Host "Qt $QtVersion ($Arch) already installed at $InstallRoot."
  exit 0
}

$pythonExe = Ensure-Python
Ensure-AqtInstall -PythonExe $pythonExe

Write-Host "Installing Qt $QtVersion ($Arch) to $InstallRoot ..."
& $pythonExe -m aqt install-qt windows desktop $QtVersion $Arch -O $InstallRoot -m qtbase qtmultimedia qtsvg qtimageformats
if ($LASTEXITCODE -ne 0) {
  throw "aqtinstall failed with exit code $LASTEXITCODE"
}

if (-not (QtInstalled -Root $InstallRoot -Version $QtVersion -ArchName $Arch)) {
  throw "Qt installation completed but qmake6.exe was not found."
}

Write-Host "Qt installation complete."

