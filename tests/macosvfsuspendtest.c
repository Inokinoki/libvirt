/*
 * macosvfsuspendtest.c: test macOS Virtualization.Framework suspend/resume and statistics
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

/* Test CPU statistics tracking */
static int
testCPUStats(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    unsigned long long cpuTime = 0;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create a minimal domain definition */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    /* Set basic CPU and memory */
    virDomainDefSetVcpusMax(def, 2, NULL);
    virDomainDefSetVcpus(def, 2);

    def->mem.cur_balloon = 1024 * 1024; /* 1GB in KiB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test initial CPU time (should be 0) */
    if (macosvfVMGetCPUStats(vm, &cpuTime) < 0) {
        fprintf(stderr, "%s: Failed to get CPU stats\n", __FUNCTION__);
        goto cleanup;
    }

    if (cpuTime != 0) {
        fprintf(stderr, "%s: Expected CPU time 0, got %llu\n", __FUNCTION__, cpuTime);
        goto cleanup;
    }

    /* Note: We can't test actual VM operations without the framework,
     * but we can verify the structure is properly initialized */
    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    virDomainDefFree(def);
    return ret;
}

/* Test memory statistics */
static int
testMemoryStats(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    unsigned long long memoryUsed = 0;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create a minimal domain definition */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    /* Set basic CPU and memory */
    virDomainDefSetVcpusMax(def, 2, NULL);
    virDomainDefSetVcpus(def, 2);

    def->mem.cur_balloon = 1024 * 1024; /* 1GB in KiB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test memory stats (should return configured memory when not running) */
    if (macosvfVMGetMemoryStats(vm, &memoryUsed) < 0) {
        fprintf(stderr, "%s: Failed to get memory stats\n", __FUNCTION__);
        goto cleanup;
    }

    /* When VM is not running, memory should be 0 */
    if (memoryUsed != 0) {
        fprintf(stderr, "%s: Expected memory 0 for stopped VM, got %llu\n",
                __FUNCTION__, memoryUsed);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    virDomainDefFree(def);
    return ret;
}

/* Test VM state transitions */
static int
testVMStateTransitions(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    macosvfVMState state;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create a minimal domain definition */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    /* Set basic CPU and memory */
    virDomainDefSetVcpusMax(def, 2, NULL);
    virDomainDefSetVcpus(def, 2);

    def->mem.cur_balloon = 1024 * 1024; /* 1GB in KiB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test initial state (should be STOPPED) */
    state = macosvfVMGetState(vm);
    if (state != MACOSVF_VM_STATE_STOPPED) {
        fprintf(stderr, "%s: Expected initial state STOPPED, got %d\n",
                __FUNCTION__, state);
        goto cleanup;
    }

    /* Note: We can't test actual state transitions without the framework,
     * but we can verify the initial state is correct */

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    virDomainDefFree(def);
    return ret;
}

/* Test pause/resume error handling */
static int
testPauseResumeErrors(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create a minimal domain definition */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    /* Set basic CPU and memory */
    virDomainDefSetVcpusMax(def, 2, NULL);
    virDomainDefSetVcpus(def, 2);

    def->mem.cur_balloon = 1024 * 1024; /* 1GB in KiB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test pause on stopped VM (should fail) */
    if (macosvfVMPause(vm) == 0) {
        fprintf(stderr, "%s: Pause should fail on stopped VM\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test resume on stopped VM (should fail) */
    if (macosvfVMResume(vm) == 0) {
        fprintf(stderr, "%s: Resume should fail on stopped VM\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test pause with NULL VM (should fail) */
    if (macosvfVMPause(NULL) == 0) {
        fprintf(stderr, "%s: Pause should fail with NULL VM\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test resume with NULL VM (should fail) */
    if (macosvfVMResume(NULL) == 0) {
        fprintf(stderr, "%s: Resume should fail with NULL VM\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    virDomainDefFree(def);
    return ret;
}

/* Test statistics with different CPU configurations */
static int
testStatsMultiVCPU(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    unsigned long long cpuTime = 0;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create a domain definition with multiple vCPUs */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    /* Set up 4 vCPUs */
    virDomainDefSetVcpusMax(def, 4, NULL);
    virDomainDefSetVcpus(def, 4);

    def->mem.cur_balloon = 2048 * 1024; /* 2GB in KiB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test CPU time (should be 0 initially) */
    if (macosvfVMGetCPUStats(vm, &cpuTime) < 0) {
        fprintf(stderr, "%s: Failed to get CPU stats\n", __FUNCTION__);
        goto cleanup;
    }

    if (cpuTime != 0) {
        fprintf(stderr, "%s: Expected CPU time 0 for multi-vCPU VM, got %llu\n",
                __FUNCTION__, cpuTime);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    virDomainDefFree(def);
    return ret;
}

/* Test VM lifecycle state tracking */
static int
testVMLifecycleStates(const void *data G_GNUC_UNUSED)
{
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    macosvfVMState state;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create a minimal domain definition */
    def = virDomainDefNew(NULL);
    if (!def) {
        fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
        goto cleanup;
    }

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");

    /* Set basic CPU and memory */
    virDomainDefSetVcpusMax(def, 1, NULL);
    virDomainDefSetVcpus(def, 1);

    def->mem.cur_balloon = 512 * 1024; /* 512MB in KiB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Verify initial state is STOPPED */
    state = macosvfVMGetState(vm);
    if (state != MACOSVF_VM_STATE_STOPPED) {
        fprintf(stderr, "%s: Expected STOPPED state, got %d\n",
                __FUNCTION__, state);
        goto cleanup;
    }

    /* Verify state transitions can't happen without actual VM ops */
    /* (these would fail as expected without the framework) */

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    virDomainDefFree(def);
    return ret;
}

static int
mymain(void)
{
    int ret = 0;
    struct testInfo info;

    /* macOSVF only supports ARM64/Apple Silicon */
    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Test CPU statistics */
    if (virTestRun("MACOSVF CPU Stats",
                   testCPUStats, &info) < 0)
        ret = -1;

    /* Test memory statistics */
    if (virTestRun("MACOSVF Memory Stats",
                   testMemoryStats, &info) < 0)
        ret = -1;

    /* Test VM state transitions */
    if (virTestRun("MACOSVF VM State Transitions",
                   testVMStateTransitions, &info) < 0)
        ret = -1;

    /* Test pause/resume error handling */
    if (virTestRun("MACOSVF Pause/Resume Errors",
                   testPauseResumeErrors, &info) < 0)
        ret = -1;

    /* Test statistics with multi-vCPU */
    if (virTestRun("MACOSVF Stats Multi-VCPU",
                   testStatsMultiVCPU, &info) < 0)
        ret = -1;

    /* Test VM lifecycle states */
    if (virTestRun("MACOSVF VM Lifecycle States",
                   testVMLifecycleStates, &info) < 0)
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
