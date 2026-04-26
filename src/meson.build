conf = configuration_data()
conf.set('PAKFU_VERSION', meson.project_version())
conf.set('PAKFU_GITHUB_REPO', get_option('github_repo'))
conf.set('PAKFU_UPDATE_CHANNEL', get_option('update_channel'))

cpp_compiler = meson.get_compiler('cpp')

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
qt_libs = qt6_dep.get_variable(configtool: 'QT_INSTALL_LIBS', default_value: '')
qt_version = qt6_dep.get_variable(configtool: 'QT_VERSION', default_value: '')

fs = import('fs')
vulkan_preview_opt = get_option('vulkan_preview')
qt_rhi_includes = []
qt_rhi_candidates = []
qt_rhi_headers_found = false
if qt_headers != '' and qt_version != ''
  qt_rhi_candidates += join_paths(qt_headers, 'QtGui', qt_version, 'QtGui')
  qt_rhi_candidates += join_paths(qt_headers, 'QtGui.framework', 'Headers', qt_version, 'QtGui')
endif
if qt_headers != ''
  qt_rhi_candidates += join_paths(qt_headers, 'QtGui')
  qt_rhi_candidates += join_paths(qt_headers, 'QtGui.framework', 'Headers')
endif
if qt_libs != '' and qt_version != ''
  qt_rhi_candidates += join_paths(qt_libs, 'QtGui.framework', 'Headers', qt_version, 'QtGui')
  qt_rhi_candidates += join_paths(qt_libs, 'QtGui.framework', 'Versions', 'A', 'Headers', qt_version, 'QtGui')
endif
if qt_libs != ''
  qt_rhi_candidates += join_paths(qt_libs, 'QtGui.framework', 'Headers')
  qt_rhi_candidates += join_paths(qt_libs, 'QtGui.framework', 'Versions', 'A', 'Headers')
endif

foreach path : qt_rhi_candidates
  if fs.exists(join_paths(path, 'rhi', 'qrhi.h')) and fs.exists(join_paths(path, 'rhi', 'qshader.h'))
    qt_rhi_headers_found = true
    qt_rhi_includes += include_directories(path)
    break
  endif
endforeach

if vulkan_preview_opt.enabled() and not qt_rhi_headers_found
  error('Vulkan preview requires Qt private RHI headers (rhi/qrhi.h and rhi/qshader.h). Install the Qt private development headers or configure with -Dvulkan_preview=disabled.')
endif

vulkan_preview_enabled = qt_rhi_headers_found and not vulkan_preview_opt.disabled()
conf.set('PAKFU_WITH_VULKAN_PREVIEW', vulkan_preview_enabled ? 1 : 0)

if vulkan_preview_enabled
  message('Vulkan preview renderer enabled.')
else
  message('Vulkan preview renderer disabled; OpenGL preview remains available.')
endif

configure_file(
  input: 'pakfu_config.h.in',
  output: 'pakfu_config.h',
  configuration: conf,
)

pakfu_includes = [include_directories('.')] + qt_rhi_includes

