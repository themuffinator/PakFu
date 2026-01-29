project(
  'pakfu',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'werror=false',
  ],
)

qt6_modules = ['Core', 'Gui', 'Widgets']
qt6_dep = dependency('qt6', modules: qt6_modules, method: 'config-tool', required: false)
if not qt6_dep.found()
  qt6_dep = dependency('qt6', modules: qt6_modules, method: 'pkg-config', required: false)
endif
if not qt6_dep.found()
  error('Qt 6 not found. Install Qt 6 and ensure qmake6 is on PATH or pkg-config is available.')
endif

subdir('src')
