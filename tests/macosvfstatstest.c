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

#define LIBVIRT_VIRDOMAINOBJ_PRIVATE_H
#include "datatypes.h"
#include "macosvf/macosvf_conf.h"
#include "macosvf/macosvf_domain.h"
#include "macosvf/macosvf_driver.h"
#include "virlog.h"
#include <libvirt/libvirt-domain.h>

#define VIR_FROM_THIS VIR_FROM_NONE

static struct _macosvfConn driver;

struct testInfo {
  const char *name;
};

/* Test domain control info */
static int testDomainGetControlInfo(const void *data G_GNUC_UNUSED) {
  virDomainObj *vm = NULL;
  virDomainDef *def = NULL;
  virDomainControlInfo info;
  int ret = -1;

  virTestSetHostArch(VIR_ARCH_AARCH64);

  /* Create a test domain definition */
  def = virDomainDefNew(driver.xmlopt);
  if (!def)
    goto cleanup;

  def->os.type = VIR_DOMAIN_OSTYPE_HVM;
  def->os.arch = VIR_ARCH_AARCH64;
  def->os.machine = g_strdup("macosvf");
  virDomainDefSetVcpusMax(def, 1, driver.xmlopt);
  virDomainDefSetVcpus(def, 1);

  /* Create domain object */
  vm = virDomainObjNew(driver.xmlopt);
  if (!vm)
    goto cleanup;
  vm->def = def;
  def = NULL;

  /* Get control info */
  if (macosvfDomainGetControlInfoFromObj(vm, &info) < 0) {
    fprintf(stderr, "%s: Failed to get control info\n", __FUNCTION__);
    goto cleanup;
  }

  /* Validate control info */
  if (info.state != VIR_DOMAIN_CONTROL_OK) {
    fprintf(stderr, "%s: Expected control state OK, got %d\n", __FUNCTION__,
            info.state);
    goto cleanup;
  }

  ret = 0;

cleanup:
  if (vm)
    virObjectUnref(vm);
  virDomainDefFree(def);
  return ret;
}

/* Test domain block stats */
static int testDomainBlockStats(const void *data G_GNUC_UNUSED) {
  virDomainObj *vm = NULL;
  virDomainDef *def = NULL;
  virDomainBlockStatsStruct stats;
  virDomainDiskDef *disk = NULL;
  int ret = -1;

  virTestSetHostArch(VIR_ARCH_AARCH64);

  /* Create a test domain definition */
  def = virDomainDefNew(driver.xmlopt);
  if (!def)
    goto cleanup;

  def->os.type = VIR_DOMAIN_OSTYPE_HVM;
  def->os.arch = VIR_ARCH_AARCH64;
  def->os.machine = g_strdup("macosvf");

  /* Add a disk */
  disk = virDomainDiskDefNew(driver.xmlopt);
  if (!disk)
    goto cleanup;
  disk->dst = g_strdup("vda");
  disk->bus = VIR_DOMAIN_DISK_BUS_VIRTIO;
  VIR_APPEND_ELEMENT(def->disks, def->ndisks, disk);

  /* Create domain object */
  vm = virDomainObjNew(driver.xmlopt);
  if (!vm)
    goto cleanup;
  vm->def = def;
  def = NULL;

  /* Get block stats */
  if (macosvfDomainBlockStatsFromObj(vm, "vda", &stats) < 0) {
    fprintf(stderr, "%s: Failed to get block stats\n", __FUNCTION__);
    goto cleanup;
  }

  /* Validate stats - should return -1 for unsupported values */
  if (stats.rd_req != -1 || stats.rd_bytes != -1 || stats.wr_req != -1 ||
      stats.wr_bytes != -1) {
    fprintf(stderr, "%s: Expected unsupported stats to be -1\n", __FUNCTION__);
    goto cleanup;
  }

  ret = 0;

cleanup:
  if (vm)
    virObjectUnref(vm);
  virDomainDefFree(def);
  return ret;
}

