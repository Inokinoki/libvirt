macosvf_c_sources = files(
  'macosvf_capabilities.c',
  'macosvf_conf.c',
  'macosvf_device.c',
  'macosvf_domain.c',
  'macosvf_driver.c',
  'macosvf_utils.c',
)

macosvf_objc_sources = files('macosvf_vm.c')

driver_source_files += macosvf_c_sources
stateful_driver_source_files += macosvf_c_sources

if conf.has('WITH_MACOSVF')
  # Build Objective-C source with proper compiler flags
  macosvf_objc_impl = static_library(
    'virt_driver_macosvf_objc',
    macosvf_objc_sources,
    dependencies: [
      access_dep,
      src_dep,
    ],
    include_directories: [
      conf_inc_dir,
      hypervisor_inc_dir,
    ],
    # Compile as Objective-C
    c_args: ['-x', 'objective-c'],
    # Link against virtualization.framework
    link_args: ['-framework', 'Virtualization'],
  )

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

  # Combine both libraries
  macosvf_driver_impl = static_library(
    'virt_driver_macosvf_impl',
    link_whole: [
      macosvf_objc_impl,
      macosvf_c_impl,
    ],
    # Link against virtualization.framework
    link_args: ['-framework', 'Virtualization'],
  )

  virt_modules += {
    'name': 'virt_driver_macosvf',
    'link_whole': [
      macosvf_driver_impl,
    ],
    'link_args': [
      libvirt_no_undefined,
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
