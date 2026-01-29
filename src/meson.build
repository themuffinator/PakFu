sources = files(
  'main.cpp',
  'cli/cli.cpp',
  'ui/main_window.cpp',
)

pakfu = executable(
  'pakfu',
  sources,
  include_directories: include_directories('.'),
  dependencies: [qt6_dep],
  install: true,
)
