# macOS Virtualization.Framework Driver Testing Guide

This document describes the testing infrastructure for the macOS Virtualization.Framework (macOSVF) driver in libvirt.

## Test Overview

The macOSVF driver has comprehensive test coverage to ensure proper functionality and validation of domain configurations.

### Test Files

1. **macosvfxml2xmltest.c** - XML-to-XML roundtrip testing
   - Tests domain definition parsing and formatting
   - Validates device configurations
   - Tests error handling for invalid configurations
   - Architecture: aarch64 only

2. **macosfvcapabilitiestest.c** - Capabilities testing
   - Tests host capability detection
   - Validates guest OS configuration
   - Ensures proper ARM64 support reporting

3. **test_macosvf_simple.c** - Simple smoke test
   - Basic functionality verification
   - Used for quick sanity checks

4. **macosvfdomainlifecycletest.c** - Domain lifecycle testing
   - Tests domain definition
   - Validates lifecycle operations

## Test Coverage

### Valid Configurations (30 tests)

1. **minimal.xml** - Minimal domain configuration
   - Tests absolute minimum required settings
   - Validates auto-addition of controllers

2. **basic.xml** - Basic VM configuration
   - Simple domain with disk
   - Tests standard boot parameters

3. **with-kernel.xml** - Linux kernel boot
   - Direct kernel boot
   - Tests kernel/initrd/cmdline parameters

4. **with-initrd.xml** - Initrd configuration
   - Tests initrd-only boot
   - Validates initrd path handling

5. **with-cmdline.xml** - Command line parameters
   - Tests kernel command line
   - Validates console and root parameters

6. **with-disk.xml** - Disk device configuration
   - Single disk setup
   - Tests virtio disk bus

7. **with-network.xml** - Network device configuration
   - Bridge network type
   - Tests virtio network model

8. **with-serial.xml** - Serial/console devices
   - Tests serial console configuration
   - Validates pty console type

9. **full-config.xml** - Complete configuration
   - All supported features combined
   - Tests multiple disks and networks
   - Validates kernel/initrd/cmdline with devices

10. **max-cpus.xml** - Maximum CPU configuration
    - Tests maximum vCPU limits
    - Validates CPU boundaries

11. **multiple-disks.xml** - Multiple disk setup
    - Tests multiple disk devices
    - Validates different disk formats (raw, qcow2)

12. **multiple-networks.xml** - Multiple network interfaces
    - Tests multiple network devices
    - Validates network configuration

13. **with-network-user.xml** - User networking
    - Tests user-type network
    - Validates SLIRP networking

14. **console-types.xml** - Console type variations
    - Tests pty, file, unix, null serial types
    - Validates multiple serial devices

15. **cpu-host-model.xml** - CPU mode configuration
    - Tests host-model CPU mode
    - Validates CPU configuration

16. **disk-qcow2.xml** - QCOW2 disk format
    - Tests qcow2 disk format
    - Validates CD-ROM devices

17. **disk-readonly.xml** - Read-only disk
    - Tests readonly flag
    - Validates read-only disks

18. **network-mac.xml** - Network MAC addresses
    - Tests explicit MAC addresses
    - Multiple network interfaces

19. **boot-order.xml** - Boot order configuration
    - Tests boot device ordering
    - Multiple boot devices

20. **memory-min.xml** - Minimum memory configuration
    - Tests 1GB memory configuration
    - Memory boundary testing

21. **features-pae.xml** - PAE feature
    - Tests PAE feature flag
    - Feature validation

22. **metadata.xml** - Domain metadata
    - Tests libosinfo metadata
    - Custom metadata support

23. **controller-auto.xml** - Controller auto-addition
    - Tests automatic PCI/ISA controller addition
    - Controller validation

24. **filesystem-basic.xml** - Basic filesystem (shared folder)
    - Tests virtiofs shared folder support
    - Validates mount configuration

25. **filesystem-readonly.xml** - Read-only filesystem
    - Tests read-only shared folder
    - Validates accessmode='mapped'

