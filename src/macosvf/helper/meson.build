# Meson build file for virtmacosvf-helper
#
# This builds the helper app using a custom target since meson
# doesn't have native Objective-C support configured

# Find clang for building Objective-C
clang = find_program('clang', required: true)

# Source directory
source_dir = meson.project_source_root() / 'src' / 'macosvf' / 'helper'

# Source files
helper_sources = files(
  'virtmacosvf-helper_main.m',
  'VZManager.m',
  'VZXPCService.m',
)

# Create the helper executable using custom target
helper_exe = custom_target(
  'virtmacosvf-helper',
  output: 'virtmacosvf-helper',
  build_always_stale: false,
  build_by_default: true,
  command: [
    clang,
    '-fobjc-arc',
    '-I' + meson.project_source_root(),
    '-I' + source_dir,
    '-I' + meson.project_build_root() / 'include',
    '-I' + meson.project_source_root() / 'src',
    '-I' + meson.project_source_root() / 'src' / 'conf',
    '-I' + meson.project_source_root() / 'src' / 'util',
    '-I' + meson.project_build_root() / 'src',
    '-I' + meson.project_build_root(),
    '-I/opt/homebrew/Cellar/glib/2.86.4/include/glib-2.0',
    '-I/opt/homebrew/Cellar/glib/2.86.4/lib/glib-2.0/include',
    '-framework', 'Cocoa',
    '-framework', 'Virtualization',
    '-framework', 'Foundation',
    '-o', '@OUTPUT@',
    source_dir / 'virtmacosvf-helper_main.m',
    source_dir / 'VZManager.m',
    source_dir / 'VZXPCService.m',
  ],
  install: true,
  install_dir: get_option('prefix') / 'Library' / 'PrivilegedHelperTools',
)

# Get the path to the helper executable for the app bundle target
helper_exe_path = helper_exe.full_path()

# Create the helper app bundle structure
# For now, we just build the executable - the .app bundle can be created manually if needed
helper_app_bundle = helper_exe
