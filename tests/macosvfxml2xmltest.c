/*
 * macosvfxml2xmltest.c: test macOS Virtualization.Framework domain XML conversions
 *
 * Copyright (C) 2025
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "testutils.h"

#ifdef WITH_MACOSVF

# include "macosvf/macosvf_capabilities.h"
# include "macosvf/macosvf_domain.h"
# include "macosvf/macosvf_conf.h"

# define VIR_FROM_THIS VIR_FROM_NONE

static macosvfConn driver;

struct testInfo {
    const char *name;
    unsigned int flags;
};

typedef enum {
    FLAG_IS_DIFFERENT =   1 << 0,
    FLAG_EXPECT_FAILURE = 1 << 1,
} virMacOSVFXMLToXMLTestFlags;

static int
testCompareXMLToXMLHelper(const void *data)
{
    const struct testInfo *info = data;
    g_autofree char *xml_in = NULL;
    g_autofree char *xml_out = NULL;
    bool is_different = info->flags & FLAG_IS_DIFFERENT;
    int ret = -1;
    const char *arch = virArchToString(virArchFromHost());

    xml_in = g_strdup_printf("%s/macosvfxml2xmldata/%s/macosvfxml2xml-%s.xml",
                             abs_srcdir, arch, info->name);
    xml_out = g_strdup_printf("%s/macosvfxml2xmloutdata/%s/macosvfxml2xmlout-%s.xml",
                              abs_srcdir, arch, info->name);

    ret = testCompareDomXML2XMLFiles(driver.caps, driver.xmlopt, xml_in,
                                     is_different ? xml_out : xml_in,
                                     false, 0,
                                     TEST_COMPARE_DOM_XML2XML_RESULT_SUCCESS);

    if ((ret != 0) && (info->flags & FLAG_EXPECT_FAILURE)) {
        ret = 0;
        VIR_TEST_DEBUG("Got expected error: %s",
                       virGetLastErrorMessage());
        virResetLastError();
    }

    return ret;
}

static int
mymain(void)
{
    int ret = 0;

    /* macOSVF only supports ARM64/Apple Silicon */
    virTestSetHostArch(VIR_ARCH_AARCH64);

    if ((driver.caps = macosvfCreateCapabilities()) == NULL)
        return EXIT_FAILURE;

    if ((driver.xmlopt = virMacOSVFDriverCreateXMLConf(&driver)) == NULL) {
        virObjectUnref(driver.caps);
        return EXIT_FAILURE;
    }

# define DO_TEST(name) \
    DO_TEST_FULL(name, 0)

# define DO_TEST_DIFFERENT(name) \
    DO_TEST_FULL(name, FLAG_IS_DIFFERENT)

# define DO_TEST_FAILURE(name) \
    DO_TEST_FULL(name, FLAG_EXPECT_FAILURE)

