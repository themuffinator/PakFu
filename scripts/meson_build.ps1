param(
  [string]$BuildDir = "build",
  [string]$Backend = "ninja"
)

$ErrorActionPreference = "Stop"

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

$qmake = Find-QMake
if ($qmake) {
  $env:QMAKE = $qmake
  Write-Host "Using qmake: $qmake"
} else {
  Write-Host "qmake6 not found. Install Qt6 or set QMAKE/QT_DIR/Qt6_DIR." -ForegroundColor Yellow
}

$needsSetup = $false
if (!(Test-Path "$BuildDir/meson-info")) {
  $needsSetup = $true
} elseif (!(Test-Path "$BuildDir/build.ninja")) {
  $needsSetup = $true
}

if ($needsSetup) {
  if (Test-Path $BuildDir) {
    & meson setup $BuildDir --backend $Backend --wipe
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
