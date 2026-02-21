macosvf_c_sources = files(
  'macosvf_capabilities.c',
  'macosvf_conf.c',
  'macosvf_device.c',
  'macosvf_domain.c',
  'macosvf_driver.c',
  'macosvf_utils.c',
)

driver_source_files += macosvf_c_sources
stateful_driver_source_files += macosvf_c_sources

if conf.has('WITH_MACOSVF')
  # Build the helper app bundle first
  subdir('helper')

  # Find clang for building Objective-C
  clang = find_program('clang', required: true)

  # Compile macosvf_vm.c as Objective-C
  macosvf_vm_obj = custom_target(
    'macosvf_vm.o',
    output: 'macosvf_vm.o',
    build_always_stale: false,
    command: [
      clang,
      '-c', '-x', 'objective-c', '-fobjc-arc',
      '-I' + meson.project_source_root() / 'src',
      '-I' + meson.project_build_root() / 'include',
      '-I' + meson.project_source_root() / 'include',
      '-I' + meson.project_source_root() / 'include' / 'libvirt',
      '-I' + meson.project_source_root() / 'src' / 'conf',
      '-I' + meson.project_source_root() / 'src' / 'access',
      '-I' + meson.project_source_root() / 'src' / 'rpc',
      '-I' + meson.project_source_root() / 'src' / 'util',
      '-I' + meson.project_build_root() / 'src',
      '-I' + meson.project_build_root(),
      '-I/opt/homebrew/Cellar/glib/2.86.4/include/glib-2.0',
      '-I/opt/homebrew/Cellar/glib/2.86.4/lib/glib-2.0/include',
      '-framework', 'Cocoa',
      '-framework', 'Foundation',
      '-o', '@OUTPUT@',
      meson.current_source_dir() / 'macosvf_vm.c',
    ],
    install: false,
  )

  # Compile macosvf_helper_client.m as Objective-C
  macosvf_helper_client_obj = custom_target(
    'macosvf_helper_client.o',
    output: 'macosvf_helper_client.o',
    build_always_stale: false,
    command: [
      clang,
      '-c', '-x', 'objective-c', '-fobjc-arc',
      '-I' + meson.project_source_root() / 'src',
      '-I' + meson.project_build_root() / 'include',
      '-I' + meson.project_source_root() / 'include',
      '-I' + meson.project_source_root() / 'include' / 'libvirt',
      '-I' + meson.project_source_root() / 'src' / 'conf',
      '-I' + meson.project_source_root() / 'src' / 'access',
      '-I' + meson.project_source_root() / 'src' / 'rpc',
      '-I' + meson.project_source_root() / 'src' / 'util',
      '-I' + meson.project_build_root() / 'src',
      '-I' + meson.project_build_root(),
      '-I/opt/homebrew/Cellar/glib/2.86.4/include/glib-2.0',
      '-I/opt/homebrew/Cellar/glib/2.86.4/lib/glib-2.0/include',
      '-framework', 'Cocoa',
      '-framework', 'Foundation',
      '-framework', 'ServiceManagement',
      '-framework', 'Security',
      '-o', '@OUTPUT@',
      meson.current_source_dir() / 'macosvf_helper_client.m',
    ],
    install: false,
  )

  # Collect Objective-C objects
  objc_objects = [macosvf_vm_obj, macosvf_helper_client_obj]

  # Build C sources
  macosvf_c_impl = static_library(
    'virt_driver_macosvf_c',
    macosvf_c_sources,
    dependencies: [
      access_dep,
      src_dep,
    ],
    include_directories: [
      conf_inc_dir,
      hypervisor_inc_dir,
    ],
  )

  # Combine all libraries - include Objective-C objects
  macosvf_driver_impl = static_library(
    'virt_driver_macosvf_impl',
    link_whole: [
      macosvf_c_impl,
    ],
    objects: objc_objects,
    # Link against Virtualization and Cocoa frameworks
    link_args: ['-framework', 'Virtualization', '-framework', 'Cocoa', '-framework', 'Foundation'],
  )

  virt_modules += {
    'name': 'virt_driver_macosvf',
    'link_whole': [
      macosvf_driver_impl,
    ],
    'link_args': [
      libvirt_no_undefined,
      '-framework', 'Virtualization',
      '-framework', 'Cocoa',
      '-framework', 'Foundation',
    ],
  }

  virt_daemons += {
    'name': 'virtmacosvfd',
    'c_args': [
      '-DDAEMON_NAME="virtmacosvfd"',
      '-DMODULE_NAME="macosvf"'
    ],
  }

  virt_conf_files += files('macosvf.conf')
  virt_install_dirs += [
    localstatedir / 'lib' / 'libvirt' / 'macosvf',
  ]
endif
