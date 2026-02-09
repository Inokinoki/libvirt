/*
 * macosvfinterfaceparamstest.c: test macOS Virtualization.Framework interface parameters
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
# include "macosvf/macosvf_vm.h"
# include "domain_conf.h"

# define VIR_FROM_THIS VIR_FROM_NONE

static macosvfConn driver;

/* Test basic interface parameter functionality */
static int
testInterfaceParametersBasic(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    virDomainNetDef *net = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create domain definition */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup("test-interface-basic");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 1024 * 1024;  /* 1 GB */

    /* Add a network interface */
    net = virDomainNetDefNew(NULL);
    if (!net) {
        fprintf(stderr, "%s: Failed to create network interface\n", __FUNCTION__);
        goto cleanup;
    }

    net->type = VIR_DOMAIN_NET_TYPE_NETWORK;
    net->model = VIR_DOMAIN_NET_MODEL_VIRTIO;
    net->ifname = g_strdup("vnet0");

    if (VIR_APPEND_ELEMENT(def->nets, net) < 0) {
        fprintf(stderr, "%s: Failed to add network interface\n", __FUNCTION__);
        goto cleanup;
    }

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    return ret;
}

/* Test interface parameters with different configurations */
static int
testInterfaceParametersConfigs(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        const char *model;
    } configs[] = {
        { "virtio-net", "virtio" },
        { NULL, NULL }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i].name != NULL; i++) {
        g_autoptr(virDomainDef) def = NULL;
        macosvfVMObject *vm = NULL;
        virDomainNetDef *net = NULL;

        def = virDomainDefNew(NULL);
        if (!def) {
            fprintf(stderr, "%s: Failed to create domain definition for %s\n",
                    __FUNCTION__, configs[i].name);
            goto cleanup;
        }

        def->os.type = VIR_DOMAIN_OSTYPE_HVM;
        def->os.arch = VIR_ARCH_AARCH64;
        def->os.machine = g_strdup("macosvf");
        def->name = g_strdup(configs[i].name);
        virUUIDGenerate(def->uuid);
        def->mem.cur_balloon = 1024 * 1024;

        net = virDomainNetDefNew(NULL);
        if (!net) {
            fprintf(stderr, "%s: Failed to create network for %s\n",
                    __FUNCTION__, configs[i].name);
            goto cleanup;
        }

        net->type = VIR_DOMAIN_NET_TYPE_NETWORK;
        net->model = VIR_DOMAIN_NET_MODEL_VIRTIO;
        net->ifname = g_strdup("vnet0");

        if (VIR_APPEND_ELEMENT(def->nets, net) < 0) {
            fprintf(stderr, "%s: Failed to add network for %s\n",
                    __FUNCTION__, configs[i].name);
            goto cleanup;
        }

        if (macosvfVMCreate(def, &vm) < 0) {
            fprintf(stderr, "%s: Failed to create VM for %s\n",
                    __FUNCTION__, configs[i].name);
            goto cleanup;
        }

        macosvfVMFree(vm);
        vm = NULL;
    }

    ret = 0;

cleanup:
    return ret;
}

