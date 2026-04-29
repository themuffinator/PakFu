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
pkgconfig = import('pkgconfig')
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

fuzzing_opt = get_option('fuzzing')
fuzz_compile_args = ['-fsanitize=fuzzer', '-fno-omit-frame-pointer']
fuzz_link_args = ['-fsanitize=fuzzer']
fuzz_link_probe = '''
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) {
  return 0;
}
'''
fuzzing_supported = (
  cpp_compiler.has_multi_arguments(fuzz_compile_args) and
  cpp_compiler.links(
    fuzz_link_probe,
    args: fuzz_compile_args + fuzz_link_args,
    name: 'libFuzzer entrypoint',
  )
)
if fuzzing_opt.enabled() and not fuzzing_supported
  error('Parser fuzz targets require compiler support for -fsanitize=fuzzer.')
endif
fuzzing_enabled = fuzzing_supported and not fuzzing_opt.disabled()
if fuzzing_enabled
  message('Parser fuzz targets enabled.')
endif

core_sources = files(
  'archive/archive.cpp',
  'archive/archive_session.cpp',
  'archive/dir_archive.cpp',
  'archive/archive_search_index.cpp',
  'foundation/performance_metrics.cpp',
  'extensions/extension_plugin.cpp',
  'resources/resources_archive.cpp',
  'game/game_set.cpp',
  'game/game_auto_detect.cpp',
  'formats/dds_image.cpp',
  'formats/fontdat_font.cpp',
  'formats/ftx_image.cpp',
  'formats/bsp_preview.cpp',
  'formats/idtech4_map.cpp',
  'formats/idtech_asset_loader.cpp',
  'formats/idwav_audio.cpp',
  'formats/sprite_loader.cpp',
  'formats/image_loader.cpp',
  'formats/image_writer.cpp',
  'formats/lmp_image.cpp',
  'formats/m8_image.cpp',
  'formats/m32_image.cpp',
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
  'pak/pak_archive.cpp',
  'wad/wad_archive.cpp',
)

pakfu_core_public_headers = files(
  'pakfu_core.h',
)

pakfu_archive_public_headers = files(
  'archive/archive.h',
  'archive/archive_entry.h',
  'archive/archive_session.h',
  'archive/archive_search_index.h',
  'archive/dir_archive.h',
  'archive/path_safety.h',
)

pakfu_extensions_public_headers = files(
  'extensions/extension_plugin.h',
)

pakfu_formats_public_headers = files(
  'formats/bsp_preview.h',
  'formats/cin_cinematic.h',
  'formats/cinematic.h',
  'formats/dds_image.h',
  'formats/fontdat_font.h',
  'formats/ftx_image.h',
  'formats/idtech4_map.h',
  'formats/idtech_asset_loader.h',
  'formats/idwav_audio.h',
  'formats/image_loader.h',
  'formats/image_writer.h',
  'formats/lmp_image.h',
  'formats/m8_image.h',
  'formats/m32_image.h',
  'formats/miptex_image.h',
  'formats/model.h',
  'formats/pcx_image.h',
  'formats/quake3_shader.h',
  'formats/quake3_skin.h',
  'formats/roq_cinematic.h',
  'formats/sprite_loader.h',
  'formats/swl_image.h',
  'formats/tga_image.h',
  'formats/wal_image.h',
)

pakfu_game_public_headers = files(
  'game/game_auto_detect.h',
  'game/game_set.h',
)

pakfu_pak_public_headers = files(
  'pak/pak_archive.h',
)

pakfu_resources_public_headers = files(
  'resources/resources_archive.h',
)

pakfu_wad_public_headers = files(
  'wad/wad_archive.h',
)

pakfu_zip_public_headers = files(
  'zip/quakelive_pk3_crypto.h',
  'zip/zip_archive.h',
)

app_sources = files(
  'main.cpp',
  'cli/cli.cpp',
  'ui/background_task_coordinator.cpp',
  'ui/splash_screen.cpp',
  'ui/about_dialog.cpp',
  'ui/main_window.cpp',
  'ui/file_dialog_utils.cpp',
  'ui/file_associations_dialog.cpp',
  'ui/game_set_dialog.cpp',
  'ui/game_set_editor_dialog.cpp',
  'ui/preferences_tab.cpp',
  'ui/workspace_tab.cpp',
  'ui/preview_renderer.cpp',
  'ui/preview_io_cache.cpp',
  'ui/preview_scene.cpp',
  'ui/preview_session.cpp',
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
  'update/update_service.cpp',
)

