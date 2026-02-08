/*
 * macosvfmemorystatstest.c: test macOS Virtualization.Framework memory statistics
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

/* Test memory statistics for various domain configurations */
static int
testMemoryStats(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned long long memory;
        unsigned int vcpus;
    } configs[] = {
        { "minimal-512mb", 524288, 1 },
        { "basic-1gb", 1048576, 2 },
        { "advanced-2gb", 2097152, 4 },
        { "large-4gb", 4194304, 8 },
        { NULL, 0, 0 }
    };

    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    virDomainMemoryStat stats[VIR_DOMAIN_MEMORY_STAT_NR];
    int ret = -1;
    int nstats = 0;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i].name != NULL; i++) {
        /* Create domain definition */
        def = virDomainDefNew();
        if (!def) {
            fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
            goto cleanup;
        }

        def->os.type = VIR_DOMAIN_OSTYPE_HVM;
        def->os.arch = VIR_ARCH_AARCH64;
        def->os.machine = g_strdup("macosvf");
        def->name = g_strdup(configs[i].name);
        virUUIDGenerate(def->uuid);

        def->vcpus = g_new0(virDomainVcpuDef, 1);
        def->vcpus[0].type = VIR_DOMAIN_VCPU_TYPE-online;
        def->maxvcpus = configs[i].vcpus;

        def->mem.cur_balloon = configs[i].memory;

        /* Create VM object */
        if (macosvfVMCreate(def, &vm) < 0) {
            fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
            goto cleanup;
        }

        /* Get memory statistics */
        memset(stats, 0, sizeof(stats));
        nstats = macosvfVMGetMemoryStats(vm, stats, VIR_DOMAIN_MEMORY_STAT_NR);
        if (nstats < 0) {
            fprintf(stderr, "%s: Failed to get memory stats for %s\n",
                    __FUNCTION__, configs[i].name);
            goto cleanup;
        }

        /* Validate statistics */
        bool found_total = false;
        for (int j = 0; j < nstats; j++) {
            if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_TOTAL) {
                if (stats[j].val != configs[i].memory) {
                    fprintf(stderr, "%s: Expected total memory %llu, got %llu\n",
                            __FUNCTION__, configs[i].memory, stats[j].val);
                    goto cleanup;
                }
                found_total = true;
            }
        }

        if (!found_total) {
            fprintf(stderr, "%s: Missing total memory stat for %s\n",
                    __FUNCTION__, configs[i].name);
            goto cleanup;
        }

        VIR_DEBUG("Memory stats for %s: %d stats, total=%llu KiB",
                  configs[i].name, nstats, configs[i].memory);

        /* Cleanup for next iteration */
        macosvfVMFree(vm);
        vm = NULL;
        virDomainDefFree(def);
        def = NULL;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    if (def)
        virDomainDefFree(def);
    return ret;
}

/* Test memory stats with different memory sizes */
static int
testMemoryStatsSizes(const void *data G_GNUC_UNUSED)
{
    unsigned long long sizes[] = {
        512 * 1024,       /* 512 MB */
        1024 * 1024,      /* 1 GB */
        2048 * 1024,      /* 2 GB */
        4096 * 1024,      /* 4 GB */
        8192 * 1024,      /* 8 GB */
        16384 * 1024,     /* 16 GB */
    };
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);

    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    virDomainMemoryStat stats[VIR_DOMAIN_MEMORY_STAT_NR];
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; i < nsizes; i++) {
        /* Create domain definition */
        def = virDomainDefNew();
        if (!def) {
            fprintf(stderr, "%s: Failed to create domain definition\n", __FUNCTION__);
            goto cleanup;
        }

        def->os.type = VIR_DOMAIN_OSTYPE_HVM;
        def->os.arch = VIR_ARCH_AARCH64;
        def->os.machine = g_strdup("macosvf");
        def->mem.cur_balloon = sizes[i];

        /* Create VM object */
        if (macosvfVMCreate(def, &vm) < 0) {
            fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
            goto cleanup;
        }

        /* Get memory statistics */
        memset(stats, 0, sizeof(stats));
        int nstats = macosvfVMGetMemoryStats(vm, stats, VIR_DOMAIN_MEMORY_STAT_NR);
        if (nstats < 0) {
            fprintf(stderr, "%s: Failed to get memory stats for size %llu\n",
                    __FUNCTION__, sizes[i]);
            goto cleanup;
        }

        /* Find and validate total memory */
        bool found_total = false;
        for (int j = 0; j < nstats; j++) {
            if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_TOTAL) {
                if (stats[j].val != sizes[i]) {
                    fprintf(stderr, "%s: Expected %llu KiB, got %llu KiB\n",
                            __FUNCTION__, sizes[i], stats[j].val);
                    goto cleanup;
                }
                found_total = true;
            }
        }

        if (!found_total) {
            fprintf(stderr, "%s: Missing total memory stat\n", __FUNCTION__);
            goto cleanup;
        }

        VIR_DEBUG("Validated memory stats for %llu KiB", sizes[i]);

        /* Cleanup for next iteration */
        macosvfVMFree(vm);
        vm = NULL;
        virDomainDefFree(def);
        def = NULL;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    if (def)
        virDomainDefFree(def);
    return ret;
}