/* Test domain interface stats */
static int testDomainInterfaceStats(const void *data G_GNUC_UNUSED) {
  virDomainObj *vm = NULL;
  virDomainDef *def = NULL;
  virDomainInterfaceStatsStruct stats;
  virDomainNetDef *net = NULL;
  int ret = -1;

  virTestSetHostArch(VIR_ARCH_AARCH64);

  /* Create a test domain definition */
  def = virDomainDefNew(driver.xmlopt);
  if (!def)
    goto cleanup;

  def->os.type = VIR_DOMAIN_OSTYPE_HVM;
  def->os.arch = VIR_ARCH_AARCH64;
  def->os.machine = g_strdup("macosvf");

  /* Add a network interface */
  net = virDomainNetDefNew(driver.xmlopt);
  if (!net)
    goto cleanup;
  net->ifname = g_strdup("vnet0");
  VIR_APPEND_ELEMENT(def->nets, def->nnets, net);

  /* Create domain object */
  vm = virDomainObjNew(driver.xmlopt);
  if (!vm)
    goto cleanup;
  vm->def = def;
  def = NULL;

  /* Get interface stats */
  if (macosvfDomainInterfaceStatsFromObj(vm, "vnet0", &stats) < 0) {
    fprintf(stderr, "%s: Failed to get interface stats\n", __FUNCTION__);
    goto cleanup;
  }

  /* Validate stats - should return -1 for unsupported values */
  if (stats.rx_bytes != -1 || stats.tx_bytes != -1) {
    fprintf(stderr, "%s: Expected unsupported stats to be -1\n", __FUNCTION__);
    goto cleanup;
  }

  ret = 0;

cleanup:
  if (vm)
    virObjectUnref(vm);
  virDomainDefFree(def);
  return ret;
}

/* Test invalid device paths for stats */
static int testDomainStatsInvalidPath(const void *data G_GNUC_UNUSED) {
  virDomainObj *vm = NULL;
  virDomainDef *def = NULL;
  virDomainBlockStatsStruct bstats;
  virDomainInterfaceStatsStruct istats;
  int ret = -1;

  virTestSetHostArch(VIR_ARCH_AARCH64);

  def = virDomainDefNew(driver.xmlopt);
  if (!def)
    goto cleanup;

  def->os.type = VIR_DOMAIN_OSTYPE_HVM;
  def->os.arch = VIR_ARCH_AARCH64;
  def->os.machine = g_strdup("macosvf");

  vm = virDomainObjNew(driver.xmlopt);
  if (!vm)
    goto cleanup;
  vm->def = def;
  def = NULL;

  /* Test with invalid disk path - should fail */
  if (macosvfDomainBlockStatsFromObj(vm, "invalid-disk", &bstats) == 0) {
    fprintf(stderr, "%s: Should fail with invalid disk path\n", __FUNCTION__);
    goto cleanup;
  }

  /* Test with invalid interface path - should fail */
  if (macosvfDomainInterfaceStatsFromObj(vm, "invalid-net", &istats) == 0) {
    fprintf(stderr, "%s: Should fail with invalid net path\n", __FUNCTION__);
    goto cleanup;
  }

  ret = 0;

cleanup:
  if (vm)
    virObjectUnref(vm);
  virDomainDefFree(def);
  return ret;
}

static int mymain(void) {
  int ret = 0;

  driver.xmlopt = virMacOSVFDriverDomainXMLConfInit();
  if (!driver.xmlopt)
    return EXIT_FAILURE;

  if (virTestRun("Domain control info", testDomainGetControlInfo, NULL) < 0)
    ret = -1;
  if (virTestRun("Domain block stats", testDomainBlockStats, NULL) < 0)
    ret = -1;
  if (virTestRun("Domain interface stats", testDomainInterfaceStats, NULL) < 0)
    ret = -1;
  if (virTestRun("Domain stats invalid path", testDomainStatsInvalidPath,
                 NULL) < 0)
    ret = -1;

  virObjectUnref(driver.xmlopt);

  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)

#else

int main(void) { return 77; }

#endif /* WITH_MACOSVF */