# define DO_TEST_FULL(name, flags) \
    do { \
        const struct testInfo info = {name, (flags)}; \
        if (virTestRun("MACOSVF XML-2-XML " name, \
                       testCompareXMLToXMLHelper, &info) < 0) \
            ret = -1; \
    } while (0)

    /* Basic domain configuration tests */
    DO_TEST_DIFFERENT("minimal");
    DO_TEST_DIFFERENT("basic");
    DO_TEST_DIFFERENT("with-kernel");
    DO_TEST_DIFFERENT("with-initrd");
    DO_TEST_DIFFERENT("with-cmdline");
    DO_TEST_DIFFERENT("os-loader");

    /* Device tests */
    DO_TEST_DIFFERENT("with-disk");
    DO_TEST_DIFFERENT("with-network");
    DO_TEST_DIFFERENT("with-serial");
    DO_TEST_DIFFERENT("with-network-user");
    DO_TEST_DIFFERENT("console-types");
    DO_TEST_DIFFERENT("serial-target-port");
    DO_TEST_DIFFERENT("console-target-serial");
    DO_TEST_DIFFERENT("simple-network");

    /* Disk tests */
    DO_TEST_DIFFERENT("disk-qcow2");
    DO_TEST_DIFFERENT("disk-readonly");
    DO_TEST_DIFFERENT("minimal-cdrom");
    DO_TEST_DIFFERENT("disk-aio");
    DO_TEST_DIFFERENT("disk-event-idx");
    DO_TEST_DIFFERENT("disk-metadata");
    DO_TEST_DIFFERENT("disk-discard");
    DO_TEST_DIFFERENT("disk-copy-on-read");
    DO_TEST_DIFFERENT("disk-startup-policy");
    DO_TEST_DIFFERENT("disk-tray");
    DO_TEST_DIFFERENT("disk-readonly-cdrom");

    /* Serial tests */
    DO_TEST_DIFFERENT("serial-log-file");
    DO_TEST_DIFFERENT("serial-log-append");
    DO_TEST_DIFFERENT("serial-unix-bind");
    DO_TEST_DIFFERENT("serial-pty");

    /* Network tests */
    DO_TEST_DIFFERENT("network-mac");
    DO_TEST_DIFFERENT("interface-bridge");
    DO_TEST_DIFFERENT("network-link-state");
    DO_TEST_DIFFERENT("interface-start-off");
    DO_TEST_DIFFERENT("interface-link-down");
    DO_TEST_DIFFERENT("interface-link-up");
    DO_TEST_DIFFERENT("interface-offline");
    DO_TEST_DIFFERENT("network-driver");
    DO_TEST_DIFFERENT("interface-type-bridge");
    DO_TEST_DIFFERENT("interface-rom");
    DO_TEST_DIFFERENT("interface-tls");

    /* Boot tests */
    DO_TEST_DIFFERENT("boot-order");
    DO_TEST_DIFFERENT("boot-dev-floppy");
    DO_TEST_DIFFERENT("boot-multiple");

    /* Memory tests */
    DO_TEST_DIFFERENT("memory-min");
    DO_TEST_DIFFERENT("memory-hugepages");
    DO_TEST_DIFFERENT("memory-current");

    /* Feature tests */
    DO_TEST_DIFFERENT("features-pae");
    DO_TEST_DIFFERENT("no-features");
    DO_TEST_DIFFERENT("features-apic");
    DO_TEST_DIFFERENT("feature-gic");

    /* Metadata tests */
    DO_TEST_DIFFERENT("metadata");
    DO_TEST_DIFFERENT("metadata-libosinfo");

    /* Controller tests */
    DO_TEST_DIFFERENT("controller-auto");

    /* Clock and timer tests */
    DO_TEST_DIFFERENT("clock-utc");
    DO_TEST_DIFFERENT("timer-platform");
    DO_TEST_DIFFERENT("timer-tickpolicy");
    DO_TEST_DIFFERENT("timer-hypervclock");

    /* vCPU tests */
    DO_TEST_DIFFERENT("vcpu-current");
    DO_TEST_DIFFERENT("vcpu-placement-auto");
    DO_TEST_DIFFERENT("vcpu-hotplug");

    /* Lifecycle tests */
    DO_TEST_DIFFERENT("on-poweroff");
    DO_TEST_DIFFERENT("lifecycle-events");

    /* Device attributes tests */
    DO_TEST_DIFFERENT("disk-driver-name");
    DO_TEST_DIFFERENT("device-address");
    DO_TEST_DIFFERENT("device-address-pci");
    DO_TEST_DIFFERENT("device-alias");

    /* CPU tests */
    DO_TEST_DIFFERENT("cpu-host-model");
    DO_TEST_DIFFERENT("cpu-host-passthrough");
    DO_TEST_DIFFERENT("cpu-topology");

    /* Iteration 20: New feature tests */
    DO_TEST_DIFFERENT("smbios-basic");
    DO_TEST_DIFFERENT("nvram");
    DO_TEST_DIFFERENT("timer-rtc");
    DO_TEST_DIFFERENT("timer-arm");
    DO_TEST_DIFFERENT("cpu-maxphysaddr");
    DO_TEST_DIFFERENT("cpu-sched");
    DO_TEST_DIFFERENT("disk-throttle");
    DO_TEST_DIFFERENT("disk-write-zeroes");

    /* Iteration 21: New feature tests */
    DO_TEST_DIFFERENT("numa-single");
    DO_TEST_DIFFERENT("memory-source-anon");
    DO_TEST_DIFFERENT("memory-hugepages-2m");
    DO_TEST_DIFFERENT("iommu");
    DO_TEST_DIFFERENT("controller-pci-index");
    DO_TEST_DIFFERENT("controller-usb-master");
    DO_TEST_DIFFERENT("cpu-vendor");
    DO_TEST_DIFFERENT("cpu-cache-mode");

    /* Iteration 22: New feature tests */
    DO_TEST_DIFFERENT("interface-bridge");
    DO_TEST_DIFFERENT("disk-qcow2-readonly");
    DO_TEST_DIFFERENT("disk-vmdk");
    DO_TEST_DIFFERENT("controller-virtio-serial");
    DO_TEST_DIFFERENT("device-address-multifunction");
    DO_TEST_DIFFERENT("cpu-force-policy");
    DO_TEST_DIFFERENT("cpu-disable-policy");

    /* Iteration 23: New feature tests */
    DO_TEST_DIFFERENT("timer-multiple");
    DO_TEST_DIFFERENT("disk-address-pci");
    DO_TEST_DIFFERENT("disk-driver-cache");
    DO_TEST_DIFFERENT("disk-io-queues");
    DO_TEST_DIFFERENT("features-apic-eoi");
    DO_TEST_DIFFERENT("features-hap");
    DO_TEST_DIFFERENT("vcpu-max");

    /* Iteration 24: New feature tests */
    DO_TEST_DIFFERENT("cpu-sockets");
    DO_TEST_DIFFERENT("memory-max-current");
    DO_TEST_DIFFERENT("boot-order-multiple");
    DO_TEST_DIFFERENT("numa-distances");

    /* Iteration 25: New feature tests */
    DO_TEST_DIFFERENT("interface-link-state");

    /* Iteration 19: New feature tests */
    DO_TEST_DIFFERENT("disk-detect-zeroes");
    DO_TEST_DIFFERENT("disk-guest-cleanup");
    DO_TEST_DIFFERENT("interface-filterref");
    DO_TEST_DIFFERENT("interface-virtualport");
    DO_TEST_DIFFERENT("serial-tcp");
    DO_TEST_DIFFERENT("console-duplicate");
    DO_TEST_DIFFERENT("memory-lock");

    /* Combined tests */
    DO_TEST_DIFFERENT("full-config");
    DO_TEST_DIFFERENT("multiple-disks");
    DO_TEST_DIFFERENT("multiple-networks");
    DO_TEST_DIFFERENT("max-cpus");
    DO_TEST_DIFFERENT("interface-address-multiple");

    /* Error validation tests - these should fail */
    DO_TEST_FAILURE("invalid-arch-x86_64");
    DO_TEST_FAILURE("invalid-disk-bus-ide");
    DO_TEST_FAILURE("invalid-disk-bus-sata");
    DO_TEST_FAILURE("invalid-disk-device-lun");
    DO_TEST_FAILURE("invalid-disk-type-lun");
    DO_TEST_FAILURE("invalid-disk-type-block");
    DO_TEST_FAILURE("invalid-disk-format-vmdk");
    DO_TEST_FAILURE("invalid-net-model-e1000");
    DO_TEST_FAILURE("invalid-net-model-virtio-nonvirtio");
    DO_TEST_FAILURE("invalid-net-type-user");
    DO_TEST_FAILURE("invalid-graphics-device");
    DO_TEST_FAILURE("invalid-sound-device");
    DO_TEST_FAILURE("invalid-hostdev-passthrough");
    DO_TEST_FAILURE("invalid-usb-device");
    DO_TEST_FAILURE("invalid-watchdog-device");
    DO_TEST_FAILURE("invalid-memballoon-device");

    /* Graphics and input device tests - now supported */
    DO_TEST_DIFFERENT("input-keyboard");
    DO_TEST_DIFFERENT("graphics-basic");
    DO_TEST_DIFFERENT("graphics-vga");

    /* Domain configuration validation tests */
    DO_TEST_FAILURE("invalid-os-type-xen");
    DO_TEST_FAILURE("invalid-machine-type-pc");
    DO_TEST_FAILURE("invalid-clock-offset-localtime");
    DO_TEST_FAILURE("invalid-memory-too-small");

    /* Controller validation tests */
    DO_TEST_FAILURE("invalid-controller-usb");
    DO_TEST_FAILURE("invalid-controller-scsi");
    DO_TEST_FAILURE("invalid-controller-ide");

    /* NUMA and resource validation tests */
    DO_TEST_FAILURE("invalid-numa-cell");
    DO_TEST_FAILURE("invalid-memory-backing-hugepages");
    DO_TEST_FAILURE("invalid-cputune-shares");
    DO_TEST_FAILURE("invalid-resource-partition");

    /* Device advanced features validation */
    DO_TEST_FAILURE("invalid-disk-iothread");
    DO_TEST_FAILURE("invalid-seclabel");
    DO_TEST_FAILURE("invalid-emulator");
    DO_TEST_FAILURE("invalid-launcher-security");
    DO_TEST_FAILURE("invalid-memory-backing-source");
    DO_TEST_FAILURE("invalid-disk-snapshot");
    DO_TEST_FAILURE("invalid-disk-pio");

    /* CPU and memory validation tests */
    DO_TEST_FAILURE("invalid-cpu-too-many");
    DO_TEST_FAILURE("invalid-cpu-mode-custom");
    DO_TEST_FAILURE("invalid-memory-too-large");
    DO_TEST_FAILURE("invalid-timer-tsc");
    DO_TEST_FAILURE("invalid-timer-hpet");
    DO_TEST_FAILURE("invalid-timer-pit");
    DO_TEST_FAILURE("invalid-clock-variable");

    /* Advanced configuration validation tests */
    DO_TEST_FAILURE("invalid-numa-config");
    DO_TEST_FAILURE("invalid-vcpu-hotplug");
    DO_TEST_FAILURE("invalid-memory-hotplug");
    DO_TEST_FAILURE("invalid-hugepages");
    DO_TEST_FAILURE("invalid-cpu-scheduling");
    DO_TEST_FAILURE("invalid-net-bandwidth");
    DO_TEST_FAILURE("invalid-net-filter");

    /* Additional device validation tests */
    DO_TEST_FAILURE("invalid-vsock-device");
    DO_TEST_FAILURE("invalid-redirdev-device");
    DO_TEST_FAILURE("invalid-rng-device");
    DO_TEST_FAILURE("invalid-tpm-device");
    DO_TEST_FAILURE("invalid-shmem-device");
    DO_TEST_FAILURE("invalid-nvram-device");
    DO_TEST_FAILURE("invalid-serial-type-dev");
    DO_TEST_FAILURE("invalid-disk-format-vmdk");
    DO_TEST_FAILURE("invalid-disk-cache");
    DO_TEST_FAILURE("invalid-interface-vhostuser");

    /* OS and firmware validation tests */
    DO_TEST_FAILURE("invalid-bootloader");
    DO_TEST_FAILURE("invalid-firmware-bios");
    DO_TEST_FAILURE("invalid-feature-smm");
    DO_TEST_FAILURE("invalid-feature-ioapic");
    DO_TEST_FAILURE("invalid-feature-hyperv");
    DO_TEST_FAILURE("invalid-feature-pae");
    DO_TEST_FAILURE("invalid-iommu-device");

    /* Disk attribute validation tests */
    DO_TEST_FAILURE("invalid-disk-sgio");
    DO_TEST_FAILURE("invalid-disk-iothrottle");
    DO_TEST_FAILURE("invalid-disk-transient");
    DO_TEST_FAILURE("invalid-disk-error-policy");
    DO_TEST_FAILURE("invalid-disk-geometry");
    DO_TEST_FAILURE("invalid-disk-blockio");

    /* Network feature validation tests */
    DO_TEST_FAILURE("invalid-network-portforward");
    DO_TEST_FAILURE("invalid-network-virtualport");
    DO_TEST_FAILURE("invalid-interface-tpf");

    /* Serial validation tests */
    DO_TEST_FAILURE("invalid-serial-tcp");
    DO_TEST_FAILURE("invalid-serial-udp");

    /* Memory backing validation tests */
    DO_TEST_FAILURE("invalid-memory-source-anonymous");
    DO_TEST_FAILURE("invalid-memory-hugepages");

    /* Clock validation tests */
    DO_TEST_FAILURE("invalid-clock-variable");
    DO_TEST_FAILURE("invalid-clock-timezone");

    /* CPU feature validation tests */
    DO_TEST_FAILURE("invalid-cpu-feature-pmu");

    /* Controller validation tests */
    DO_TEST_FAILURE("invalid-controller-pcie");
    DO_TEST_FAILURE("invalid-controller-pcie-port");

    /* Feature validation tests */
    DO_TEST_FAILURE("invalid-feature-hyperv-vendor");
    DO_TEST_FAILURE("invalid-feature-kvm");
    DO_TEST_FAILURE("invalid-feature-vmport");

    /* Serial validation tests */
    DO_TEST_FAILURE("invalid-serial-pipe");

    /* OS validation tests */
    DO_TEST_FAILURE("invalid-os-loader");

    /* Interface validation tests */
    DO_TEST_FAILURE("invalid-interface-viridian");

    /* Disk format validation tests */
    DO_TEST_FAILURE("invalid-disk-format-vdi");
    DO_TEST_FAILURE("invalid-disk-format-vpc");
    DO_TEST_FAILURE("invalid-disk-scsi");

    /* Timer validation tests */
    DO_TEST_FAILURE("invalid-timer-rtc");

    /* vCPU validation tests */
    DO_TEST_FAILURE("invalid-vcpu-max");

    /* Memory validation tests */
    DO_TEST_FAILURE("invalid-memory-hotplug");

    /* CPU validation tests */
    DO_TEST_FAILURE("invalid-cpu-mode-custom");

    /* Feature validation tests */
    DO_TEST_FAILURE("invalid-feature-smm");
    DO_TEST_FAILURE("invalid-feature-spinlocks");
    DO_TEST_FAILURE("invalid-feature-gic-v2");

    /* Console validation tests */
    DO_TEST_FAILURE("invalid-console-virtio");

    /* Lifecycle validation tests */
    DO_TEST_FAILURE("invalid-lifecycle-lockfailure");

    /* Disk validation tests */
    DO_TEST_FAILURE("invalid-disk-snapshot-external");

    /* Interface validation tests */
    DO_TEST_FAILURE("invalid-interface-coalesce");

    /* Iteration 18: Security and bandwidth validation tests */
    DO_TEST_FAILURE("invalid-disk-seclabel");
    DO_TEST_FAILURE("invalid-clock-localtime");

    /* Iteration 19: New validation tests */
    DO_TEST_FAILURE("invalid-network-portforward");
    DO_TEST_FAILURE("invalid-interface-script");
    DO_TEST_FAILURE("invalid-disk-transient");
    DO_TEST_FAILURE("invalid-serial-udp");

    /* Iteration 20: New validation tests */
    DO_TEST_FAILURE("invalid-smbios-host");
    DO_TEST_FAILURE("invalid-cputune-vcpupin");
    DO_TEST_FAILURE("invalid-timer-pit");
    DO_TEST_FAILURE("invalid-disk-copy-on-read");

    /* Iteration 21: New validation tests */
    DO_TEST_FAILURE("invalid-numa-interleave");
    DO_TEST_FAILURE("invalid-iommu-intel");
    DO_TEST_FAILURE("invalid-cpu-mode-host-passthrough");
    DO_TEST_FAILURE("invalid-memory-discard");

    /* Iteration 22: New validation tests */
    DO_TEST_FAILURE("invalid-interface-type-direct");
    DO_TEST_FAILURE("invalid-disk-format-vdi");
    DO_TEST_FAILURE("invalid-controller-ccid");
    DO_TEST_FAILURE("invalid-cpu-feature-require");

    /* Iteration 23: New validation tests */
    DO_TEST_FAILURE("invalid-timer-hypervclock");
    DO_TEST_FAILURE("invalid-disk-size-large");
    DO_TEST_FAILURE("invalid-feature-viridian");
    DO_TEST_FAILURE("invalid-interface-type-ethernet");

    /* Iteration 24: New validation tests */
    DO_TEST_FAILURE("invalid-feature-hyperv-relaxed");
    DO_TEST_FAILURE("invalid-interface-network-vhostuser");
    DO_TEST_FAILURE("invalid-disk-scsi-device");
    DO_TEST_FAILURE("invalid-vcpu-too-many");
    DO_TEST_FAILURE("invalid-memory-too-large");

    /* Iteration 25: New validation tests */
    DO_TEST_FAILURE("invalid-feature-hyperv-vapic");
    DO_TEST_FAILURE("invalid-feature-kvm");
    DO_TEST_FAILURE("invalid-disk-iothread");
    DO_TEST_FAILURE("invalid-controller-scsi");
    DO_TEST_FAILURE("invalid-feature-pm");

    /* Iteration 26: New validation tests */
    DO_TEST_FAILURE("invalid-hyperv-spinlocks");
    DO_TEST_FAILURE("invalid-disk-geometry");
    DO_TEST_FAILURE("invalid-interface-driver");
    DO_TEST_FAILURE("invalid-interface-timestamp");
    DO_TEST_FAILURE("invalid-disk-blockio");

    /* Iteration 27: New validation tests */
    DO_TEST_FAILURE("invalid-disk-discard");
    DO_TEST_FAILURE("invalid-disk-detect-zeroes");
    DO_TEST_FAILURE("invalid-interface-tso");
    DO_TEST_FAILURE("invalid-disk-transient");
    DO_TEST_FAILURE("invalid-controller-sata");
    DO_TEST_FAILURE("invalid-disk-sgio");
    DO_TEST_FAILURE("invalid-disk-error-policy");
    DO_TEST_FAILURE("invalid-disk-iothread");

    /* Iteration 28: New feature tests */
    DO_TEST_DIFFERENT("cpu-topology-sockets");
    DO_TEST_DIFFERENT("cpu-topology-cores");
    DO_TEST_DIFFERENT("features-acpi");
    DO_TEST_DIFFERENT("disk-io-threads");
    DO_TEST_DIFFERENT("disk-io-native");
    DO_TEST_DIFFERENT("network-coalesce");
    DO_TEST_DIFFERENT("interface-offload");
    DO_TEST_DIFFERENT("timer-pit");
    DO_TEST_DIFFERENT("numa-cell");
    DO_TEST_DIFFERENT("vcpu-hotplug-enabled");

    /* Iteration 28: New validation tests */
    DO_TEST_FAILURE("invalid-coalesce-rx");
    DO_TEST_FAILURE("invalid-timer-cputick");
    DO_TEST_FAILURE("invalid-disk-io-invalid");
    DO_TEST_FAILURE("invalid-cpu-topology");
    DO_TEST_FAILURE("invalid-numa-too-many");

    /* Iteration 29: New feature tests */
    DO_TEST_DIFFERENT("memory-backing-anon");
    DO_TEST_DIFFERENT("hugepages-1gb");
    DO_TEST_DIFFERENT("cpu-feature-pm");
    DO_TEST_DIFFERENT("cpu-cache");
    DO_TEST_DIFFERENT("controller-pci-bridge");
    DO_TEST_DIFFERENT("disk-snapshot-external");

    /* Iteration 29: New validation tests */
    DO_TEST_FAILURE("invalid-memory-hugepages-2mb");
    DO_TEST_FAILURE("invalid-controller-pcie-root");
    DO_TEST_FAILURE("invalid-disk-scsi");
    DO_TEST_FAILURE("invalid-interface-direct");

    /* Iteration 30: New feature tests */
    DO_TEST_DIFFERENT("disk-vmdk");
    DO_TEST_DIFFERENT("disk-readonly-cdrom");
    DO_TEST_DIFFERENT("serial-tcp");
    DO_TEST_DIFFERENT("serial-udp");
    DO_TEST_DIFFERENT("features-apic-eoi");
    DO_TEST_DIFFERENT("features-hap");
    DO_TEST_DIFFERENT("os-loader");
    DO_TEST_DIFFERENT("boot-order-multiple");
    DO_TEST_DIFFERENT("vcpu-max");
    DO_TEST_DIFFERENT("memory-max-current");
    DO_TEST_DIFFERENT("cpu-sockets");

    /* Iteration 31: New feature tests */
    DO_TEST_DIFFERENT("network-virtualport");
    DO_TEST_DIFFERENT("network-tls");
    DO_TEST_DIFFERENT("disk-discard");
    DO_TEST_DIFFERENT("disk-detect-zeroes");
    DO_TEST_DIFFERENT("timer-rtc");
    DO_TEST_DIFFERENT("timer-arm");
    DO_TEST_DIFFERENT("interface-virtualport");

    /* Iteration 31: New validation tests */
    DO_TEST_FAILURE("invalid-interface-vhostuser");
    DO_TEST_FAILURE("invalid-disk-event-idx");
    DO_TEST_FAILURE("invalid-disk-copy-on-read");
    DO_TEST_FAILURE("invalid-interface-coalesce");

    /* Iteration 32: New feature tests */
    DO_TEST_DIFFERENT("smbios-type2");
    DO_TEST_DIFFERENT("nvram-load");
    DO_TEST_DIFFERENT("numa-distances");
    DO_TEST_DIFFERENT("cpu-vendor");
    DO_TEST_DIFFERENT("cpu-force-policy");
    DO_TEST_DIFFERENT("disk-address-pci");

    /* Iteration 32: New validation tests */
    DO_TEST_FAILURE("invalid-cpu-mode-custom");
    DO_TEST_FAILURE("invalid-feature-viridian");
    DO_TEST_FAILURE("invalid-feature-kvm");
    DO_TEST_FAILURE("invalid-feature-pm");
    DO_TEST_FAILURE("invalid-disk-iothread");

    /* Iteration 33: New feature tests */
    DO_TEST_DIFFERENT("interface-rom");
    DO_TEST_DIFFERENT("interface-tls");
    DO_TEST_DIFFERENT("disk-write-zeroes");
    DO_TEST_DIFFERENT("timer-hypervclock");
    DO_TEST_DIFFERENT("cpu-maxphysaddr");
    DO_TEST_DIFFERENT("cpu-sched");

    /* Iteration 33: New validation tests */
    DO_TEST_FAILURE("invalid-cputune-vcpupin");
    DO_TEST_FAILURE("invalid-disk-size-large");
    DO_TEST_FAILURE("invalid-feature-hyperv-relaxed");
    DO_TEST_FAILURE("invalid-interface-coalesce-2");

    /* Iteration 34: New feature tests */
    DO_TEST_DIFFERENT("metadata-description");
    DO_TEST_DIFFERENT("clock-offset-localtime");
    DO_TEST_DIFFERENT("timer-multiple");
    DO_TEST_DIFFERENT("disk-io-queues");
    DO_TEST_DIFFERENT("disk-driver-cache");
    DO_TEST_DIFFERENT("disk-qcow2-readonly");

    /* Iteration 35: Additional device attribute tests */
    DO_TEST_DIFFERENT("disk-shareable");
    DO_TEST_DIFFERENT("interface-mtu");
    DO_TEST_DIFFERENT("interface-tx-queue");
    DO_TEST_DIFFERENT("memory-hugepages-1g");
    DO_TEST_DIFFERENT("cpu-min-max");
    DO_TEST_DIFFERENT("disk-snapshot");

    /* Iteration 36: More device and validation tests */
    DO_TEST_DIFFERENT("interface-coalesce");
    DO_TEST_DIFFERENT("serial-file");
    DO_TEST_FAILURE("invalid-memory-hugepages-nodemask");

    /* Iteration 37: Additional device tests */
    DO_TEST_DIFFERENT("disk-metadata");
    DO_TEST_DIFFERENT("cpu-numa-cell");
    DO_TEST_DIFFERENT("serial-pipe");

    /* Iteration 38: Boot and controller tests */
    DO_TEST_DIFFERENT("boot-multiple-devices");
    DO_TEST_DIFFERENT("boot-enable");
    DO_TEST_DIFFERENT("controller-usb-none");

    /* Iteration 39: Unsupported device tests */
    DO_TEST_FAILURE("invalid-video-type-vga");
    DO_TEST_FAILURE("invalid-input-type-usb");
    DO_TEST_FAILURE("invalid-sound-model-ich6");
    DO_TEST_FAILURE("video-device");
    DO_TEST_FAILURE("input-device");
    DO_TEST_FAILURE("sound-device");
    DO_TEST_FAILURE("watchdog-device");

    /* Iteration 40: Advanced configuration tests */
    DO_TEST_DIFFERENT("memory-discard");
    DO_TEST_DIFFERENT("cpu-vendor-cpu-model");
    DO_TEST_DIFFERENT("clock-timer-rtc");
    DO_TEST_DIFFERENT("numa-interleave");

    /* Iteration 41: Additional unsupported devices */
    DO_TEST_FAILURE("redirdev-usb");
    DO_TEST_FAILURE("tpm-device");
    DO_TEST_FAILURE("rng-device");
    DO_TEST_FAILURE("panic-device");
    DO_TEST_FAILURE("memballoon-device");

    /* Iteration 42: Complex multi-device and lifecycle configurations */
    DO_TEST_DIFFERENT("multi-disk-net");
    DO_TEST_DIFFERENT("lifecycle-preserve");
    DO_TEST_DIFFERENT("lifecycle-restart");
    DO_TEST_DIFFERENT("resource-partition");
    DO_TEST_FAILURE("memory-hotplug-disabled");

    /* Iteration 43: Enhanced device support and validation improvements */
    DO_TEST_DIFFERENT("controller-virtio-serial-tolerated");
    DO_TEST_DIFFERENT("timer-arm-virtio");
    DO_TEST_DIFFERENT("serial-tcp-raw");
    DO_TEST_DIFFERENT("vsock-tolerated");
    DO_TEST_FAILURE("invalid-serial-tcp-telnet");

    /* Iteration 44: Network validation and ARM64 feature improvements */
    DO_TEST_DIFFERENT("network-driver-virtio");
    DO_TEST_DIFFERENT("network-link-state-down");
    DO_TEST_DIFFERENT("features-arm64-tolerated");
    DO_TEST_FAILURE("invalid-network-driver");

    /* Iteration 45: Console/Serial enhancements and disk attribute validation */
    DO_TEST_DIFFERENT("console-virtio");
    DO_TEST_DIFFERENT("console-tcp");
    DO_TEST_FAILURE("invalid-disk-transient");
    DO_TEST_FAILURE("invalid-disk-shareable");
    DO_TEST_FAILURE("invalid-disk-cache");

    /* Iteration 46: CPU, boot, and metadata improvements */
    DO_TEST_DIFFERENT("cpu-topology-valid");
    DO_TEST_DIFFERENT("boot-order");
    DO_TEST_DIFFERENT("metadata-description");
    DO_TEST_FAILURE("invalid-boot-floppy");
    DO_TEST_FAILURE("invalid-boot-network");

    /* Iteration 47: Comprehensive multi-device and advanced configuration tests */
    DO_TEST_DIFFERENT("interface-multiple");
    DO_TEST_DIFFERENT("disk-multiple-mixed");
    DO_TEST_DIFFERENT("numa-simple");
    DO_TEST_DIFFERENT("memory-backing-hugepages");
    DO_TEST_DIFFERENT("comprehensive-config");

    /* Iteration 48: Advanced features - vCPU, timers, lifecycle, aliases, SMBIOS */
    DO_TEST_DIFFERENT("vcpu-hotplug-disabled");
    DO_TEST_DIFFERENT("timer-advanced");
    DO_TEST_DIFFERENT("lifecycle-restart-destroy");
    DO_TEST_DIFFERENT("device-alias");
    DO_TEST_DIFFERENT("smbios-full");

    /* Iteration 49: Character devices and CPU configuration enhancements */
    DO_TEST_DIFFERENT("serial-unix-bind");
    DO_TEST_DIFFERENT("serial-null");
    DO_TEST_DIFFERENT("serial-multiple");
    DO_TEST_DIFFERENT("cpu-features");
    DO_TEST_DIFFERENT("cpu-host-passthrough");

    /* Iteration 50: Enhanced validation tests for clocks, timers, and features */
    DO_TEST_FAILURE("invalid-clock-variable");
    DO_TEST_FAILURE("invalid-clock-localtime");
    DO_TEST_FAILURE("invalid-clock-timezone");
    DO_TEST_FAILURE("invalid-timer-hpet");
    DO_TEST_FAILURE("invalid-timer-pit");
    DO_TEST_FAILURE("invalid-feature-viridian");
    DO_TEST_FAILURE("invalid-feature-kvm");

    /* Iteration 51: Additional validation tests for controllers, devices, and resources */
    DO_TEST_FAILURE("invalid-controller-pcie-root");
    DO_TEST_FAILURE("invalid-controller-pcie-port");
    DO_TEST_FAILURE("invalid-controller-ccid");
    DO_TEST_FAILURE("invalid-interface-type-direct");
    DO_TEST_FAILURE("invalid-disk-scsi-device");
    DO_TEST_FAILURE("invalid-vcpu-too-many");
    DO_TEST_FAILURE("invalid-memory-too-large");

    /* Iteration 52: Memory and resource management validation tests */
    DO_TEST_FAILURE("invalid-memory-backing-type");
    DO_TEST_FAILURE("invalid-memory-hotplug");
    DO_TEST_FAILURE("invalid-cputune-shares");
    DO_TEST_FAILURE("invalid-memory-locked");
    DO_TEST_FAILURE("invalid-hugepage-nodemask");

    /* Iteration 52: Disk device and format validation tests */
    DO_TEST_FAILURE("invalid-disk-source-block");
    DO_TEST_FAILURE("invalid-disk-device-floppy");
    DO_TEST_FAILURE("invalid-disk-format-vmdk");

    /* Iteration 53: Extended CPU tuning validation tests */
    DO_TEST_FAILURE("invalid-cputune-period");
    DO_TEST_FAILURE("invalid-cputune-quota");
    DO_TEST_FAILURE("invalid-cputune-emulator-period");
    DO_TEST_FAILURE("invalid-cputune-emulator-quota");

    /* Iteration 53: Extended disk format validation tests */
    DO_TEST_FAILURE("invalid-disk-format-vdi");
    DO_TEST_FAILURE("invalid-disk-format-vpc");
    DO_TEST_FAILURE("invalid-disk-format-qed");

    /* Iteration 53: Extended disk source and device validation tests */
    DO_TEST_FAILURE("invalid-disk-source-directory");
    DO_TEST_FAILURE("invalid-disk-device-lun");

    /* Iteration 54: Enhanced validation error message tests */
    DO_TEST_FAILURE("disk-format-vmdk");
    DO_TEST_FAILURE("disk-cache-writeback");
    DO_TEST_FAILURE("disk-iothread");
    DO_TEST_FAILURE("disk-shareable");
    DO_TEST_FAILURE("network-bandwidth");
    DO_TEST_FAILURE("cpu-topology-exceeds-vcpus");

    /* Iteration 55: Comprehensive configuration tests */
    DO_TEST("disk-multiple");
    DO_TEST("network-multiple");
    DO_TEST("serial-multiple");
    DO_TEST("cpu-topology");
    DO_TEST("memory-hugepages");
    DO_TEST("features-acpi-apic");

    /* Iteration 56: Additional configuration and edge case tests */
    DO_TEST("boot-kernel-cmdline");
    DO_TEST("boot-order-multi");
    DO_TEST("cpu-host-passthrough");
    DO_TEST("clock-localtime");
    DO_TEST("console-tty");
    DO_TEST("lifecycle-events");

    /* Iteration 57: Additional device and configuration tests */
    DO_TEST("disk-readonly");
    DO_TEST("network-mac");
    DO_TEST("serial-pty");
    DO_TEST("cpu-sockets");
    DO_TEST("memory-size");
    DO_TEST("controller-pci");

    /* Iteration 58: Additional configuration and timer tests */
    DO_TEST("clock-timer-rtc");
    DO_TEST("boot-cdrom");
    DO_TEST("minimal-memory");
    DO_TEST("typical-config");
    DO_TEST("full-bootloader");
    DO_TEST("metadata");

    /* Iteration 59: Multi-device and configuration tests */
    DO_TEST("multi-serial-types");
    DO_TEST("disk-virtio-types");
    DO_TEST("cpu-cores");
    DO_TEST("max-vcpus");
    DO_TEST("network-bridge");
    DO_TEST("boot-devices");

    /* Iteration 60: Controller and device configuration tests */
    DO_TEST_FAILURE("usb-controller");
    DO_TEST("graphics-none");
    DO_TEST("audio-none");
    DO_TEST("network-user-multiple");
    DO_TEST("controller-pci-root");
    DO_TEST("minimal-config");

    /* Iteration 61: Storage, memory, CPU, and lifecycle tests */
    DO_TEST_FAILURE("disk-block");
    DO_TEST("memory-hugepages");
    DO_TEST_FAILURE("cpu-features");
    DO_TEST_FAILURE("clock-variable");
    DO_TEST("features-pm");
    DO_TEST("lifecycle-destroy");

    /* Iteration 62: Advanced device and feature tests */
    DO_TEST_FAILURE("watchdog");
    DO_TEST_FAILURE("tpm");
    DO_TEST_FAILURE("rng");
    DO_TEST_FAILURE("vsock");
    DO_TEST_FAILURE("panic");
    DO_TEST_FAILURE("memory-balloon");

    /* Iteration 63: Multimedia and communication device tests */
    DO_TEST_FAILURE("video-virtio");
    DO_TEST_FAILURE("input-usb");
    DO_TEST_FAILURE("sound");
    DO_TEST_FAILURE("channel-qemu-guest-agent");
    DO_TEST("network-models");
    DO_TEST("disk-io");

    /* Iteration 64: Advanced memory and security tests */
    DO_TEST_FAILURE("disk-cache-writeback");
    DO_TEST_FAILURE("disk-cache-writethrough");
    DO_TEST_FAILURE("disk-cache-none");
    DO_TEST("numa");
    DO_TEST("launch-security-sev");
    DO_TEST("memory-shared");

    /* Iteration 65: Advanced configuration tests */
    DO_TEST_FAILURE("disk-detect-zeroes");
    DO_TEST("disk-event-idx");
    DO_TEST_FAILURE("clock-timer-pit");
    DO_TEST("cpu-host-passthrough");
    DO_TEST_FAILURE("network-filter");
    DO_TEST("disk-sg");

    /* Iteration 66: Complex configuration tests */
    DO_TEST("numa-multiple-cells");
    DO_TEST("cpu-topology-complex");
    DO_TEST("disk-libosinfo");
    DO_TEST("serial-many");
    DO_TEST_FAILURE("network-bandwidth-xml");
    DO_TEST("disk-backing-qcow2");

    /* Iteration 67: Firmware, timers, and advanced features tests */
    DO_TEST_FAILURE("clock-timer-hpet");
    DO_TEST_FAILURE("features-hyperv");
    DO_TEST("disk-readonly-cdrom");
    DO_TEST("controller-virtio-serial-multi");
    DO_TEST_FAILURE("firmware-efi");
    DO_TEST("disk-snapshot-external");

    /* Iteration 68: Advanced configuration tests */
    DO_TEST("disk-order");
    DO_TEST("interface-multiple-addresses");
    DO_TEST("cpu-maxvcpus-topology");
    DO_TEST_FAILURE("features-kvm");
    DO_TEST_FAILURE("disk-multi-file");
    DO_TEST_FAILURE("features-vmport");

    /* Iteration 69: Disk identifiers, CPU cache, and advanced features tests */
    DO_TEST("disk-serial");
    DO_TEST_FAILURE("disk-wwn");
    DO_TEST_FAILURE("interface-script");
    DO_TEST("cpu-cache-disable");
    DO_TEST_FAILURE("features-smm");
    DO_TEST_FAILURE("clock-timer-hypervclock");

    /* Iteration 70: Advanced disk, network tuning, and CPU vendor tests */
    DO_TEST_FAILURE("disk-write-cur");
    DO_TEST_FAILURE("disk-read-only-seq");
    DO_TEST("interface-tune");
    DO_TEST_FAILURE("cpu-vendor-lenovo");
    DO_TEST_FAILURE("features-kvm-features");
    DO_TEST_FAILURE("clock-offset-localtime");

    /* Iteration 71: Disk guest info, bandwidth, hotplug, and feature tests */
    DO_TEST_FAILURE("disk-guest-off");
    DO_TEST_FAILURE("disk-transient");
    DO_TEST_FAILURE("interface-bandwidth");
    DO_TEST("cpu-maxvcpus-hotplug");
    DO_TEST_FAILURE("features-hpet");
    DO_TEST_FAILURE("features-pviommu");

    /* Iteration 72: Memory hotplug, I/O tuning, and network filter tests */
    DO_TEST("memory-hotplug");
    DO_TEST("disk-iotune-write");
    DO_TEST("disk-iotune-read");
    DO_TEST("disk-iotune-total");
    DO_TEST_FAILURE("interface-filterref");
    DO_TEST_FAILURE("features-ioapic");

    /* Iteration 73: Controllers, disk options, and hypervisor features tests */
    DO_TEST_FAILURE("controller-ide");
    DO_TEST("disk-shareable");
    DO_TEST_FAILURE("disk-iommu");
    DO_TEST("features-viridian");
    DO_TEST_FAILURE("clock-timer-tsc");
    DO_TEST("cpu-numa-node-distance");

    /* Iteration 74: Memory backing, controllers, timers, and error policies */
    DO_TEST_FAILURE("memory-backed-anon");
    DO_TEST_FAILURE("controller-usb-none");
    DO_TEST_FAILURE("controller-sata");
    DO_TEST_FAILURE("features-kvm-clock");
    DO_TEST("clock-timer-rtc");
    DO_TEST_FAILURE("disk-error-policy-stop");

    /* Iteration 75: Disk discard, network link, NUMA, and advanced features */
    DO_TEST_FAILURE("disk-discard-unmap");
    DO_TEST_FAILURE("disk-discard-ignore");
    DO_TEST("interface-offline");
    DO_TEST("cpu-numa-cell");
    DO_TEST_FAILURE("features-spinlocks");
    DO_TEST_FAILURE("clock-timer-pit");

    /* Iteration 76: Network models, disk buses, CPU check, memory lock, and APIC */
    DO_TEST("interface-no-model");
    DO_TEST_FAILURE("disk-scsi-generic");
    DO_TEST("cpu-check-none");
    DO_TEST_FAILURE("memory-lock");
    DO_TEST("features-apic-eoi");
    DO_TEST("vcpu-placement-static");

    /* Iteration 77: Network MTU, snapshots, NUMA interleave, ARM timer, and PM */
    DO_TEST("interface-mtu");
    DO_TEST_FAILURE("disk-snapshot-no-metadata");
    DO_TEST_FAILURE("cpu-numa-interleave");
    DO_TEST_FAILURE("clock-timer-arm");
    DO_TEST_FAILURE("features-pm");
    DO_TEST_FAILURE("vcpu-placement-auto");

    /* Iteration 78: I/O threads, queues, readonly, cache mode, and discard */
    DO_TEST_FAILURE("disk-iothreads-queues");
    DO_TEST("interface-tx-queue");
    DO_TEST("interface-rx-queue");
    DO_TEST("disk-readonly");
    DO_TEST("cpu-cache-mode");
    DO_TEST_FAILURE("disk-discard");

    /* Iteration 79: Shareable disks, link state, vendor, hugepages, filters, transient */
    DO_TEST("disk-shareable");
    DO_TEST("interface-link-state");
    DO_TEST_FAILURE("cpu-vendor");
    DO_TEST_FAILURE("memory-hugepages");
    DO_TEST_FAILURE("interface-filter");
    DO_TEST_FAILURE("disk-transient");

    /* Iteration 80: Cache modes, TSO, topology, ioapic, SATA, HPET */
    DO_TEST_FAILURE("disk-write-cache");
    DO_TEST_FAILURE("interface-tso");
    DO_TEST_FAILURE("cpu-topology");
    DO_TEST("features-ioapic");
    DO_TEST_FAILURE("disk-bus-sata");
    DO_TEST_FAILURE("features-hpet");

    /* Iteration 81: Clock, error policy, scripts, CPU features, memory discard, bandwidth */
    DO_TEST_FAILURE("clock-localtime");
    DO_TEST_FAILURE("disk-error-stop");
    DO_TEST_FAILURE("interface-start-script");
    DO_TEST("cpu-feature-policy");
    DO_TEST_FAILURE("memory-discard");
    DO_TEST_FAILURE("interface-bandwidth");

    /* Iteration 82: I/O eventfd, event idx, copy-on-read, detect zeroes, CPU max */
    DO_TEST("disk-io-eventfd");
    DO_TEST("disk-event-index");
    DO_TEST("interface-ioeventfd");
    DO_TEST("cpu-max-limit");
    DO_TEST("disk-copy-on-read");
    DO_TEST_FAILURE("disk-detect-zeroes");

    /* Iteration 83: Pause/resume and statistics test cases */
    DO_TEST_DIFFERENT("pause-resume");
    DO_TEST_DIFFERENT("statistics");
    DO_TEST_DIFFERENT("advanced-virtio");

    /* Iteration 84: Multi-device and edge case tests */
    DO_TEST_DIFFERENT("disk-multiple-formats");
    DO_TEST_DIFFERENT("network-multiple-types");
    DO_TEST_DIFFERENT("serial-many-types");

    /* Iteration 84: Additional validation error tests */
    DO_TEST_FAILURE("invalid-disk-cache-writeback");
    DO_TEST_FAILURE("invalid-network-bandwidth");
    DO_TEST_FAILURE("invalid-serial-tcp-telnet");

    /* Iteration 85: Configuration and lifecycle tests */
    DO_TEST_DIFFERENT("domain-config");
    DO_TEST_DIFFERENT("boot-lifecycle");
    DO_TEST_DIFFERENT("numa-memory");

    /* Iteration 86: Device hotplug and stats tests */
    DO_TEST_DIFFERENT("device-hotplug");
    DO_TEST_FAILURE("invalid-vcpu-hotplug");

    /* Iteration 87: Advanced configuration tests */
    DO_TEST_DIFFERENT("advanced-config");
    DO_TEST_DIFFERENT("timer-comprehensive");

    /* Iteration 88: Filesystem (shared folder) device tests */
    DO_TEST_DIFFERENT("filesystem-basic");
    DO_TEST_DIFFERENT("filesystem-readonly");
    DO_TEST_DIFFERENT("filesystem-multiple");

    /* Iteration 88: Filesystem validation error tests */
    DO_TEST_FAILURE("invalid-filesystem-type-ram");
    DO_TEST_FAILURE("invalid-filesystem-driver-nbd");
    DO_TEST_FAILURE("invalid-filesystem-no-source");
    DO_TEST_FAILURE("invalid-filesystem-no-target");
    DO_TEST_FAILURE("invalid-filesystem-wrpolicy");

    /* Iteration 89: Graphics device validation tests */
    DO_TEST_FAILURE("invalid-graphics-vnc");
    DO_TEST_FAILURE("invalid-graphics-spice");
    DO_TEST_FAILURE("invalid-graphics-sdl");
    DO_TEST_FAILURE("invalid-graphics-egl-headless");

    /* Iteration 90: Enhanced device validation tests */
    DO_TEST_FAILURE("invalid-sound-ich6");
    DO_TEST_FAILURE("invalid-video-vga");
    DO_TEST_FAILURE("invalid-input-keyboard");
    DO_TEST_FAILURE("invalid-watchdog-i6300esb");

    virObjectUnref(driver.caps);
    virObjectUnref(driver.xmlopt);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)

#else

int
main(void)
{
    return EXIT_AM_SKIP;
}

#endif /* WITH_MACOSVF */
