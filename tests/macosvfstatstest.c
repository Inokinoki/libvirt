/*
 * macosvfstatstest.c: test macOS Virtualization.Framework domain statistics
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

# include "macosvf/macosvf_driver.h"
# include "macosvf/macosvf_conf.h"
# include "macosvf/macosvf_domain.h"

# define VIR_FROM_THIS VIR_FROM_NONE

static macosvfConn driver;

struct testInfo {
    const char *name;
};

/* Test domain control info */
static int
testDomainGetControlInfo(const void *data G_GNUC_UNUSED)
{
    virDomainPtr dom = NULL;
    virDomainControlInfo info;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create a test domain */
    dom = virGetDomain(driver.conn, "test-domain",
                       "12345678-1234-1234-1234-123456789abc");
    if (!dom) {
        fprintf(stderr, "%s: Failed to create domain object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Get control info */
    if (macosvfDomainGetControlInfo(dom, &info, 0) < 0) {
        fprintf(stderr, "%s: Failed to get control info\n", __FUNCTION__);
        goto cleanup;
    }

    /* Validate control info */
    if (info.state != VIR_DOMAIN_CONTROL_OK) {
        fprintf(stderr, "%s: Expected control state OK, got %d\n",
                __FUNCTION__, info.state);
        goto cleanup;
    }

    if (info.details != 0) {
        fprintf(stderr, "%s: Expected control details 0, got %u\n",
                __FUNCTION__, info.details);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (dom)
        virObjectUnref(dom);
    return ret;
}

/* Test domain block stats */
static int
testDomainBlockStats(const void *data G_GNUC_UNUSED)
{
    virDomainObj *vm = NULL;
    virDomainDef *def = NULL;
    virDomainBlockStats stats;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create a test domain with disk */
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

    /* Add a disk */
    if (virDomainDiskDefParseXML("vda", "virtio", NULL, &def->disks, &def->ndisks,
                                   VIR_DOMAIN_DEF_PARSE_OK) < 0) {
        fprintf(stderr, "%s: Failed to create disk definition\n", __FUNCTION__);
        goto cleanup;
    }

    /* Create domain object */
    vm = virDomainObjNew(def);
    if (!vm) {
        fprintf(stderr, "%s: Failed to create domain object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Get block stats */
    if (macosvfDomainBlockStatsFromObj(vm, "vda", &stats, sizeof(stats)) < 0) {
        fprintf(stderr, "%s: Failed to get block stats\n", __FUNCTION__);
        goto cleanup;
    }

    /* Validate stats - should return -1 for unsupported values */
    if (stats.rd_req != -1 || stats.rd_bytes != -1 ||
        stats.wr_req != -1 || stats.wr_bytes != -1) {
        fprintf(stderr, "%s: Expected unsupported stats to be -1\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        virObjectUnref(vm);
    return ret;
}

/* Test domain interface stats */
static int
testDomainInterfaceStats(const void *data G_GNUC_UNUSED)
{
    virDomainObj *vm = NULL;
    virDomainDef *def = NULL;
    virDomainInterfaceStats stats;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create a test domain with network */
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

    /* Add a network interface */
    virDomainNetDef *net = virDomainNetDefNew(VIR_DOMAIN_NET_TYPE_USER);
    if (!net) {
        fprintf(stderr, "%s: Failed to create network definition\n", __FUNCTION__);
        goto cleanup;
    }

    net->model = VIR_DOMAIN_NET_MODEL_VIRTIO;
    if (VIR_ALLOC_N(def->nets, 1) < 0 || (def->nnets = 1, def->nets[0] = net, 0)) {
        virDomainNetDefFree(net);
        goto cleanup;
    }

    /* Create domain object */
    vm = virDomainObjNew(def);
    if (!vm) {
        fprintf(stderr, "%s: Failed to create domain object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Get interface stats */
    if (macosvfDomainInterfaceStatsFromObj(vm, "vnet0", &stats, sizeof(stats)) < 0) {
        fprintf(stderr, "%s: Failed to get interface stats\n", __FUNCTION__);
        goto cleanup;
    }

    /* Validate stats - should return -1 for unsupported values */
    if (stats.rx_bytes != -1 || stats.rx_packets != -1 ||
        stats.tx_bytes != -1 || stats.tx_packets != -1) {
        fprintf(stderr, "%s: Expected unsupported stats to be -1\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        virObjectUnref(vm);
    return ret;
}

/* Test invalid device paths for stats */
static int
testDomainStatsInvalidPath(const void *data G_GNUC_UNUSED)
{
    virDomainObj *vm = NULL;
    virDomainDef *def = NULL;
    virDomainBlockStats stats;
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

    def->vcpus = g_new0(virDomainVcpuDef, 1);
    def->vcpus[0].type = VIR_DOMAIN_VCPU_TYPE-online;
    def->maxvcpus = 1;

    def->mem.cur_balloon = 1024 * 1024;

    vm = virDomainObjNew(def);
    if (!vm) {
        fprintf(stderr, "%s: Failed to create domain object\n", __FUNCTION__);
        goto cleanup;
    }

    /* Test with invalid disk path - should fail */
    if (macosvfDomainBlockStatsFromObj(vm, "invalid-disk", &stats, sizeof(stats)) == 0) {
        fprintf(stderr, "%s: Should fail with invalid disk path\n", __FUNCTION__);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (vm)
        virObjectUnref(vm);
    return ret;
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

    /* Create a connection */
    if ((driver.conn = virConnectNew(&driver.conn, &macosvfConnectDriver)) == NULL) {
        virObjectUnref(driver.caps);
        virObjectUnref(driver.xmlopt);
        return EXIT_FAILURE;
    }

    /* Test domain control info */
    if (virTestRun("MACOSVF Domain Get Control Info",
                   testDomainGetControlInfo, NULL) < 0)
        ret = -1;

    /* Test block stats */
    if (virTestRun("MACOSVF Domain Block Stats",
                   testDomainBlockStats, NULL) < 0)
        ret = -1;

    /* Test interface stats */
    if (virTestRun("MACOSVF Domain Interface Stats",
                   testDomainInterfaceStats, NULL) < 0)
        ret = -1;

    /* Test invalid paths */
    if (virTestRun("MACOSVF Domain Stats Invalid Path",
                   testDomainStatsInvalidPath, NULL) < 0)
        ret = -1;

    virObjectUnref(driver.caps);
    virObjectUnref(driver.xmlopt);
    virObjectUnref(driver.conn);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN_PRELOAD(mymain)
VIR_TEST_MAIN(mymain)

#else

int
main(void)
{
    return EXIT_AM_SKIP;
}

#endif /* WITH_MACOSVF */
