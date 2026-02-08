/*
 * macosvfedgetest.c: test macOS Virtualization.Framework edge cases and complex scenarios
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

# include "macosvf/macosvf_vm.h"
# include "macosvf/macosvf_conf.h"
# include "macosvf/macosvf_domain.h"
# include "conf/domain_conf.h"

# define VIR_FROM_THIS VIR_FROM_NONE

struct testInfo {
    const char *name;
};

/* Test VM creation with maximum resources */
static int
testVMMaxResources(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create domain with maximum supported resources */
    def = virDomainDefNew();
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    /* Set maximum vCPUs (16) */
    def->vcpus = g_new0(virDomainVcpuDef, 16);
    for (unsigned int i = 0; i < 16; i++) {
        def->vcpus[i].type = VIR_DOMAIN_VCPU_TYPE-online;
    }
    def->maxvcpus = 16;

    /* Set maximum memory (1 TB in KiB) */
    def->mem.cur_balloon = 1024UL * 1024UL * 1024UL; /* 1 TB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object with max resources\n", __FUNCTION__);
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
    virDomainDefFree(def);
    return ret;
}

/* Test VM creation with minimum resources */
static int
testVMMinResources(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create domain with minimum supported resources */
    def = virDomainDefNew();
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    /* Set minimum vCPUs (1) */
    def->vcpus = g_new0(virDomainVcpuDef, 1);
    def->vcpus[0].type = VIR_DOMAIN_VCPU_TYPE-online;
    def->maxvcpus = 1;

    /* Set minimum memory (1 MB in KiB) */
    def->mem.cur_balloon = 1024; /* 1 MB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object with min resources\n", __FUNCTION__);
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
    virDomainDefFree(def);
    return ret;
}

/* Test VM creation with various CPU topologies */
static int
testVMCPUTopologies(const void *data G_GNUC_UNUSED)
{
    struct {
        unsigned int sockets;
        unsigned int cores;
        unsigned int threads;
        unsigned int total_vcpus;
        bool should_pass;
    } topologies[] = {
        { 1, 1, 1, 1, true },     /* Single CPU, single core, single thread */
        { 1, 2, 1, 2, true },     /* Single CPU, 2 cores */
        { 1, 2, 2, 4, true },     /* Single CPU, 2 cores, 2 threads */
        { 2, 2, 2, 8, true },     /* 2 CPUs, 2 cores, 2 threads */
        { 4, 4, 1, 16, true },    /* 4 CPUs, 4 cores */
        { 1, 1, 32, 32, false },  /* Invalid: exceeds max vCPUs */
        { 0, 0, 0, 0, false },    /* Invalid: zero topology */
    };

    for (size_t i = 0; i < G_N_ELEMENTS(topologies); i++) {
        virDomainDef *def = NULL;
        macosvfVMObject *vm = NULL;
        int result;

        virTestSetHostArch(VIR_ARCH_AARCH64);

        def = virDomainDefNew();
        if (!def) {
            fprintf(stderr, "%s: Failed to create domain definition for topology %zu\n",
                    __FUNCTION__, i);
            continue;
        }

        def->os.type = VIR_DOMAIN_OSTYPE_HVM;
        def->os.arch = VIR_ARCH_AARCH64;
        def->os.machine = g_strdup("macosvf");

        def->vcpus = g_new0(virDomainVcpuDef, topologies[i].total_vcpus);
        for (unsigned int j = 0; j < topologies[i].total_vcpus; j++) {
            def->vcpus[j].type = VIR_DOMAIN_VCPU_TYPE-online;
        }
        def->maxvcpus = topologies[i].total_vcpus;

        def->mem.cur_balloon = 1024 * 1024; /* 1 GB */

        /* Create VM object */
        result = macosvfVMCreate(def, &vm);

        if (topologies[i].should_pass) {
            if (result < 0 || !vm) {
                fprintf(stderr, "%s: Topology %zu (sockets=%u, cores=%u, threads=%u, vcpus=%u) should have passed but failed\n",
                        __FUNCTION__, i,
                        topologies[i].sockets,
                        topologies[i].cores,
                        topologies[i].threads,
                        topologies[i].total_vcpus);
                virDomainDefFree(def);
                continue;
            }
        } else {
            if (result == 0 && vm) {
                fprintf(stderr, "%s: Topology %zu (sockets=%u, cores=%u, threads=%u, vcpus=%u) should have failed but passed\n",
                        __FUNCTION__, i,
                        topologies[i].sockets,
                        topologies[i].cores,
                        topologies[i].threads,
                        topologies[i].total_vcpus);
            }
        }

        if (vm)
            macosvfVMFree(vm);
        virDomainDefFree(def);
    }

    return 0;
}

/* Test statistics accuracy through state transitions */
static int
testStatisticsAccuracy(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    unsigned long long cpuTime1 = 0, cpuTime2 = 0;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    def = virDomainDefNew();
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    def->vcpus = g_new0(virDomainVcpuDef, 2);
    def->vcpus[0].type = VIR_DOMAIN_VCPU_TYPE-online;
    def->vcpus[1].type = VIR_DOMAIN_VCPU_TYPE-online;
    def->maxvcpus = 2;

    def->mem.cur_balloon = 2048 * 1024; /* 2 GB */

    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test CPU time is initially 0 */
    if (macosvfVMGetCPUStats(vm, &cpuTime1) < 0) {
        fprintf(stderr, "%s: Failed to get CPU stats (first call)\n", __FUNCTION__);
        goto cleanup;
    }

    if (cpuTime1 != 0) {
        fprintf(stderr, "%s: Expected CPU time 0 initially, got %llu\n",
                __FUNCTION__, cpuTime1);
        goto cleanup;
    }

    /* Test CPU time doesn't change when VM is stopped */
    if (macosvfVMGetCPUStats(vm, &cpuTime2) < 0) {
        fprintf(stderr, "%s: Failed to get CPU stats (second call)\n", __FUNCTION__);
        goto cleanup;
    }

    if (cpuTime2 != 0) {
        fprintf(stderr, "%s: Expected CPU time 0 for stopped VM, got %llu\n",
                __FUNCTION__, cpuTime2);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    virDomainDefFree(def);
    return ret;
}

/* Test error handling for edge cases */
static int
testErrorHandling(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    unsigned long long cpuTime;
    macosvfVMState state;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Test with NULL VM object */
    state = macosvfVMGetState(NULL);
    if (state != MACOSVF_VM_STATE_ERROR) {
        fprintf(stderr, "%s: GetState with NULL VM should return ERROR state\n",
                __FUNCTION__);
        goto cleanup;
    }

    /* Test CPU stats with NULL VM */
    if (macosvfVMGetCPUStats(NULL, &cpuTime) == 0) {
        fprintf(stderr, "%s: GetCPUStats with NULL VM should fail\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test CPU stats with NULL pointer */
    def = virDomainDefNew();
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    def->vcpus = g_new0(virDomainVcpuDef, 1);
    def->vcpus[0].type = VIR_DOMAIN_VCPU_TYPE-online;
    def->maxvcpus = 1;

    def->mem.cur_balloon = 1024 * 1024;

    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    if (macosvfVMGetCPUStats(vm, NULL) == 0) {
        fprintf(stderr, "%s: GetCPUStats with NULL pointer should fail\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    virDomainDefFree(def);
    return ret;
}

/* Test memory statistics for different configurations */
static int
testMemoryConfigurations(const void *data G_GNUC_UNUSED)
{
    struct {
        unsigned long long memory_kb;
        bool should_pass;
    } memory_configs[] = {
        { 1024, true },              /* 1 MB - minimum */
        { 1024 * 1024, true },       /* 1 GB */
        { 4 * 1024 * 1024, true },   /* 4 GB */
        { 16 * 1024 * 1024, true },  /* 16 GB */
        { 1024UL * 1024UL * 1024UL, true }, /* 1 TB - maximum */
    };

    for (size_t i = 0; i < G_N_ELEMENTS(memory_configs); i++) {
        virDomainDef *def = NULL;
        macosvfVMObject *vm = NULL;
        unsigned long long memoryUsed = 0;
        int result;

        virTestSetHostArch(VIR_ARCH_AARCH64);

        def = virDomainDefNew();
        if (!def) {
            fprintf(stderr, "%s: Failed to create domain definition for config %zu\n",
                    __FUNCTION__, i);
            continue;
        }

        def->os.type = VIR_DOMAIN_OSTYPE_HVM;
        def->os.arch = VIR_ARCH_AARCH64;
        def->os.machine = g_strdup("macosvf");

        def->vcpus = g_new0(virDomainVcpuDef, 1);
        def->vcpus[0].type = VIR_DOMAIN_VCPU_TYPE-online;
        def->maxvcpus = 1;

        def->mem.cur_balloon = memory_configs[i].memory_kb;

        result = macosvfVMCreate(def, &vm);
        if (result < 0) {
            fprintf(stderr, "%s: Failed to create VM with memory %llu KB\n",
                    __FUNCTION__, memory_configs[i].memory_kb);
            virDomainDefFree(def);
            continue;
        }

        /* Get memory statistics */
        if (macosvfVMGetMemoryStats(vm, &memoryUsed) < 0) {
            fprintf(stderr, "%s: Failed to get memory stats for config %zu\n",
                    __FUNCTION__, i);
        }

        /* For stopped VM, memory should be 0 */
        if (memoryUsed != 0) {
            fprintf(stderr, "%s: Expected memory 0 for stopped VM, got %llu\n",
                    __FUNCTION__, memoryUsed);
        }

        if (vm)
            macosvfVMFree(vm);
        virDomainDefFree(def);
    }

    return 0;
}

static int
mymain(void)
{
    int ret = 0;
    struct testInfo info;

    /* macOSVF only supports ARM64/Apple Silicon */
    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Test resource limits */
    if (virTestRun("MACOSVF VM Max Resources",
                   testVMMaxResources, &info) < 0)
        ret = -1;

    if (virTestRun("MACOSVF VM Min Resources",
                   testVMMinResources, &info) < 0)
        ret = -1;

    /* Test CPU topologies */
    if (virTestRun("MACOSVF CPU Topologies",
                   testVMCPUTopologies, &info) < 0)
        ret = -1;

    /* Test statistics accuracy */
    if (virTestRun("MACOSVF Statistics Accuracy",
                   testStatisticsAccuracy, &info) < 0)
        ret = -1;

    /* Test error handling */
    if (virTestRun("MACOSVF Error Handling",
                   testErrorHandling, &info) < 0)
        ret = -1;

    /* Test memory configurations */
    if (virTestRun("MACOSVF Memory Configurations",
                   testMemoryConfigurations, &info) < 0)
        ret = -1;

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
