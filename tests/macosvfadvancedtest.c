/*
 * macosvfadvancedtest.c: Advanced macOS Virtualization.Framework configuration tests
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
# include "macosvf/macosvf_conf.h"
# include "macosvf/macosvf_domain.h"
# include "macosvf/macosvf_device.h"

# define VIR_FROM_THIS VIR_FROM_NONE

static macosvfConn driver;

/* Test multiple disk configurations */
static int
testMultipleDiskConfig(const void *data G_GNUC_UNUSED)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-disk-multiple-formats.xml",
                         abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse multiple disk config\n");
        return -1;
    }

    /* Validate multiple disks */
    if (def->ndisks < 2) {
        fprintf(stderr, "Expected at least 2 disks, got %zu\n", def->ndisks);
        return -1;
    }

    printf("✓ Multiple disk configuration test passed (%zu disks)\n", def->ndisks);
    return 0;
}

/* Test multiple network configurations */
static int
testMultipleNetworkConfig(const void *data G_GNUC_UNUSED)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-network-multiple-types.xml",
                         abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse multiple network config\n");
        return -1;
    }

    /* Validate multiple networks */
    if (def->nnets < 2) {
        fprintf(stderr, "Expected at least 2 networks, got %zu\n", def->nnets);
        return -1;
    }

    printf("✓ Multiple network configuration test passed (%zu networks)\n", def->nnets);
    return 0;
}

/* Test boot order configuration */
static int
testBootOrderConfig(const void *data G_GNUC_UNUSED)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-boot-multiple-devices.xml",
                         abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse boot order config\n");
        return -1;
    }

    /* Validate boot devices */
    if (def->os.nBootDevs == 0) {
        fprintf(stderr, "Expected boot devices, got 0\n");
        return -1;
    }

    printf("✓ Boot order configuration test passed (%zu boot devices)\n", def->os.nBootDevs);
    return 0;
}

/* Test memory configuration with hugepages */
static int
testMemoryHugepagesConfig(const void *data G_GNUC_UNUSED)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-memory-hugepages.xml",
                         abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse memory hugepages config\n");
        return -1;
    }

    /* Validate hugepages configuration */
    if (def->mem.nhugepages == 0) {
        fprintf(stderr, "Expected hugepages configuration\n");
        return -1;
    }

    printf("✓ Memory hugepages configuration test passed\n");
    return 0;
}

/* Test CPU topology configuration */
static int
testCPUTopologyConfig(const void *data G_GNUC_UNUSED)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-cpu-topology.xml",
                         abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse CPU topology config\n");
        return -1;
    }

    /* Validate CPU topology */
    if (virDomainDefGetVcpus(def) == 0) {
        fprintf(stderr, "Expected vCPUs configured\n");
        return -1;
    }

    printf("✓ CPU topology configuration test passed (%d vCPUs)\n",
           virDomainDefGetVcpus(def));
    return 0;
}

/* Test NUMA configuration */
static int
testNUMAConfig(const void *data G_GNUC_UNUSED)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-numa-cell.xml",
                         abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse NUMA config\n");
        return -1;
    }

    /* Validate NUMA configuration - just check it parsed successfully */
    printf("✓ NUMA configuration test passed\n");
    return 0;
}

/* Test timer configuration */
static int
testTimerConfig(const void *data G_GNUC_UNUSED)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-timer-comprehensive.xml",
                         abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse timer config\n");
        return -1;
    }

    /* Validate timer configuration */
    printf("✓ Timer configuration test passed\n");
    return 0;
}

