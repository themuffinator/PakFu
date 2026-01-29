project(
  'pakfu',
  'cpp',
  version: run_command(
    'python',
    '-c',
    'from pathlib import Path; print(Path("VERSION").read_text().strip())',
    check: true
  ).stdout().strip(),
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'werror=false',
  ],
)

qt6_modules = ['Core', 'Gui', 'Widgets', 'Network']
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: qt6_modules, method: 'config-tool', required: false)
if not qt6_dep.found()
  qt6_dep = dependency('qt6', modules: qt6_modules, method: 'pkg-config', required: false)
endif
if not qt6_dep.found()
  error('Qt 6 not found. Install Qt 6 and ensure qmake6 is on PATH or pkg-config is available.')
endif

subdir('src')