if vulkan_preview_enabled
  app_sources += files(
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
app_sources += rcc_sources
app_sources += moc_sources

pakfu_core = static_library(
  'pakfu_core',
  core_sources,
  include_directories: pakfu_includes,
  dependencies: [qt6_core_dep],
  link_whole: [miniz_lib],
  install: true,
)

pakfu_core_dep = declare_dependency(
  include_directories: pakfu_includes,
  dependencies: [qt6_core_dep],
  link_with: [pakfu_core],
)

install_headers(pakfu_core_public_headers, subdir: 'pakfu')
install_headers(pakfu_archive_public_headers, subdir: 'pakfu/archive')
install_headers(pakfu_extensions_public_headers, subdir: 'pakfu/extensions')
install_headers(pakfu_formats_public_headers, subdir: 'pakfu/formats')
install_headers(pakfu_game_public_headers, subdir: 'pakfu/game')
install_headers(pakfu_pak_public_headers, subdir: 'pakfu/pak')
install_headers(pakfu_resources_public_headers, subdir: 'pakfu/resources')
install_headers(pakfu_wad_public_headers, subdir: 'pakfu/wad')
install_headers(pakfu_zip_public_headers, subdir: 'pakfu/zip')

pkgconfig.generate(
  pakfu_core,
  name: 'PakFu Core',
  filebase: 'pakfu_core',
  description: 'Non-UI archive, format, search, game-profile, and extension library for PakFu',
  version: meson.project_version(),
  subdirs: 'pakfu',
  requires: ['Qt6Core', 'Qt6Gui'],
)

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
  app_sources + pakfu_win_resources,
  include_directories: pakfu_includes,
  dependencies: [qt6_dep, pakfu_core_dep] + pakfu_platform_deps,
  link_args: pakfu_link_args,
  win_subsystem: pakfu_win_subsystem,
  install: true,
)

if host_machine.system() == 'linux'
  install_data(
    files('../packaging/linux/io.github.themuffinator.PakFu.desktop'),
    install_dir: join_paths(get_option('datadir'), 'applications'),
  )
  install_data(
    files('../packaging/linux/io.github.themuffinator.PakFu.mime.xml'),
    rename: 'io.github.themuffinator.PakFu.xml',
    install_dir: join_paths(get_option('datadir'), 'mime/packages'),
  )
  install_data(
    files('../packaging/linux/io.github.themuffinator.PakFu.metainfo.xml'),
    install_dir: join_paths(get_option('datadir'), 'metainfo'),
  )
  install_data(
    files('../assets/img/pakfu-icon-256.png'),
    rename: 'io.github.themuffinator.PakFu.png',
    install_dir: join_paths(get_option('datadir'), 'icons/hicolor/256x256/apps'),
  )
endif

roq_probe = executable(
  'roq_probe',
  ['tools/roq_probe.cpp'],
  include_directories: pakfu_includes,
  dependencies: [pakfu_core_dep],
  install: false,
)

model_probe = executable(
  'model_probe',
  ['tools/model_probe.cpp'],
  include_directories: pakfu_includes,
  dependencies: [pakfu_core_dep],
  install: false,
)

archive_writer_roundtrip_test = executable(
  'archive_writer_roundtrip_test',
  ['tests/archive_writer_roundtrip_test.cpp'],
  include_directories: pakfu_includes,
  dependencies: [pakfu_core_dep],
  install: false,
)

test('archive-writer-roundtrip', archive_writer_roundtrip_test)

archive_search_index_test = executable(
  'archive_search_index_test',
  ['tests/archive_search_index_test.cpp'],
  include_directories: pakfu_includes,
  dependencies: [pakfu_core_dep],
  install: false,
)

test('archive-search-index', archive_search_index_test)

core_api_smoke_test = executable(
  'core_api_smoke_test',
  ['tests/core_api_smoke_test.cpp'],
  include_directories: pakfu_includes,
  dependencies: [pakfu_core_dep],
  install: false,
)

test('core-api-smoke', core_api_smoke_test)

foundation_phase_test = executable(
  'foundation_phase_test',
  ['tests/foundation_phase_test.cpp', 'ui/preview_io_cache.cpp'],
  include_directories: pakfu_includes,
  dependencies: [pakfu_core_dep],
  install: false,
)

test('foundation-phase', foundation_phase_test)

game_auto_detect_test = executable(
  'game_auto_detect_test',
  ['tests/game_auto_detect_test.cpp'],
  include_directories: pakfu_includes,
  dependencies: [pakfu_core_dep],
  install: false,
)

