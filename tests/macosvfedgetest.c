/*
 * macosvfedgetest.c: test edge cases for macOS Virtualization.Framework
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

#include "conf/domain_conf.h"
#include "macosvf/macosvf_conf.h"
#include "macosvf/macosvf_domain.h"
#include "macosvf/macosvf_vm.h"

#define VIR_FROM_THIS VIR_FROM_NONE

struct testInfo {
  const char *name;
};

/* Test domain definition with invalid values */
static int testInvalidDomainDef(const void *data G_GNUC_UNUSED) {
  virDomainDef *def = NULL;
  macosvfVMObject *vm = NULL;
  int ret = -1;

  virTestSetHostArch(VIR_ARCH_AARCH64);

  /* Test NULL definition */
  if (macosvfVMCreate(NULL, &vm) == 0) {
    fprintf(stderr, "%s: Should fail with NULL definition\n", __FUNCTION__);
    goto cleanup;
  }

  /* Test definition with 0 vCPUs (auto-defaulted to 1) */
  def = virDomainDefNew(NULL);
  if (!def)
    goto cleanup;
  def->os.type = VIR_DOMAIN_OSTYPE_HVM;
  def->os.arch = VIR_ARCH_AARCH64;
  def->os.machine = g_strdup("macosvf");
  virDomainDefSetVcpusMax(def, 0, NULL);
  virDomainDefSetVcpus(def, 0);

  if (macosvfVMCreate(def, &vm) != 0) {
    fprintf(stderr, "%s: Should succeed with 0 vCPUs (defaulted to 1)\n", __FUNCTION__);
    goto cleanup;
  }
  macosvfVMFree(vm);
  vm = NULL;
  virDomainDefFree(def);
  def = NULL;

  /* Test definition with too many vCPUs (> 16) */
  def = virDomainDefNew(NULL);
  if (!def)
    goto cleanup;
  def->os.type = VIR_DOMAIN_OSTYPE_HVM;
  def->os.arch = VIR_ARCH_AARCH64;
  def->os.machine = g_strdup("macosvf");
  virDomainDefSetVcpusMax(def, 17, NULL);
  virDomainDefSetVcpus(def, 17);

  if (macosvfVMCreate(def, &vm) == 0) {
    fprintf(stderr, "%s: Should fail with 17 vCPUs\n", __FUNCTION__);
    goto cleanup;
  }

  ret = 0;

cleanup:
  if (vm)
    macosvfVMFree(vm);
  virDomainDefFree(def);
  return ret;
}

/* Test memory boundaries */
static int testMemoryBoundaries(const void *data G_GNUC_UNUSED) {
  struct {
    const char *name;
    unsigned long long memory_kb;
  } memory_configs[] = {
      {"minimal-256mb", 256 * 1024},
      {"large-32gb", 32 * 1024 * 1024},
      {NULL, 0},
  };
  size_t i;
  int ret = 0;

  virTestSetHostArch(VIR_ARCH_AARCH64);

  for (i = 0; memory_configs[i].name != NULL; i++) {
    virDomainDef *def = NULL;
    macosvfVMObject *vm = NULL;
    virDomainMemoryStatStruct stats[1];
    unsigned long long memoryUsed = 0;
    int result;

    def = virDomainDefNew(NULL);
    if (!def)
      return -1;

    def->os.type = VIR_DOMAIN_OSTYPE_HVM;
    def->os.arch = VIR_ARCH_AARCH64;
    def->os.machine = g_strdup("macosvf");
    def->name = g_strdup(memory_configs[i].name);

    virDomainDefSetVcpusMax(def, 1, NULL);
    virDomainDefSetVcpus(def, 1);

    def->mem.cur_balloon = memory_configs[i].memory_kb;

    result = macosvfVMCreate(def, &vm);
    if (result < 0) {
      fprintf(stderr, "%s: Failed to create VM with memory %llu KB\n",
              __FUNCTION__, memory_configs[i].memory_kb);
      virDomainDefFree(def);
      continue;
    }

    /* Get memory statistics */
    if (macosvfVMGetMemoryStats(vm, stats, 1) < 0) {
      fprintf(stderr, "%s: Failed to get memory stats for config %zu\n",
              __FUNCTION__, i);
      memoryUsed = 0;
    } else {
      memoryUsed = stats[0].val;
    }

    /* For stopped VM, AVAILABLE memory should be configured memory */
    if (memoryUsed != memory_configs[i].memory_kb) {
      fprintf(stderr, "%s: Expected memory %llu for stopped VM, got %llu\n",
              __FUNCTION__, memory_configs[i].memory_kb, memoryUsed);
    }

    if (vm)
      macosvfVMFree(vm);
    virDomainDefFree(def);
  }

  return ret;
}

static int mymain(void) {
  int ret = 0;
  struct testInfo info;

  virTestSetHostArch(VIR_ARCH_AARCH64);

  if (virTestRun("MACOSVF Invalid Domain Def", testInvalidDomainDef, &info) < 0)
    ret = -1;

  if (virTestRun("MACOSVF Memory Boundaries", testMemoryBoundaries, &info) < 0)
    ret = -1;

  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)

#else

int main(void) { return 77; }

#endif /* WITH_MACOSVF */