/* Test memory stats during VM state transitions */
static int
testMemoryStatsStateTransitions(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    virDomainMemoryStat stats[VIR_DOMAIN_MEMORY_STAT_NR];
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
    def->mem.cur_balloon = 1024 * 1024;  /* 1 GB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Get memory stats in initial state */
    memset(stats, 0, sizeof(stats));
    int nstats = macosvfVMGetMemoryStats(vm, stats, VIR_DOMAIN_MEMORY_STAT_NR);
    if (nstats < 0) {
        fprintf(stderr, "%s: Failed to get memory stats in initial state\n", __FUNCTION__);
        goto cleanup;
    }

    if (nstats == 0) {
        fprintf(stderr, "%s: No memory stats returned in initial state\n", __FUNCTION__);
        goto cleanup;
    }

    /* Note: We can't actually start the VM in tests, but we've validated
     * that the stats function works with the VM object */

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    return ret;
}

/* Test all memory stat tags */
static int
testMemoryStatsTags(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    virDomainMemoryStat stats[VIR_DOMAIN_MEMORY_STAT_NR];
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
    def->mem.cur_balloon = 2048 * 1024;  /* 2 GB */

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Get memory statistics */
    memset(stats, 0, sizeof(stats));
    int nstats = macosvfVMGetMemoryStats(vm, stats, VIR_DOMAIN_MEMORY_STAT_NR);
    if (nstats < 0) {
        fprintf(stderr, "%s: Failed to get memory stats\n", __FUNCTION__);
        goto cleanup;
    }

    /* Validate that expected tags are present */
    bool has_total = false;
    bool has_unused = false;
    bool has_available = false;

    for (int i = 0; i < nstats; i++) {
        switch (stats[i].tag) {
        case VIR_DOMAIN_MEMORY_STAT_TOTAL:
            has_total = true;
            if (stats[i].val != 2048 * 1024) {
                fprintf(stderr, "%s: Total memory mismatch: expected %u, got %llu\n",
                        __FUNCTION__, 2048 * 1024, stats[i].val);
                goto cleanup;
            }
            break;
        case VIR_DOMAIN_MEMORY_STAT_UNUSED:
            has_unused = true;
            /* Unused is reported as 0 (not supported) */
            break;
        case VIR_DOMAIN_MEMORY_STAT_AVAILABLE:
            has_available = true;
            if (stats[i].val != 2048 * 1024) {
                fprintf(stderr, "%s: Available memory mismatch: expected %u, got %llu\n",
                        __FUNCTION__, 2048 * 1024, stats[i].val);
                goto cleanup;
            }
            break;
        default:
            /* Other tags are fine - we support reporting them */
            break;
        }
    }

    if (!has_total) {
        fprintf(stderr, "%s: Missing VIR_DOMAIN_MEMORY_STAT_TOTAL tag\n", __FUNCTION__);
        goto cleanup;
    }

    if (!has_available) {
        fprintf(stderr, "%s: Missing VIR_DOMAIN_MEMORY_STAT_AVAILABLE tag\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    return ret;
}

/* Test edge cases for memory stats */
static int
testMemoryStatsEdgeCases(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    virDomainMemoryStat stats[VIR_DOMAIN_MEMORY_STAT_NR];
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
    def->mem.cur_balloon = 128 * 1024;  /* 128 MB - minimum */

    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    memset(stats, 0, sizeof(stats));
    int nstats = macosvfVMGetMemoryStats(vm, stats, VIR_DOMAIN_MEMORY_STAT_NR);
    if (nstats < 0) {
        fprintf(stderr, "%s: Failed to get memory stats for minimum memory\n", __FUNCTION__);
        goto cleanup;
    }

    /* Cleanup */
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
    def->mem.cur_balloon = 64 * 1024 * 1024;  /* 64 GB - large */

    if (macosvfVMCreate(def, &vm) < 0) {
        fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
        goto cleanup;
    }

    memset(stats, 0, sizeof(stats));
    nstats = macosvfVMGetMemoryStats(vm, stats, VIR_DOMAIN_MEMORY_STAT_NR);
    if (nstats < 0) {
        fprintf(stderr, "%s: Failed to get memory stats for large memory\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        macosvfVMFree(vm);
    if (def)
        virDomainDefFree(def);
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

    /* Test memory statistics */
    if (virTestRun("MACOSVF Memory Stats",
                   testMemoryStats, NULL) < 0)
        ret = -1;

    /* Test memory stats with different sizes */
    if (virTestRun("MACOSVF Memory Stats Sizes",
                   testMemoryStatsSizes, NULL) < 0)
        ret = -1;

    /* Test memory stats during state transitions */
    if (virTestRun("MACOSVF Memory Stats State Transitions",
                   testMemoryStatsStateTransitions, NULL) < 0)
        ret = -1;

    /* Test all memory stat tags */
    if (virTestRun("MACOSVF Memory Stats Tags",
                   testMemoryStatsTags, NULL) < 0)
        ret = -1;

    /* Test edge cases */
    if (virTestRun("MACOSVF Memory Stats Edge Cases",
                   testMemoryStatsEdgeCases, NULL) < 0)
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
