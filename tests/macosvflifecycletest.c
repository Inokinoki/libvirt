/*
 * macosvflifecycletest.c: test macOS Virtualization.Framework domain lifecycle and configuration
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
# include "conf/domain_conf.h"

# define VIR_FROM_THIS VIR_FROM_NONE

static macosvfConn driver;

struct testInfo {
    const char *name;
};

/* Test domain definition parsing with metadata */
static int
testDomainParseMetadata(const void *data)
{
    const struct testInfo *info = data;
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-%s.xml",
                         abs_srcdir, info->name);

    if (!(def = virDomainDefParseFile(xml, driver.xmlopt,
                                       NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE)))
        return -1;

    /* Verify metadata is preserved */
    if (!def->metadata) {
        fprintf(stderr, "%s: Metadata should be preserved\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

/* Test domain definition with boot configuration */
static int
testDomainParseBootConfig(const void *data)
{
    const struct testInfo *info = data;
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-%s.xml",
                         abs_srcdir, info->name);

    if (!(def = virDomainDefParseFile(xml, driver.xmlopt,
                                       NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE)))
        return -1;

    /* Verify boot devices */
    if (def->os.nBootDevs == 0) {
        fprintf(stderr, "%s: Boot devices should be defined\n", __FUNCTION__);
        return -1;
    }

    /* Verify boot order */
    for (size_t i = 0; i < def->os.nBootDevs; i++) {
        if (def->os.bootDevs[i] != VIR_DOMAIN_BOOT_DISK &&
            def->os.bootDevs[i] != VIR_DOMAIN_BOOT_CDROM) {
            fprintf(stderr, "%s: Invalid boot device %d\n",
                    __FUNCTION__, def->os.bootDevs[i]);
            return -1;
        }
    }

    return 0;
}

/* Test domain definition with lifecycle events */
static int
testDomainParseLifecycle(const void *data)
{
    const struct testInfo *info = data;
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-%s.xml",
                         abs_srcdir, info->name);

    if (!(def = virDomainDefParseFile(xml, driver.xmlopt,
                                       NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE)))
        return -1;

    /* Verify lifecycle actions */
    if (def->onPoweroff != VIR_DOMAIN_LIFECYCLE_DESTROY &&
        def->onPoweroff != VIR_DOMAIN_LIFECYCLE_RESTART) {
        fprintf(stderr, "%s: Invalid on_poweroff action\n", __FUNCTION__);
        return -1;
    }

    if (def->onReboot != VIR_DOMAIN_LIFECYCLE_RESTART &&
        def->onReboot != VIR_DOMAIN_LIFECYCLE_DESTROY) {
        fprintf(stderr, "%s: Invalid on_reboot action\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

/* Test domain definition with NUMA configuration */
static int
testDomainParseNUMA(const void *data)
{
    const struct testInfo *info = data;
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-%s.xml",
                         abs_srcdir, info->name);

    if (!(def = virDomainDefParseFile(xml, driver.xmlopt,
                                       NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE)))
        return -1;

    /* Verify NUMA configuration */
    if (def->numa == NULL || def->numa->nCells == 0) {
        fprintf(stderr, "%s: NUMA cells should be defined\n", __FUNCTION__);
        return -1;
    }

    /* Verify NUMA cell count is reasonable */
    if (def->numa->nCells > 2) {
        fprintf(stderr, "%s: NUMA cell count %zu exceeds reasonable limit\n",
                __FUNCTION__, def->numa->nCells);
        return -1;
    }

    return 0;
}

/* Test domain definition with memory backing */
static int
testDomainParseMemoryBacking(const void *data)
{
    const struct testInfo *info = data;
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-%s.xml",
                         abs_srcdir, info->name);

    if (!(def = virDomainDefParseFile(xml, driver.xmlopt,
                                       NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE)))
        return -1;

    /* Verify memory backing */
    if (def->mem.nhugepages > 0) {
        /* Hugepages are supported but nodemask is not */
        for (size_t i = 0; i < def->mem.nhugepages; i++) {
            if (def->mem.hugepages[i].nodemask) {
                fprintf(stderr, "%s: Hugepage nodemask is not supported\n",
                        __FUNCTION__);
                return -1;
            }
        }
    }

    return 0;
}

/* Test domain definition with multiple devices */
static int
testDomainParseMultipleDevices(const void *data)
{
    const struct testInfo *info = data;
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-%s.xml",
                         abs_srcdir, info->name);

    if (!(def = virDomainDefParseFile(xml, driver.xmlopt,
                                       NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE)))
        return -1;

    /* Verify multiple disks */
    if (def->ndisks < 2) {
        fprintf(stderr, "%s: Expected at least 2 disks, got %zu\n",
                __FUNCTION__, def->ndisks);
        return -1;
    }

    /* Verify multiple networks */
    if (def->nnets < 1) {
        fprintf(stderr, "%s: Expected at least 1 network, got %zu\n",
                __FUNCTION__, def->nnets);
        return -1;
    }

    /* Verify serial/console devices */
    if (def->nserials < 1 && def->nconsoles < 1) {
        fprintf(stderr, "%s: Expected at least 1 serial or console device\n",
                __FUNCTION__);
        return -1;
    }

    return 0;
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

    struct testInfo info;

    /* Test metadata handling */
    info.name = "domain-config";
    if (virTestRun("MACOSVF Domain Parse Metadata",
                   testDomainParseMetadata, &info) < 0)
        ret = -1;

    /* Test boot configuration */
    info.name = "boot-lifecycle";
    if (virTestRun("MACOSVF Domain Parse Boot Config",
                   testDomainParseBootConfig, &info) < 0)
        ret = -1;

    /* Test lifecycle events */
    if (virTestRun("MACOSVF Domain Parse Lifecycle",
                   testDomainParseLifecycle, &info) < 0)
        ret = -1;

    /* Test NUMA configuration */
    info.name = "numa-memory";
    if (virTestRun("MACOSVF Domain Parse NUMA",
                   testDomainParseNUMA, &info) < 0)
        ret = -1;

    /* Test memory backing */
    if (virTestRun("MACOSVF Domain Parse Memory Backing",
                   testDomainParseMemoryBacking, &info) < 0)
        ret = -1;

    /* Test multiple devices */
    info.name = "disk-multiple-formats";
    if (virTestRun("MACOSVF Domain Parse Multiple Devices",
                   testDomainParseMultipleDevices, &info) < 0)
        ret = -1;

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