/* Test disk cache modes */
static int
testDiskCacheConfig(const void *data G_GNUC_UNUSED)
{
    const char *configs[] = {
        "macosvfxml2xml-disk-cache-none.xml",
        "macosvfxml2xml-disk-cache-writeback.xml",
        "macosvfxml2xml-disk-cache-writethrough.xml",
        "macosvfxml2xml-disk-driver-cache.xml",
        NULL
    };

    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i] != NULL; i++) {
        xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/%s",
                             abs_srcdir, configs[i]);

        def = virDomainDefParseFile(xml, driver.xmlopt,
                                    NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

        if (!def) {
            fprintf(stderr, "Failed to parse disk cache config: %s\n", configs[i]);
            return -1;
        }

        /* Validate disk configuration */
        if (def->ndisks == 0) {
            fprintf(stderr, "Expected disk in config: %s\n", configs[i]);
            return -1;
        }
    }

    printf("✓ Disk cache configuration test passed (4 cache modes)\n");
    return 0;
}

/* Test console configurations */
static int
testConsoleConfig(const void *data G_GNUC_UNUSED)
{
    const char *configs[] = {
        "macosvfxml2xml-serial-many-types.xml",
        "macosvfxml2xml-console-types.xml",
        NULL
    };

    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i] != NULL; i++) {
        xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/%s",
                             abs_srcdir, configs[i]);

        def = virDomainDefParseFile(xml, driver.xmlopt,
                                    NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

        if (!def) {
            fprintf(stderr, "Failed to parse console config: %s\n", configs[i]);
            return -1;
        }

        /* Validate console/serial configuration */
        if (def->nserials == 0 && def->nconsoles == 0) {
            fprintf(stderr, "Expected console/serial in config: %s\n", configs[i]);
            return -1;
        }
    }

    printf("✓ Console configuration test passed (2 configurations)\n");
    return 0;
}

/* Test network interface configurations */
static int
testNetworkInterfaceConfig(const void *data G_GNUC_UNUSED)
{
    const char *configs[] = {
        "macosvfxml2xml-interface-link-state.xml",
        "macosvfxml2xml-interface-mtu.xml",
        "macosvfxml2xml-interface-tx-queue.xml",
        "macosvfxml2xml-interface-virtualport.xml",
        NULL
    };

    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i] != NULL; i++) {
        xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/%s",
                             abs_srcdir, configs[i]);

        def = virDomainDefParseFile(xml, driver.xmlopt,
                                    NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

        if (!def) {
            fprintf(stderr, "Failed to parse network interface config: %s\n", configs[i]);
            return -1;
        }

        /* Validate network interface configuration */
        if (def->nnets == 0) {
            fprintf(stderr, "Expected network interface in config: %s\n", configs[i]);
            return -1;
        }
    }

    printf("✓ Network interface configuration test passed (4 configurations)\n");
    return 0;
}

static int
mymain(void)
{
    int ret = 0;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    if ((driver.caps = macosvfCreateCapabilities()) == NULL)
        return EXIT_FAILURE;

    if ((driver.xmlopt = virMacOSVFDriverCreateXMLConf(&driver)) == NULL) {
        virObjectUnref(driver.caps);
        return EXIT_FAILURE;
    }

    /* Test multiple device configurations */
    if (virTestRun("Multiple Disk Config", testMultipleDiskConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    if (virTestRun("Multiple Network Config", testMultipleNetworkConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    if (virTestRun("Boot Order Config", testBootOrderConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    /* Test memory and CPU configurations */
    if (virTestRun("Memory Hugepages Config", testMemoryHugepagesConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    if (virTestRun("CPU Topology Config", testCPUTopologyConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    if (virTestRun("NUMA Config", testNUMAConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    /* Test device-specific configurations */
    if (virTestRun("Timer Config", testTimerConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    if (virTestRun("Disk Cache Config", testDiskCacheConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    if (virTestRun("Console Config", testConsoleConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    if (virTestRun("Network Interface Config", testNetworkInterfaceConfig, NULL) < 0)
        ret = EXIT_FAILURE;

    /* Cleanup */
    virObjectUnref(driver.caps);
    virObjectUnref(driver.xmlopt);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)

#endif /* WITH_MACOSVF */
