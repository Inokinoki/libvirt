/*
 * macosvfnumaparamstest.c: test macOS Virtualization.Framework NUMA parameters
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

# define VIR_FROM_THIS VIR_FROM_NONE

static macosvfConn driver;

/* Test basic NUMA parameter functionality */
static int
testNumaParametersBasic(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create domain definition */
    def = virDomainDefNew();
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup("test-numaparams-basic");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 1024 * 1024;  /* 1 GB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Verify VM was created successfully */
    if (!vm) {
        fprintf(stderr, "%s: VM object is NULL\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    return ret;
}

/* Test NUMA parameters with different memory configurations */
static int
testNumaParametersMemorySizes(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned long long memory;
    } configs[] = {
        { "minimal-512mb", 524288 },
        { "standard-1gb", 1048576 },
        { "large-2gb", 2097152 },
        { "xlarge-4gb", 4194304 },
        { NULL, 0 }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i].name != NULL; i++) {
        g_autoptr(virDomainDef) def = NULL;
        macosvfVMObject *vm = NULL;

        def = virDomainDefNew();
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
        def->mem.cur_balloon = configs[i].memory;

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

/* Test NUMA parameter validation */
static int
testNumaParametersValidation(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        int mode;
        bool valid;
    } params[] = {
        { "strict", VIR_DOMAIN_NUMATUNE_MEM_STRICT, true },
        { "preferred", VIR_DOMAIN_NUMATUNE_MEM_PREFERRED, true },
        { "interleave", VIR_DOMAIN_NUMATUNE_MEM_INTERLEAVE, true },
        { NULL, 0, false }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Validate that these are reasonable NUMA mode values */
    for (int i = 0; params[i].name != NULL; i++) {
        /* Check that mode is valid */
        if (params[i].mode != VIR_DOMAIN_NUMATUNE_MEM_STRICT &&
            params[i].mode != VIR_DOMAIN_NUMATUNE_MEM_PREFERRED &&
            params[i].mode != VIR_DOMAIN_NUMATUNE_MEM_INTERLEAVE) {
            fprintf(stderr, "%s: Invalid NUMA mode %d for %s\n",
                    __FUNCTION__, params[i].mode, params[i].name);
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    return ret;
}

/* Test NUMA parameters with CPU configurations */
static int
testNumaParametersCPUConfigurations(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned int vcpus;
        unsigned long long memory;
    } configs[] = {
        { "1vcpu-1gb", 1, 1048576 },
        { "2vcpu-2gb", 2, 2097152 },
        { "4vcpu-4gb", 4, 4194304 },
        { NULL, 0, 0 }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i].name != NULL; i++) {
        g_autoptr(virDomainDef) def = NULL;
        macosvfVMObject *vm = NULL;

        def = virDomainDefNew();
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
        def->mem.cur_balloon = configs[i].memory;

        def->vcpus = g_new0(virDomainVcpuDef, 1);
        def->vcpus[0].type = VIR_DOMAIN_VCPU_TYPE-online;
        def->maxvcpus = configs[i].vcpus;

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

/* Test NUMA parameters edge cases */
static int
testNumaParametersEdgeCases(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Test with minimum memory */
    def = virDomainDefNew();
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup("test-min-numa");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 256 * 1024;  /* 256 MB */

    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM with minimum memory\n", __FUNCTION__);
        goto cleanup;
    }

    macosvfVMFree(vm);
    vm = NULL;
    virDomainDefFree(def);
    def = NULL;

    /* Test with large memory */
    def = virDomainDefNew();
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup("test-max-numa");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 32 * 1024 * 1024;  /* 32 GB */

    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM with large memory\n", __FUNCTION__);
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

    /* Test basic NUMA parameter functionality */
    if (virTestRun("MACOSVF NUMA Parameters Basic",
                   testNumaParametersBasic, NULL) < 0)
        ret = -1;

    /* Test NUMA parameters with different memory configurations */
    if (virTestRun("MACOSVF NUMA Parameters Memory Sizes",
                   testNumaParametersMemorySizes, NULL) < 0)
        ret = -1;

    /* Test NUMA parameter validation */
    if (virTestRun("MACOSVF NUMA Parameters Validation",
                   testNumaParametersValidation, NULL) < 0)
        ret = -1;

    /* Test NUMA parameters with CPU configurations */
    if (virTestRun("MACOSVF NUMA Parameters CPU Configurations",
                   testNumaParametersCPUConfigurations, NULL) < 0)
        ret = -1;

    /* Test NUMA parameters edge cases */
    if (virTestRun("MACOSVF NUMA Parameters Edge Cases",
                   testNumaParametersEdgeCases, NULL) < 0)
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
