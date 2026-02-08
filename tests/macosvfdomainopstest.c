/*
 * macosvfdomainopstest.c: test macOS Virtualization.Framework domain operations
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

# define VIR_FROM_THIS VIR_FROM_NONE

static macosvfConn driver;

/* Test domain XML parsing with various configurations */
static int
testDomainParseVariousConfigs(const void *data)
{
    const char *configs[] = {
        "minimal",
        "basic",
        "with-kernel",
        "with-initrd",
        "with-cmdline",
        "advanced-config",
        "timer-comprehensive",
        "device-hotplug",
        "domain-config",
        "boot-lifecycle",
        "numa-memory",
        NULL
    };

    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i] != NULL; i++) {
        xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-%s.xml",
                             abs_srcdir, configs[i]);

        def = virDomainDefParseFile(xml, driver.xmlopt,
                                    NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

        if (!def) {
            fprintf(stderr, "Failed to parse config: %s\n", configs[i]);
            return -1;
        }

        /* Validate basic properties */
        if (def->os.arch != VIR_ARCH_AARCH64) {
            fprintf(stderr, "Config %s: Expected aarch64 architecture\n", configs[i]);
            return -1;
        }

        if (def->os.type != VIR_DOMAIN_OSTYPE_HVM) {
            fprintf(stderr, "Config %s: Expected HVM OS type\n", configs[i]);
            return -1;
        }

        g_free(xml);
        virDomainDefFree(def);
        def = NULL;
    }

    return 0;
}

/* Test domain metadata preservation */
static int
testDomainMetadataPreservation(const void *data)
{
    g_autofree char *xml_in = NULL;
    g_autofree char *xml_out = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml_in = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-domain-config.xml",
                           abs_srcdir);

    def = virDomainDefParseFile(xml_in, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse domain config\n");
        return -1;
    }

    /* Check metadata is preserved */
    if (!def->metadata) {
        fprintf(stderr, "Metadata should be preserved\n");
        return -1;
    }

    /* Format back to XML */
    xml_out = virDomainDefFormat(def, NULL, VIR_DOMAIN_XML_INACTIVE);
    if (!xml_out) {
        fprintf(stderr, "Failed to format domain XML\n");
        return -1;
    }

    /* Verify metadata is in output */
    if (strstr(xml_out, "<metadata>") == NULL) {
        fprintf(stderr, "Metadata missing from output XML\n");
        return -1;
    }

    return 0;
}

/* Test vCPU configuration validation */
static int
testVcpuConfiguration(const void *data)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-device-hotplug.xml",
                        abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse device hotplug config\n");
        return -1;
    }

    /* Validate vCPU configuration */
    if (def->maxvcpus != 4) {
        fprintf(stderr, "Expected 4 max vCPUs, got %u\n", def->maxvcpus);
        return -1;
    }

    if (def->vcpus != NULL && def->nvcpus != 4) {
        fprintf(stderr, "Expected 4 vCPU defs, got %zu\n", def->nvcpus);
        return -1;
    }

    return 0;
}

/* Test boot configuration validation */
static int
testBootConfiguration(const void *data)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-boot-lifecycle.xml",
                        abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse boot lifecycle config\n");
        return -1;
    }

    /* Validate boot devices */
    if (def->os.nBootDevs == 0) {
        fprintf(stderr, "Expected boot devices to be defined\n");
        return -1;
    }

    /* Validate boot menu */
    if (!def->os.bootmenu) {
        fprintf(stderr, "Expected boot menu to be defined\n");
        return -1;
    }

    if (def->os.bootmenu->enable != VIR_TRISTATE_BOOL_YES) {
        fprintf(stderr, "Expected boot menu enabled\n");
        return -1;
    }

    if (def->os.bootmenu->timeout != 3000) {
        fprintf(stderr, "Expected boot menu timeout 3000, got %d\n",
                def->os.bootmenu->timeout);
        return -1;
    }

    return 0;
}

/* Test lifecycle configuration */
static int
testLifecycleConfiguration(const void *data)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-boot-lifecycle.xml",
                        abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse lifecycle config\n");
        return -1;
    }

    /* Validate lifecycle actions */
    if (def->onPoweroff != VIR_DOMAIN_LIFECYCLE_DESTROY) {
        fprintf(stderr, "Expected on_poweroff destroy\n");
        return -1;
    }

    if (def->onReboot != VIR_DOMAIN_LIFECYCLE_RESTART) {
        fprintf(stderr, "Expected on_reboot restart\n");
        return -1;
    }

    if (def->onCrash != VIR_DOMAIN_LIFECYCLE_PRESERVE) {
        fprintf(stderr, "Expected on_crash preserve\n");
        return -1;
    }

    return 0;
}

