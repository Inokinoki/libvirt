/*
 * macosvfmemorystatstest.c: test macOS Virtualization.Framework memory
 * statistics
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
#include "macosvf/macosvf_capabilities.h"
#include "macosvf/macosvf_conf.h"
#include "macosvf/macosvf_domain.h"
#include "macosvf/macosvf_vm.h"
#include "virlog.h"
#include <libvirt/libvirt-domain.h>

#define VIR_FROM_THIS VIR_FROM_NONE

static struct _macosvfConn driver;

/* Test memory statistics for various domain configurations */
static int testMemoryStats(const void *data G_GNUC_UNUSED) {
  struct {
    const char *name;
    unsigned long long memory;
    unsigned int vcpus;
  } configs[] = {{"minimal-512mb", 524288, 1},
                 {"basic-1gb", 1048576, 2},
                 {"advanced-2gb", 2097152, 4},
                 {"large-4gb", 4194304, 8},
                 {NULL, 0, 0}};

  int ret = -1;
  virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];
  int i;

  virTestSetHostArch(VIR_ARCH_AARCH64);

  for (i = 0; configs[i].name != NULL; i++) {
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    int nstats = 0;

    /* Create domain definition */
    def = virDomainDefNew(driver.xmlopt);
    if (!def)
      goto cleanup_loop;

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup(configs[i].name);
    virUUIDGenerate(def->uuid);

    virDomainDefSetVcpusMax(def, configs[i].vcpus, driver.xmlopt);
    virDomainDefSetVcpus(def, configs[i].vcpus);
    def->mem.cur_balloon = configs[i].memory;

    /* Create VM object */
    if (macosvfVMCreate(def, &vm) < 0) {
      fprintf(stderr, "%s: Failed to create VM object\n", __FUNCTION__);
      virDomainDefFree(def);
      goto cleanup_loop;
    }

    /* Get memory statistics */
    memset(stats, 0, sizeof(stats));
    nstats = macosvfVMGetMemoryStats(vm, stats, VIR_DOMAIN_MEMORY_STAT_NR);

    /* Validate statistics */
    if (nstats < 0) {
      fprintf(stderr, "%s: Failed to get memory stats for %s\n", __FUNCTION__,
              configs[i].name);
      macosvfVMFree(vm);
      virDomainDefFree(def);
      goto cleanup_loop;
    }

    {
      bool found_available = false;
      int j;
      for (j = 0; j < nstats; j++) {
        if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE) {
          if (stats[j].val != configs[i].memory) {
            fprintf(stderr, "%s: Expected available memory %llu, got %llu\n",
                    __FUNCTION__, configs[i].memory, stats[j].val);
            macosvfVMFree(vm);
            virDomainDefFree(def);
            goto cleanup_loop;
          }
          found_available = true;
        }
      }

      if (!found_available) {
        fprintf(stderr, "%s: Missing available memory stat for %s\n",
                __FUNCTION__, configs[i].name);
        macosvfVMFree(vm);
        virDomainDefFree(def);
        goto cleanup_loop;
      }
    }

    macosvfVMFree(vm);
    virDomainDefFree(def);
    continue;

  cleanup_loop:
    return -1;
  }

  ret = 0;
  return ret;
}

/* Test memory stats with different memory sizes */
static int testMemoryStatsAccuracy(const void *data G_GNUC_UNUSED) {
  unsigned long long sizes[] = {
      512 * 1024,  /* 512 MB */
      1024 * 1024, /* 1 GB */
      2048 * 1024, /* 2 GB */
  };
  int nsizes = sizeof(sizes) / sizeof(sizes[0]);
  int i;

  virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];

  for (i = 0; i < nsizes; i++) {
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    int nstats;

    def = virDomainDefNew(driver.xmlopt);
    if (!def)
      return -1;
    def->mem.cur_balloon = sizes[i];

    if (macosvfVMCreate(def, &vm) < 0) {
      virDomainDefFree(def);
      return -1;
    }

    nstats = macosvfVMGetMemoryStats(vm, stats, VIR_DOMAIN_MEMORY_STAT_NR);
    if (nstats < 0) {
      macosvfVMFree(vm);
      virDomainDefFree(def);
      return -1;
    }

    {
      bool found_available = false;
      int j;
      for (j = 0; j < nstats; j++) {
        if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE) {
          if (stats[j].val != sizes[i]) {
            macosvfVMFree(vm);
            virDomainDefFree(def);
            return -1;
          }
          found_available = true;
        }
      }
      if (!found_available) {
        macosvfVMFree(vm);
        virDomainDefFree(def);
        return -1;
      }
    }

    macosvfVMFree(vm);
    virDomainDefFree(def);
  }

  return 0;
}

static int mymain(void) {
  int ret = 0;

  driver.xmlopt = virMacOSVFDriverDomainXMLConfInit();
  if (!driver.xmlopt)
    return EXIT_FAILURE;

  if (virTestRun("Memory statistics basic", testMemoryStats, NULL) < 0)
    ret = -1;
  if (virTestRun("Memory statistics accuracy", testMemoryStatsAccuracy, NULL) <
      0)
    ret = -1;

  virObjectUnref(driver.xmlopt);

  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)

#else

int main(void) { return 77; }

#endif /* WITH_MACOSVF */
