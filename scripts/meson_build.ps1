param(
  [string]$BuildDir = "build",
  [string]$Backend = "ninja",
  [bool]$DeployQtRuntime = $true
)

$ErrorActionPreference = "Stop"

function Find-Compiler {
  param(
    [string]$Name
  )
  $cmd = Get-Command $Name -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  return $null
}

function Find-QMake {
  if ($env:QMAKE -and (Test-Path $env:QMAKE)) {
    return $env:QMAKE
  }

  $cmd = Get-Command qmake6 -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  $cmd = Get-Command qmake -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $roots = @(
    $env:Qt6_DIR,
    $env:QTDIR,
    $env:QT_DIR,
    "C:\\Qt"
  ) | Where-Object { $_ -and (Test-Path $_) }

  foreach ($root in $roots) {
    $matches = Get-ChildItem -Path $root -Filter qmake6.exe -File -Recurse -Depth 6 -ErrorAction SilentlyContinue
    if ($matches) {
      $preferred = $matches | Where-Object { $_.FullName -match 'msvc' } | Select-Object -First 1
      if ($preferred) { return $preferred.FullName }
      $preferred = $matches | Where-Object { $_.FullName -match 'clang' } | Select-Object -First 1
      if ($preferred) { return $preferred.FullName }
      $preferred = $matches | Where-Object { $_.FullName -match 'mingw' } | Select-Object -First 1
      if ($preferred) { return $preferred.FullName }
      return $matches[0].FullName
    }

    $matches = Get-ChildItem -Path $root -Filter qmake.exe -File -Recurse -Depth 6 -ErrorAction SilentlyContinue
    if ($matches) {
      return $matches[0].FullName
    }
  }

  return $null
}

function Find-WinDeployQt {
  param(
    [string]$QMakePath
  )

  if ($QMakePath -and (Test-Path $QMakePath)) {
    $qtBin = Split-Path -Parent $QMakePath
    $candidate = Join-Path $qtBin "windeployqt.exe"
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  $cmd = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  return $null
}

$qmake = Find-QMake
if ($qmake) {
  $env:QMAKE = $qmake
  Write-Host "Using qmake: $qmake"
} else {
  Write-Host "qmake6 not found. Install Qt6 or set QMAKE/QT_DIR/Qt6_DIR." -ForegroundColor Yellow
}

$desiredCxx = $env:CXX
$desiredCc = $env:CC
if (-not $desiredCxx) { $desiredCxx = Find-Compiler "clang-cl" }
if (-not $desiredCc) { $desiredCc = $desiredCxx }

if ($desiredCxx) {
  # Qt's MSVC builds require an MSVC-compatible toolchain (cl.exe or clang-cl.exe).
  $env:CXX = $desiredCxx
  if ($desiredCc) { $env:CC = $desiredCc }
  Write-Host "Using CXX: $env:CXX"
}

$needsSetup = $false
$wipe = $false
if (!(Test-Path "$BuildDir/meson-info")) {
  $needsSetup = $true
} elseif (!(Test-Path "$BuildDir/build.ninja")) {
  $needsSetup = $true
} elseif ($desiredCxx -and (Test-Path "$BuildDir/meson-info/intro-compilers.json")) {
  try {
    $intro = Get-Content "$BuildDir/meson-info/intro-compilers.json" | ConvertFrom-Json
    $configured = $intro.host.cpp.exelist[0]
    $configuredName = [System.IO.Path]::GetFileName($configured)
    $desiredName = [System.IO.Path]::GetFileName($desiredCxx)
    if ($configuredName -ne $desiredName) {
      Write-Host "Compiler changed ($configuredName -> $desiredName); regenerating build directory..." -ForegroundColor Yellow
      $needsSetup = $true
      $wipe = $true
    }
  } catch {
    # If detection fails, keep going; Meson will report if setup is invalid.
  }
}

if ($needsSetup) {
  if (Test-Path $BuildDir) {
    if ($wipe) {
      & meson setup $BuildDir --backend $Backend --wipe
    } else {
      & meson setup $BuildDir --backend $Backend --wipe
    }
  } else {
    & meson setup $BuildDir --backend $Backend
  }
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

& meson compile -C $BuildDir
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

if ($DeployQtRuntime -and $IsWindows) {
  $exe = Join-Path $BuildDir "src\\pakfu.exe"
  if (Test-Path $exe) {
    $windeployqt = Find-WinDeployQt -QMakePath $qmake
    if ($windeployqt -and (Test-Path $windeployqt)) {
      $deployDir = Join-Path $BuildDir "src"
      Write-Host "Deploying Qt runtime to: $deployDir"
      & $windeployqt --no-translations --dir $deployDir $exe
      if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
      }
    } else {
      Write-Host "windeployqt.exe not found; pakfu.exe may fail to start unless Qt's bin directory is on PATH." -ForegroundColor Yellow
    }
  }
}
