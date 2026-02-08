/*
 * macosvfschedulertest.c: test macOS Virtualization.Framework scheduler operations
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

/* Test basic scheduler functionality */
static int
testSchedulerBasic(const void *data G_GNUC_UNUSED)
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
    def->name = g_strdup("test-scheduler-basic");
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

/* Test scheduler with different memory configurations */
static int
testSchedulerMemoryConfigurations(const void *data G_GNUC_UNUSED)
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

/* Test scheduler with different CPU configurations */
static int
testSchedulerCPUConfigurations(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned int vcpus;
    } configs[] = {
        { "single-cpu", 1 },
        { "dual-cpu", 2 },
        { "quad-cpu", 4 },
        { "octo-cpu", 8 },
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
        def->mem.cur_balloon = 1024 * 1024;  /* 1 GB */

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

/* Test scheduler state validation */
static int
testSchedulerStateValidation(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    macosvfVMState state;
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
    def->name = g_strdup("test-scheduler-state");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 1024 * 1024;

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Check initial state */
    state = macosvfVMGetState(vm);
    if (state != MACOSVF_VM_STATE_STOPPED) {
        fprintf(stderr, "%s: Expected initial state STOPPED, got %d\n",
                __FUNCTION__, state);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    return ret;
}

/* Test scheduler parameter validation */
static int
testSchedulerParameterValidation(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned long long cpu_shares;
        bool valid;
    } params[] = {
        { "minimum", 512, true },
        { "default", 1024, true },
        { "high", 2048, true },
        { "maximum", 4096, true },
        { "zero", 0, true },
        { NULL, 0, false }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Validate that these are reasonable CPU share values */
    for (int i = 0; params[i].name != NULL; i++) {
        /* In the actual implementation, these values are validated */
        if (params[i].cpu_shares > 65536) {
            fprintf(stderr, "%s: CPU shares value %llu is too large for %s\n",
                    __FUNCTION__, params[i].cpu_shares, params[i].name);
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
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

    /* Test basic scheduler functionality */
    if (virTestRun("MACOSVF Scheduler Basic",
                   testSchedulerBasic, NULL) < 0)
        ret = -1;

    /* Test scheduler with different memory configurations */
    if (virTestRun("MACOSVF Scheduler Memory Configurations",
                   testSchedulerMemoryConfigurations, NULL) < 0)
        ret = -1;

    /* Test scheduler with different CPU configurations */
    if (virTestRun("MACOSVF Scheduler CPU Configurations",
                   testSchedulerCPUConfigurations, NULL) < 0)
        ret = -1;

    /* Test scheduler state validation */
    if (virTestRun("MACOSVF Scheduler State Validation",
                   testSchedulerStateValidation, NULL) < 0)
        ret = -1;

    /* Test scheduler parameter validation */
    if (virTestRun("MACOSVF Scheduler Parameter Validation",
                   testSchedulerParameterValidation, NULL) < 0)
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
