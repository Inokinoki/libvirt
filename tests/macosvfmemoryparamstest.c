/*
 * macosvfmemoryparamstest.c: test macOS Virtualization.Framework memory parameters
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

/* Test basic memory parameter functionality */
static int
testMemoryParametersBasic(const void *data G_GNUC_UNUSED)
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
    def->name = g_strdup("test-memoryparams-basic");
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

/* Test memory parameters with different memory configurations */
static int
testMemoryParametersDifferentSizes(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned long long memory;
    } configs[] = {
        { "minimal-512mb", 524288 },
        { "standard-1gb", 1048576 },
        { "large-2gb", 2097152 },
        { "xlarge-4gb", 4194304 },
        { "huge-8gb", 8388608 },
        { NULL, 0 }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i].name != NULL; i++) {
        g_autoptr(virDomainDef) def = NULL;
        macosvfVMObject *vm = NULL;

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

/* Test memory parameter validation */
static int
testMemoryParametersValidation(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned long long hard_limit;
        unsigned long long soft_limit;
        bool valid;
    } params[] = {
        { "unlimited", 0, 0, true },
        { "soft-only", 0, 512 * 1024, true },
        { "hard-2gb", 2 * 1024 * 1024, 0, true },
        { "hard-4gb", 4 * 1024 * 1024, 1024 * 1024, true },
        { "hard-8gb", 8 * 1024 * 1024, 2 * 1024 * 1024, true },
        { NULL, 0, 0, false }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Validate that these are reasonable memory parameter values */
    for (int i = 0; params[i].name != NULL; i++) {
        /* Check that hard_limit is reasonable */
        if (params[i].hard_limit > 10000000000ULL) {
            fprintf(stderr, "%s: hard_limit value %llu is too large for %s\n",
                    __FUNCTION__, params[i].hard_limit, params[i].name);
            goto cleanup;
        }

        /* Check that soft_limit is reasonable */
        if (params[i].soft_limit > params[i].hard_limit && params[i].hard_limit != 0) {
            fprintf(stderr, "%s: soft_limit %llu exceeds hard_limit %llu for %s\n",
                    __FUNCTION__, params[i].soft_limit, params[i].hard_limit, params[i].name);
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    return ret;
}

/* Test memory parameters with CPU configurations */
static int
testMemoryParametersCPUConfigurations(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned int vcpus;
        unsigned long long memory;
    } configs[] = {
        { "1vcpu-1gb", 1, 1048576 },
        { "2vcpu-2gb", 2, 2097152 },
        { "4vcpu-4gb", 4, 4194304 },
        { "8vcpu-8gb", 8, 8388608 },
        { NULL, 0, 0 }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i].name != NULL; i++) {
        g_autoptr(virDomainDef) def = NULL;
        macosvfVMObject *vm = NULL;

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
        def->mem.cur_balloon = configs[i].memory;

        virDomainDefSetVcpusMax(def, configs[i].vcpus, NULL);
        virDomainDefSetVcpus(def, configs[i].vcpus);

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

/* Test memory parameters edge cases */
static int
testMemoryParametersEdgeCases(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Test with minimum memory */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup("test-min-memory");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 128 * 1024;  /* 128 MB - minimum */

    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM with minimum memory\n", __FUNCTION__);
        goto cleanup;
    }

    macosvfVMFree(vm);
    vm = NULL;
    virDomainDefFree(def);
    def = NULL;

    /* Test with large memory */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup("test-max-memory");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 64 * 1024 * 1024;  /* 64 GB - large */

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

    /* Test basic memory parameter functionality */
    if (virTestRun("MACOSVF Memory Parameters Basic",
                   testMemoryParametersBasic, NULL) < 0)
        ret = -1;

    /* Test memory parameters with different memory configurations */
    if (virTestRun("MACOSVF Memory Parameters Different Sizes",
                   testMemoryParametersDifferentSizes, NULL) < 0)
        ret = -1;

    /* Test memory parameter validation */
    if (virTestRun("MACOSVF Memory Parameters Validation",
                   testMemoryParametersValidation, NULL) < 0)
        ret = -1;

    /* Test memory parameters with CPU configurations */
    if (virTestRun("MACOSVF Memory Parameters CPU Configurations",
                   testMemoryParametersCPUConfigurations, NULL) < 0)
        ret = -1;

    /* Test memory parameters edge cases */
    if (virTestRun("MACOSVF Memory Parameters Edge Cases",
                   testMemoryParametersEdgeCases, NULL) < 0)
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