/* Test timer configuration */
static int
testTimerConfiguration(const void *data)
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

    /* Validate timers */
    if (def->clock.ntimers == 0) {
        fprintf(stderr, "Expected timers to be defined\n");
        return -1;
    }

    /* Check for required timers */
    bool has_platform = false;
    bool has_rtc = false;
    bool has_armvtimer = false;

    for (size_t i = 0; i < def->clock.ntimers; i++) {
        virDomainTimerDefPtr timer = &def->clock.timers[i];

        if (timer->name == VIR_DOMAIN_TIMER_NAME_PLATFORM)
            has_platform = true;
        else if (timer->name == VIR_DOMAIN_TIMER_NAME_RTC)
            has_rtc = true;
        else if (timer->name == VIR_DOMAIN_TIMER_NAME_ARMVTIMER)
            has_armvtimer = true;
    }

    if (!has_platform || !has_rtc || !has_armvtimer) {
        fprintf(stderr, "Expected platform, rtc, and armvtimer timers\n");
        return -1;
    }

    return 0;
}

/* Test NUMA configuration validation */
static int
testNUMAConfiguration(const void *data)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-numa-memory.xml",
                        abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse NUMA config\n");
        return -1;
    }

    /* Validate NUMA configuration */
    if (!def->numa) {
        fprintf(stderr, "Expected NUMA configuration\n");
        return -1;
    }

    if (def->numa->nCells != 2) {
        fprintf(stderr, "Expected 2 NUMA cells, got %zu\n",
                def->numa->nCells);
        return -1;
    }

    /* Validate memory backing */
    if (def->mem.nhugepages == 0) {
        fprintf(stderr, "Expected hugepages to be configured\n");
        return -1;
    }

    return 0;
}

/* Test advanced configuration parsing */
static int
testAdvancedConfiguration(const void *data)
{
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-advanced-config.xml",
                        abs_srcdir);

    def = virDomainDefParseFile(xml, driver.xmlopt,
                                NULL, VIR_DOMAIN_DEF_PARSE_INACTIVE);

    if (!def) {
        fprintf(stderr, "Failed to parse advanced config\n");
        return -1;
    }

    /* Validate multiple devices */
    if (def->ndisks < 2) {
        fprintf(stderr, "Expected at least 2 disks, got %zu\n", def->ndisks);
        return -1;
    }

    if (def->nnets < 2) {
        fprintf(stderr, "Expected at least 2 networks, got %zu\n", def->nnets);
        return -1;
    }

    if (def->nserials < 2) {
        fprintf(stderr, "Expected at least 2 serial ports, got %zu\n", def->nserials);
        return -1;
    }

    /* Validate vCPU details */
    if (def->vcpus && def->nvcpus != 4) {
        fprintf(stderr, "Expected 4 vCPU defs, got %zu\n", def->nvcpus);
        return -1;
    }

    /* Validate memory configuration */
    if (def->mem.cur_balloon != 2097152) {
        fprintf(stderr, "Expected current memory 2097152 KiB, got %llu\n",
                def->mem.cur_balloon);
        return -1;
    }

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

    /* Test various domain configurations */
    if (virTestRun("MACOSVF Domain Parse Various Configs",
                   testDomainParseVariousConfigs, NULL) < 0)
        ret = -1;

    /* Test metadata preservation */
    if (virTestRun("MACOSVF Domain Metadata Preservation",
                   testDomainMetadataPreservation, NULL) < 0)
        ret = -1;

    /* Test vCPU configuration */
    if (virTestRun("MACOSVF VCPU Configuration",
                   testVcpuConfiguration, NULL) < 0)
        ret = -1;

    /* Test boot configuration */
    if (virTestRun("MACOSVF Boot Configuration",
                   testBootConfiguration, NULL) < 0)
        ret = -1;

    /* Test lifecycle configuration */
    if (virTestRun("MACOSVF Lifecycle Configuration",
                   testLifecycleConfiguration, NULL) < 0)
        ret = -1;

    /* Test timer configuration */
    if (virTestRun("MACOSVF Timer Configuration",
                   testTimerConfiguration, NULL) < 0)
        ret = -1;

    /* Test NUMA configuration */
    if (virTestRun("MACOSVF NUMA Configuration",
                   testNUMAConfiguration, NULL) < 0)
        ret = -1;

    /* Test advanced configuration */
    if (virTestRun("MACOSVF Advanced Configuration",
                   testAdvancedConfiguration, NULL) < 0)
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