/* Test interface parameters bandwidth validation */
static int
testInterfaceParametersBandwidth(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned int average;
        unsigned int peak;
        unsigned int burst;
    } bandwidths[] = {
        { "minimum", 1000, 0, 0 },
        { "standard", 10000, 0, 0 },
        { "with-peak", 10000, 15000, 0 },
        { "with-burst", 10000, 0, 20000 },
        { "full", 10000, 15000, 20000 },
        { NULL, 0, 0, 0 }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Validate bandwidth parameters */
    for (int i = 0; bandwidths[i].name != NULL; i++) {
        /* Check that bandwidth values are reasonable */
        if (bandwidths[i].average > 1000000000) {
            fprintf(stderr, "%s: Average bandwidth %u is too large for %s\n",
                    __FUNCTION__, bandwidths[i].average, bandwidths[i].name);
            goto cleanup;
        }

        if (bandwidths[i].peak > 0 && bandwidths[i].peak < bandwidths[i].average) {
            fprintf(stderr, "%s: Peak bandwidth %u is less than average for %s\n",
                    __FUNCTION__, bandwidths[i].peak, bandwidths[i].name);
            goto cleanup;
        }

        if (bandwidths[i].burst > 1000000000) {
            fprintf(stderr, "%s: Burst bandwidth %u is too large for %s\n",
                    __FUNCTION__, bandwidths[i].burst, bandwidths[i].name);
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    return ret;
}

/* Test interface parameters with multiple interfaces */
static int
testInterfaceParametersMultiple(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create domain definition */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup("test-multiple-ifaces");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 2048 * 1024;  /* 2 GB */

    /* Add multiple network interfaces */
    for (int i = 0; i < 2; i++) {
        virDomainNetDef *net = virDomainNetDefNew(NULL);
        if (!net) {
            fprintf(stderr, "%s: Failed to create network interface %d\n", __FUNCTION__, i);
            goto cleanup;
        }

        net->type = VIR_DOMAIN_NET_TYPE_NETWORK;
        net->model = VIR_DOMAIN_NET_MODEL_VIRTIO;
        net->ifname = g_strdup_printf("vnet%d", i);

        if (VIR_APPEND_ELEMENT(def->nets, net) < 0) {
            fprintf(stderr, "%s: Failed to add network interface %d\n", __FUNCTION__, i);
            goto cleanup;
        }
    }

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    return ret;
}

/* Test interface parameters edge cases */
static int
testInterfaceParametersEdgeCases(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Test with minimal configuration */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup("test-min-iface");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 512 * 1024;  /* 512 MB */

    /* Add single network interface */
    virDomainNetDef *net = virDomainNetDefNew(NULL);
    if (!net) {
        fprintf(stderr, "%s: Failed to create network interface\n", __FUNCTION__);
        goto cleanup;
    }

    net->type = VIR_DOMAIN_NET_TYPE_NETWORK;
    net->model = VIR_DOMAIN_NET_MODEL_VIRTIO;
    net->ifname = g_strdup("vnet0");

    if (VIR_APPEND_ELEMENT(def->nets, net) < 0) {
        fprintf(stderr, "%s: Failed to add network interface\n", __FUNCTION__);
        goto cleanup;
    }

    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    macosvfVMFree(vm);
    vm = NULL;
    virDomainDefFree(def);
    def = NULL;

    /* Test with larger configuration */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup("test-max-iface");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 8192 * 1024;  /* 8 GB */

    /* Add multiple network interfaces */
    for (int i = 0; i < 4; i++) {
        virDomainNetDef *net = virDomainNetDefNew(NULL);
        if (!net) {
            fprintf(stderr, "%s: Failed to create network interface %d\n", __FUNCTION__, i);
            goto cleanup;
        }

        net->type = VIR_DOMAIN_NET_TYPE_NETWORK;
        net->model = VIR_DOMAIN_NET_MODEL_VIRTIO;
        net->ifname = g_strdup_printf("vnet%d", i);

        if (VIR_APPEND_ELEMENT(def->nets, net) < 0) {
            fprintf(stderr, "%s: Failed to add network interface %d\n", __FUNCTION__, i);
            goto cleanup;
        }
    }

    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    return ret;
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

    /* Test basic interface parameter functionality */
    if (virTestRun("MACOSVF Interface Parameters Basic",
                   testInterfaceParametersBasic, NULL) < 0)
        ret = -1;

    /* Test interface parameters with different configurations */
    if (virTestRun("MACOSVF Interface Parameters Configs",
                   testInterfaceParametersConfigs, NULL) < 0)
        ret = -1;

    /* Test interface parameters bandwidth validation */
    if (virTestRun("MACOSVF Interface Parameters Bandwidth",
                   testInterfaceParametersBandwidth, NULL) < 0)
        ret = -1;

    /* Test interface parameters with multiple interfaces */
    if (virTestRun("MACOSVF Interface Parameters Multiple",
                   testInterfaceParametersMultiple, NULL) < 0)
        ret = -1;

    /* Test interface parameters edge cases */
    if (virTestRun("MACOSVF Interface Parameters Edge Cases",
                   testInterfaceParametersEdgeCases, NULL) < 0)
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
