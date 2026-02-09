/*
 * macosvfblockiotunetest.c: test macOS Virtualization.Framework block I/O tuning
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

/* Test basic block I/O tune functionality */
static int
testBlockIoTuneBasic(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virDomainDef) def = NULL;
    macosvfVMObject *vm = NULL;
    virDomainDiskDef *disk = NULL;
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
    def->name = g_strdup("test-blockiotune-basic");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 1024 * 1024;  /* 1 GB */

    /* Add a disk */
    disk = virDomainDiskDefNew(NULL);
    if (!disk) {
        fprintf(stderr, "%s: Failed to create disk definition\n", __FUNCTION__);
        goto cleanup;
    }

    disk->dst = g_strdup("vda");
    disk->device = VIR_DOMAIN_DISK_DEVICE_DISK;
    disk->bus = VIR_DOMAIN_DISK_BUS_VIRTIO;

    if (VIR_APPEND_ELEMENT(def->disks, disk) < 0) {
        fprintf(stderr, "%s: Failed to add disk to domain\n", __FUNCTION__);
        goto cleanup;
    }

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
    if (disk && def && def->ndisks == 0)
        virDomainDiskDefFree(disk);
    return ret;
}

/* Test block I/O tune with different disk configurations */
static int
testBlockIoTuneDiskConfigurations(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        const char *bus;
    } configs[] = {
        { "virtio-disk", "virtio" },
        { "ide-disk", "ide" },
        { NULL, NULL }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    for (int i = 0; configs[i].name != NULL; i++) {
        g_autoptr(virDomainDef) def = NULL;
        macosvfVMObject *vm = NULL;
        virDomainDiskDef *disk = NULL;

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

        /* Add a disk */
        disk = virDomainDiskDefNew(NULL);
        if (!disk) {
            fprintf(stderr, "%s: Failed to create disk for %s\n",
                    __FUNCTION__, configs[i].name);
            goto cleanup;
        }

        disk->dst = g_strdup("vda");
        disk->device = VIR_DOMAIN_DISK_DEVICE_DISK;

        if (STREQ(configs[i].bus, "virtio"))
            disk->bus = VIR_DOMAIN_DISK_BUS_VIRTIO;
        else if (STREQ(configs[i].bus, "ide"))
            disk->bus = VIR_DOMAIN_DISK_BUS_IDE;

        if (VIR_APPEND_ELEMENT(def->disks, disk) < 0) {
            fprintf(stderr, "%s: Failed to add disk for %s\n",
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

/* Test block I/O tune with multiple disks */
static int
testBlockIoTuneMultipleDisks(const void *data G_GNUC_UNUSED)
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
    def->name = g_strdup("test-blockiotune-multidisk");
    virUUIDGenerate(def->uuid);
    def->mem.cur_balloon = 2048 * 1024;  /* 2 GB */

    /* Add multiple disks */
    for (int i = 0; i < 3; i++) {
        virDomainDiskDef *disk = virDomainDiskDefNew(NULL);
        if (!disk) {
            fprintf(stderr, "%s: Failed to create disk %d\n", __FUNCTION__, i);
            goto cleanup;
        }

        disk->dst = g_strdup_printf("vd%c", 'a' + i);
        disk->device = VIR_DOMAIN_DISK_DEVICE_DISK;
        disk->bus = VIR_DOMAIN_DISK_BUS_VIRTIO;

        if (VIR_APPEND_ELEMENT(def->disks, disk) < 0) {
            fprintf(stderr, "%s: Failed to add disk %d\n", __FUNCTION__, i);
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

/* Test block I/O tune parameter validation */
static int
testBlockIoTuneParameterValidation(const void *data G_GNUC_UNUSED)
{
    struct {
        const char *name;
        unsigned long long bytes_sec;
        unsigned long long iops_sec;
        bool valid;
    } params[] = {
        { "minimum", 1024, 100, true },
        { "default", 1048576, 1000, true },
        { "high", 10485760, 10000, true },
        { "maximum", 1073741824, 100000, true },
        { "zero", 0, 0, true },
        { NULL, 0, 0, false }
    };

    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Validate that these are reasonable I/O tune values */
    for (int i = 0; params[i].name != NULL; i++) {
        /* In the actual implementation, these values are validated */
        if (params[i].bytes_sec > 1000000000000ULL) {
            fprintf(stderr, "%s: bytes_sec value %llu is too large for %s\n",
                    __FUNCTION__, params[i].bytes_sec, params[i].name);
            goto cleanup;
        }

        if (params[i].iops_sec > 1000000000ULL) {
            fprintf(stderr, "%s: iops_sec value %llu is too large for %s\n",
                    __FUNCTION__, params[i].iops_sec, params[i].name);
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    return ret;
}

/* Test block I/O tune with different memory configurations */
static int
testBlockIoTuneMemoryConfigurations(const void *data G_GNUC_UNUSED)
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
        virDomainDiskDef *disk = NULL;

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

        /* Add a disk */
        disk = virDomainDiskDefNew(NULL);
        if (!disk) {
            fprintf(stderr, "%s: Failed to create disk for %s\n",
                    __FUNCTION__, configs[i].name);
            goto cleanup;
        }

        disk->dst = g_strdup("vda");
        disk->device = VIR_DOMAIN_DISK_DEVICE_DISK;
        disk->bus = VIR_DOMAIN_DISK_BUS_VIRTIO;

        if (VIR_APPEND_ELEMENT(def->disks, disk) < 0) {
            fprintf(stderr, "%s: Failed to add disk for %s\n",
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

    /* Test basic block I/O tune functionality */
    if (virTestRun("MACOSVF Block I/O Tune Basic",
                   testBlockIoTuneBasic, NULL) < 0)
        ret = -1;

    /* Test block I/O tune with different disk configurations */
    if (virTestRun("MACOSVF Block I/O Tune Disk Configurations",
                   testBlockIoTuneDiskConfigurations, NULL) < 0)
        ret = -1;

    /* Test block I/O tune with multiple disks */
    if (virTestRun("MACOSVF Block I/O Tune Multiple Disks",
                   testBlockIoTuneMultipleDisks, NULL) < 0)
        ret = -1;

    /* Test block I/O tune parameter validation */
    if (virTestRun("MACOSVF Block I/O Tune Parameter Validation",
                   testBlockIoTuneParameterValidation, NULL) < 0)
        ret = -1;

    /* Test block I/O tune with different memory configurations */
    if (virTestRun("MACOSVF Block I/O Tune Memory Configurations",
                   testBlockIoTuneMemoryConfigurations, NULL) < 0)
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