26. **filesystem-multiple.xml** - Multiple filesystems
    - Tests multiple shared folders
    - Validates multiple mount points

27. **input-keyboard.xml** - USB keyboard input device
    - Tests USB keyboard input support
    - Validates keyboard device configuration

28. **graphics-basic.xml** - Virtio graphics with input
    - Tests virtio video device
    - Validates keyboard and tablet input
    - Tests graphics device support

29. **graphics-vga.xml** - VGA graphics with mouse
    - Tests VGA video device
    - Validates mouse input
    - Tests graphics device support

30. **sound-virtio.xml** - Virtio sound device
    - Tests virtio audio device
    - Validates audio configuration
    - Tests sound device support

### Invalid Configurations (53+ tests)

The test suite includes comprehensive validation tests for unsupported configurations:

#### Architecture and Platform
- `invalid-arch-x86_64.xml` - Rejects x86_64 architecture
- `invalid-os-type-xen.xml` - Rejects non-HVM OS types
- `invalid-machine-type-pc.xml` - Rejects non-macosvf machine types

#### Device Validation
- `invalid-disk-bus-ide.xml` - Rejects IDE disk bus
- `invalid-disk-bus-sata.xml` - Rejects SATA disk bus
- `invalid-disk-device-lun.xml` - Rejects LUN device type
- `invalid-disk-type-block.xml` - Rejects block device type
- `invalid-disk-format-vmdk.xml` - Rejects unsupported disk formats
- `invalid-net-model-e1000.xml` - Rejects non-virtio network models
- `invalid-net-type-user.xml` - Validates user networking support (should pass now)

#### Controller Validation
- `invalid-controller-usb.xml` - Rejects USB controllers
- `invalid-controller-scsi.xml` - Rejects SCSI controllers
- `invalid-controller-ide.xml` - Rejects IDE controllers

#### Device Type Validation
- `invalid-graphics-device.xml` - Rejects graphics devices
- `invalid-input-device.xml` - Rejects input devices
- `invalid-sound-device.xml` - Rejects sound devices
- `invalid-video-device.xml` - Rejects video devices
- `invalid-hostdev-passthrough.xml` - Rejects host device passthrough
- `invalid-usb-device.xml` - Rejects USB devices
- `invalid-watchdog-device.xml` - Rejects watchdog devices
- `invalid-memballoon-device.xml` - Rejects memory balloon devices
- `invalid-vsock-device.xml` - Rejects vsock devices
- `invalid-rng-device.xml` - Rejects RNG devices
- `invalid-tpm-device.xml` - Rejects TPM devices
- `invalid-shmem-device.xml` - Rejects shared memory devices

#### Configuration Validation
- `invalid-bootloader.xml` - Rejects bootloader configuration
- `invalid-firmware-bios.xml` - Rejects BIOS firmware
- `invalid-clock-offset-localtime.xml` - Rejects non-UTC clock
- `invalid-memory-too-small.xml` - Rejects undersized memory
- `invalid-memory-too-large.xml` - Rejects oversized memory
- `invalid-cpu-too-many.xml` - Rejects excessive vCPUs
- `invalid-cpu-mode-custom.xml` - Rejects custom CPU mode
- `invalid-cpu-scheduling.xml` - Rejects CPU tuning
- `invalid-vcpu-hotplug.xml` - Rejects vCPU hotplug
- `invalid-memory-hotplug.xml` - Rejects memory hotplug
- `invalid-hugepages.xml` - Rejects hugepages
- `invalid-numa-config.xml` - Rejects NUMA configuration

#### Feature Validation
- `invalid-feature-smm.xml` - Rejects SMM feature
- `invalid-feature-ioapic.xml` - Rejects IOAPIC feature
- `invalid-feature-hyperv.xml` - Rejects Hyper-V features
- `invalid-timer-tsc.xml` - Rejects TSC timer
- `invalid-timer-hpet.xml` - Rejects HPET timer

#### Serial/Console Validation
- `invalid-serial-type-dev.xml` - Rejects dev serial type

