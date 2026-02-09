conf = configuration_data()
conf.set('PAKFU_VERSION', meson.project_version())
conf.set('PAKFU_GITHUB_REPO', get_option('github_repo'))
conf.set('PAKFU_UPDATE_CHANNEL', get_option('update_channel'))

configure_file(
  input: 'pakfu_config.h.in',
  output: 'pakfu_config.h',
  configuration: conf,
)

miniz_lib = static_library(
  'pakfu_miniz',
  files(
    'third_party/miniz/miniz_c.cpp',
    'third_party/miniz/miniz_tdef_c.cpp',
    'third_party/miniz/miniz_tinfl_c.cpp',
    'third_party/miniz/miniz_zip_c.cpp',
  ),
  override_options: [
    'warning_level=0',
    'werror=false',
  ],
)

qt_headers = qt6_dep.get_variable(configtool: 'QT_INSTALL_HEADERS', default_value: '')
qt_version = qt6_dep.get_variable(configtool: 'QT_VERSION', default_value: '')
qt_rhi_includes = []
if qt_headers != '' and qt_version != ''
  qt_rhi_includes += include_directories(join_paths(qt_headers, 'QtGui', qt_version, 'QtGui'))
endif

pakfu_includes = [include_directories('.')] + qt_rhi_includes

sources = files(
  'main.cpp',
  'cli/cli.cpp',
  'archive/archive.cpp',
  'archive/dir_archive.cpp',
  'game/game_set.cpp',
  'game/game_auto_detect.cpp',
  'formats/dds_image.cpp',
  'formats/bsp_preview.cpp',
  'formats/image_loader.cpp',
  'formats/lmp_image.cpp',
  'formats/miptex_image.cpp',
  'formats/cinematic.cpp',
  'formats/cin_cinematic.cpp',
  'formats/roq_cinematic.cpp',
  'formats/model.cpp',
  'formats/quake3_skin.cpp',
  'formats/pcx_image.cpp',
  'formats/tga_image.cpp',
  'formats/wal_image.cpp',
  'zip/quakelive_pk3_crypto.cpp',
  'zip/zip_archive.cpp',
  'ui/splash_screen.cpp',
  'ui/main_window.cpp',
  'ui/game_set_dialog.cpp',
  'ui/game_set_editor_dialog.cpp',
  'ui/preferences_tab.cpp',
  'ui/preview_renderer.cpp',
  'ui/theme_manager.cpp',
  'ui/breadcrumb_bar.cpp',
  'ui/cfg_syntax_highlighter.cpp',
  'ui/simple_syntax_highlighter.cpp',
  'ui/cinematic_player_widget.cpp',
  'ui/video_player_widget.cpp',
  'ui/bsp_preview_widget.cpp',
  'ui/bsp_preview_vulkan_widget.cpp',
  'ui/model_viewer_widget.cpp',
  'ui/model_viewer_vulkan_widget.cpp',
  'ui/preview_pane.cpp',
  'ui/pak_tab.cpp',
  'platform/file_associations.cpp',
  'pak/pak_archive.cpp',
  'wad/wad_archive.cpp',
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
    'ui/cinematic_player_widget.h',
    'ui/video_player_widget.h',
    'ui/bsp_preview_vulkan_widget.h',
    'ui/model_viewer_vulkan_widget.h',
    'ui/preview_pane.h',
    'ui/pak_tab.h',
    'ui/preferences_tab.h',
  ],
)
sources += rcc_sources
sources += moc_sources

pakfu_link_args = []
pakfu_win_subsystem = 'console'
pakfu_win_resources = []
if host_machine.system() == 'windows'
  rc = find_program('rc', required: false)
  windres = find_program('windres', required: false)
  if rc.found() or windres.found()
    windows = import('windows')
    pakfu_win_resources += windows.compile_resources('pakfu.rc')
  else
    warning('No Windows resource compiler found; skipping embedded .exe icon resource.')
  endif
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
  sources + pakfu_win_resources,
  include_directories: pakfu_includes,
  dependencies: [qt6_dep],
  link_with: [miniz_lib],
  link_args: pakfu_link_args,
  win_subsystem: pakfu_win_subsystem,
  install: true,
)

roq_probe = executable(
  'roq_probe',
  [
    'tools/roq_probe.cpp',
    'formats/cinematic.cpp',
    'formats/cin_cinematic.cpp',
    'formats/roq_cinematic.cpp',
  ],
  include_directories: pakfu_includes,
  dependencies: [qt6_dep],
  install: false,
)

model_probe = executable(
  'model_probe',
  [
    'tools/model_probe.cpp',
    'formats/model.cpp',
  ],
  include_directories: pakfu_includes,
  dependencies: [qt6_dep],
  install: false,
)
