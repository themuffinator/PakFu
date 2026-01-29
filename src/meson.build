conf = configuration_data()
conf.set('PAKFU_VERSION', meson.project_version())
conf.set('PAKFU_GITHUB_REPO', get_option('github_repo'))
conf.set('PAKFU_UPDATE_CHANNEL', get_option('update_channel'))

configure_file(
  input: 'pakfu_config.h.in',
  output: 'pakfu_config.h',
  configuration: conf,
)

sources = files(
  'main.cpp',
  'cli/cli.cpp',
  'ui/main_window.cpp',
  'update/update_service.cpp',
)

moc_sources = qt6.preprocess(
  moc_headers: [
    'update/update_service.h',
  ],
)
sources += moc_sources

pakfu = executable(
  'pakfu',
  sources,
  include_directories: include_directories('.'),
  dependencies: [qt6_dep],
  install: true,
)
