# Meson build file for virtmacosvf-helper
#
# This builds the helper app using a custom target since meson
# doesn't have native Objective-C support configured

# Find clang for building Objective-C
clang = find_program('clang', required: true)

# Source files
helper_sources = files(
  'virtmacosvf-helper_main.m',
  'VZManager.m',
  'VZXPCService.m',
)

# Source directory
source_dir = meson.project_source_root() / 'src' / 'macosvf' / 'helper'

# Create the helper executable using custom target
helper_exe = custom_target(
  'virtmacosvf-helper',
  output: 'virtmacosvf-helper',
  build_always_stale: true,
  command: [
    clang,
    '-fobjc-arc',
    '-framework', 'Cocoa',
    '-framework', 'Virtualization',
    '-framework', 'Foundation',
    '-I' + source_dir,
    '-o', '@OUTPUT@',
    source_dir / 'virtmacosvf-helper_main.m',
    source_dir / 'VZManager.m',
    source_dir / 'VZXPCService.m',
  ],
  install: false,
)

# Get the path to the helper executable for the app bundle target
helper_exe_path = helper_exe.full_path()

# Create the helper app bundle structure
# For now, we just build the executable - the .app bundle can be created manually if needed
helper_app_bundle = helper_exe