sources = files(
  'main.cpp',
  'cli/cli.cpp',
  'archive/archive.cpp',
  'archive/dir_archive.cpp',
  'resources/resources_archive.cpp',
  'game/game_set.cpp',
  'game/game_auto_detect.cpp',
  'formats/dds_image.cpp',
  'formats/ftx_image.cpp',
  'formats/bsp_preview.cpp',
  'formats/idtech_asset_loader.cpp',
  'formats/idwav_audio.cpp',
  'formats/sprite_loader.cpp',
  'formats/image_loader.cpp',
  'formats/image_writer.cpp',
  'formats/lmp_image.cpp',
  'formats/miptex_image.cpp',
  'formats/cinematic.cpp',
  'formats/cin_cinematic.cpp',
  'formats/roq_cinematic.cpp',
  'formats/model.cpp',
  'formats/quake3_skin.cpp',
  'formats/quake3_shader.cpp',
  'formats/pcx_image.cpp',
  'formats/swl_image.cpp',
  'formats/tga_image.cpp',
  'formats/wal_image.cpp',
  'zip/quakelive_pk3_crypto.cpp',
  'zip/zip_archive.cpp',
  'ui/splash_screen.cpp',
  'ui/about_dialog.cpp',
  'ui/main_window.cpp',
  'ui/file_dialog_utils.cpp',
  'ui/file_associations_dialog.cpp',
  'ui/game_set_dialog.cpp',
  'ui/game_set_editor_dialog.cpp',
  'ui/preferences_tab.cpp',
  'ui/preview_renderer.cpp',
  'ui/theme_manager.cpp',
  'ui/drag_drop_policy.cpp',
  'ui/practical_qa.cpp',
  'ui/ui_icons.cpp',
  'ui/breadcrumb_bar.cpp',
  'ui/audio_viewer_window.cpp',
  'ui/image_viewer_window.cpp',
  'ui/model_viewer_window.cpp',
  'ui/video_viewer_window.cpp',
  'ui/cfg_syntax_highlighter.cpp',
  'ui/simple_syntax_highlighter.cpp',
  'ui/cinematic_player_widget.cpp',
  'ui/video_player_widget.cpp',
  'ui/bsp_preview_widget.cpp',
  'ui/model_viewer_widget.cpp',
  'ui/shader_viewer_widget.cpp',
  'ui/preview_pane.cpp',
  'ui/pak_tab.cpp',
  'platform/crash_handler.cpp',
  'platform/file_associations.cpp',
  'pak/pak_archive.cpp',
  'wad/wad_archive.cpp',
  'update/update_service.cpp',
)

if vulkan_preview_enabled
  sources += files(
    'ui/bsp_preview_vulkan_widget.cpp',
    'ui/model_viewer_vulkan_widget.cpp',
  )
endif

rcc_sources = qt6.preprocess(
  qresources: ['resources.qrc'],
)

moc_headers = [
  'update/update_service.h',
  'ui/splash_screen.h',
  'ui/breadcrumb_bar.h',
  'ui/cinematic_player_widget.h',
  'ui/video_player_widget.h',
  'ui/preview_pane.h',
  'ui/pak_tab.h',
  'ui/preferences_tab.h',
]
moc_extra_arguments = []

if vulkan_preview_enabled
  moc_headers += [
    'ui/bsp_preview_vulkan_widget.h',
    'ui/model_viewer_vulkan_widget.h',
  ]
  moc_extra_arguments += ['-DPAKFU_WITH_VULKAN_PREVIEW=1']
endif

moc_sources = qt6.preprocess(
  moc_headers: moc_headers,
  moc_extra_arguments: moc_extra_arguments,
)
sources += rcc_sources
sources += moc_sources

pakfu_link_args = []
pakfu_win_subsystem = 'console'
pakfu_win_resources = []
pakfu_platform_deps = []
if host_machine.system() == 'windows'
  rc = find_program('rc', required: false)
  windres = find_program('windres', required: false)
  if rc.found() or windres.found()
    windows = import('windows')
    pakfu_win_resources += windows.compile_resources('pakfu.rc')
  else
    message('No Windows resource compiler found; skipping embedded .exe icon resource.')
  endif
  pakfu_win_subsystem = 'windows'

  # Use mainCRTStartup directly for Windows GUI subsystem builds.
  # This avoids duplicate WinMain/wWinMain shims and keeps linker output warning-free.
  pakfu_link_args += '/ENTRY:mainCRTStartup'
  pakfu_platform_deps += cpp_compiler.find_library('Dbghelp', required: true)
endif

pakfu = executable(
  'pakfu',
  sources + pakfu_win_resources,
  include_directories: pakfu_includes,
  dependencies: [qt6_dep] + pakfu_platform_deps,
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