test('game-auto-detect', game_auto_detect_test)

extension_plugin_test = executable(
  'extension_plugin_test',
  ['tests/extension_plugin_test.cpp'],
  include_directories: pakfu_includes,
  dependencies: [pakfu_core_dep],
  install: false,
)

test('extension-plugin', extension_plugin_test)

support_matrix_fixture_test = executable(
  'support_matrix_fixture_test',
  ['tests/support_matrix_fixture_test.cpp'],
  include_directories: pakfu_includes,
  dependencies: [pakfu_core_dep],
  install: false,
)

test('support-matrix-fixtures', support_matrix_fixture_test)

if fuzzing_enabled
  fuzz_test_env = environment()
  fuzz_test_env.set('ASAN_OPTIONS', 'detect_leaks=0')
  fuzz_test_env.set('UBSAN_OPTIONS', 'print_stacktrace=1')

  pak_fuzzer = executable(
    'pak_fuzzer',
    ['tests/pak_fuzzer.cpp'],
    include_directories: pakfu_includes,
    dependencies: [pakfu_core_dep],
    cpp_args: fuzz_compile_args,
    link_args: fuzz_link_args,
    install: false,
  )

  test(
    'pak-fuzzer-smoke',
    pak_fuzzer,
    args: ['-runs=32', '-max_len=4096'],
    env: fuzz_test_env,
    timeout: 60,
  )

  wad_fuzzer = executable(
    'wad_fuzzer',
    ['tests/wad_fuzzer.cpp'],
    include_directories: pakfu_includes,
    dependencies: [pakfu_core_dep],
    cpp_args: fuzz_compile_args,
    link_args: fuzz_link_args,
    install: false,
  )

  test(
    'wad-fuzzer-smoke',
    wad_fuzzer,
    args: ['-runs=32', '-max_len=4096'],
    env: fuzz_test_env,
    timeout: 60,
  )

  zip_fuzzer = executable(
    'zip_fuzzer',
    ['tests/zip_fuzzer.cpp'],
    include_directories: pakfu_includes,
    dependencies: [pakfu_core_dep],
    cpp_args: fuzz_compile_args,
    link_args: fuzz_link_args,
    install: false,
  )

  test(
    'zip-fuzzer-smoke',
    zip_fuzzer,
    args: ['-runs=32', '-max_len=4096'],
    env: fuzz_test_env,
    timeout: 60,
  )

  resources_fuzzer = executable(
    'resources_fuzzer',
    ['tests/resources_fuzzer.cpp'],
    include_directories: pakfu_includes,
    dependencies: [pakfu_core_dep],
    cpp_args: fuzz_compile_args,
    link_args: fuzz_link_args,
    install: false,
  )

  test(
    'resources-fuzzer-smoke',
    resources_fuzzer,
    args: ['-runs=32', '-max_len=4096'],
    env: fuzz_test_env,
    timeout: 60,
  )

  image_fuzzer = executable(
    'image_fuzzer',
    ['tests/image_fuzzer.cpp'],
    include_directories: pakfu_includes,
    dependencies: [pakfu_core_dep],
    cpp_args: fuzz_compile_args,
    link_args: fuzz_link_args,
    install: false,
  )

  test(
    'image-fuzzer-smoke',
    image_fuzzer,
    args: ['-runs=32', '-max_len=4096'],
    env: fuzz_test_env,
    timeout: 60,
  )

  model_fuzzer = executable(
    'model_fuzzer',
    ['tests/model_fuzzer.cpp'],
    include_directories: pakfu_includes,
    dependencies: [pakfu_core_dep],
    cpp_args: fuzz_compile_args,
    link_args: fuzz_link_args,
    install: false,
  )

  test(
    'model-fuzzer-smoke',
    model_fuzzer,
    args: ['-runs=32', '-max_len=4096'],
    env: fuzz_test_env,
    timeout: 60,
  )

  cinematic_fuzzer = executable(
    'cinematic_fuzzer',
    ['tests/cinematic_fuzzer.cpp'],
    include_directories: pakfu_includes,
    dependencies: [pakfu_core_dep],
    cpp_args: fuzz_compile_args,
    link_args: fuzz_link_args,
    install: false,
  )

  test(
    'cinematic-fuzzer-smoke',
    cinematic_fuzzer,
    args: ['-runs=32', '-max_len=4096'],
    env: fuzz_test_env,
    timeout: 60,
  )
endif
