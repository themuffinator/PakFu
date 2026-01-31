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
  'ui/splash_screen.cpp',
  'ui/main_window.cpp',
  'ui/preferences_tab.cpp',
  'ui/theme_manager.cpp',
  'ui/breadcrumb_bar.cpp',
  'ui/preview_pane.cpp',
  'ui/pak_tab.cpp',
  'platform/file_associations.cpp',
  'pak/pak_archive.cpp',
  'update/update_service.cpp',
)

rcc_sources = qt6.preprocess(
  qresources: ['resources.qrc'],
)

moc_sources = qt6.preprocess(
  moc_headers: [
    'update/update_service.h',
    'ui/splash_screen.h',
    'ui/breadcrumb_bar.h',
    'ui/preview_pane.h',
    'ui/pak_tab.h',
    'ui/preferences_tab.h',
  ],
)
sources += rcc_sources
sources += moc_sources

pakfu_link_args = []
pakfu_win_subsystem = 'console'
if host_machine.system() == 'windows'
  pakfu_win_subsystem = 'windows'

  qt_libdir = qt6_dep.get_variable(configtool: 'QT_INSTALL_LIBS')
  qt_entrypoint_name = 'Qt6EntryPoint.lib'
  if get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized'
    qt_entrypoint_name = 'Qt6EntryPointd.lib'
  endif
  pakfu_link_args += join_paths(qt_libdir, qt_entrypoint_name)
endif

pakfu = executable(
  'pakfu',
  sources,
  include_directories: include_directories('.'),
  dependencies: [qt6_dep],
  link_args: pakfu_link_args,
  win_subsystem: pakfu_win_subsystem,
  install: true,
)