#### Network Validation
- `invalid-net-bandwidth.xml` - Rejects bandwidth limiting
- `invalid-net-filter.xml` - Rejects network filters

#### Filesystem/Shared Folder Validation
- `invalid-filesystem-type-ram.xml` - Rejects non-mount filesystem types
- `invalid-filesystem-driver-nbd.xml` - Rejects non-virtiofs drivers
- `invalid-filesystem-no-source.xml` - Rejects missing source directory
- `invalid-filesystem-no-target.xml` - Rejects missing mount tag
- `invalid-filesystem-wrpolicy.xml` - Rejects write policy configuration

#### Graphics Device Validation
- `invalid-graphics-vnc.xml` - Rejects VNC graphics with helpful message
- `invalid-graphics-spice.xml` - Rejects SPICE graphics with helpful message
- `invalid-graphics-sdl.xml` - Rejects SDL graphics with helpful message
- `invalid-graphics-egl-headless.xml` - Rejects EGL headless graphics

#### Unsupported Device Validation
- `invalid-sound-ich6.xml` - Rejects ICH audio with helpful message
- `invalid-watchdog-i6300esb.xml` - Rejects i6300esb watchdog with helpful message

**Note**: Video devices, input devices (keyboard, mouse, tablet), and virtio audio are now supported. See valid configuration tests above for examples. Legacy audio models (ICH6, ICH7, ICH9, AC97, ES1370, SB16, USB, PCSPK) are not supported.

## Running Tests

### Run all macOSVF tests
```bash
meson test -C build macosvfxml2xmltest
meson test -C build macosfvcapabilitiestest
```

### Run specific test
```bash
meson test -C build macosvfxml2xmltest --suite macosvf
```

### Run with verbose output
```bash
meson test -C build macosvfxml2xmltest -v
```

## Adding New Tests

### Adding a new valid configuration test

1. Create input XML file:
   ```
   tests/macosvfxml2xmldata/aarch64/macosvfxml2xml-<test-name>.xml
   ```

2. Create expected output XML file:
   ```
   tests/macosvfxml2xmloutdata/aarch64/macosvfxml2xmlout-<test-name>.xml
   ```

3. Add test to `tests/macosvfxml2xmltest.c`:
   ```c
   DO_TEST_DIFFERENT("<test-name>");
   ```

### Adding a new invalid configuration test

1. Create input XML file:
   ```
   tests/macosvfxml2xmldata/aarch64/macosvfxml2xml-invalid-<test-name>.xml
   ```

2. Add test to `tests/macosvfxml2xmltest.c`:
   ```c
   DO_TEST_FAILURE("invalid-<test-name>");
   ```

## Test Data Location

Test data files are organized by architecture:
- Input XMLs: `tests/macosvfxml2xmldata/aarch64/`
- Output XMLs: `tests/macosvfxml2xmloutdata/aarch64/`

## Architecture Requirements

The macOSVF driver and tests only support:
- Architecture: ARM64 (aarch64)
- Platform: Apple Silicon (macOS with Virtualization.Framework)
- Host OS: macOS 11.0+ (Big Sur or later)

Tests will automatically skip on non-ARM64 platforms.

## Coverage Summary

- **Valid configurations**: 26 tests
- **Invalid configurations**: 57+ tests
- **Total test cases**: 83+ tests
- **Coverage areas**:
  - Domain lifecycle
  - Device validation (disk, network, console, filesystem, graphics, sound, video, input, watchdog)
  - CPU configuration
  - Memory configuration
  - Network configuration
  - Storage configuration
  - Console/serial configuration
  - Filesystem/shared folder configuration
  - Enhanced device validation (helpful error messages for sound, video, input, watchdog)
  - Feature validation
  - Error handling

## Test Maintenance

When adding new features to the macOSVF driver:
1. Add corresponding valid configuration tests
2. Add invalid configuration tests for rejected features
3. Update this documentation
4. Ensure tests pass on Apple Silicon hardware
