/*
 * macosvf_driver.c: core driver methods for managing macOS
 * Virtualization.Framework guests
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

#include <dirent.h>
#include <fcntl.h>
#include <sys/utsname.h>

#include "conf/domain_capabilities.h"
#include "configmake.h"
#include "cpu/cpu.h"
#include "datatypes.h"
#include "domain_audit.h"
#include "domain_driver.h"
#include "domain_event.h"
#include "interface_conf.h"
#include "network_conf.h"
#include "node_device_conf.h"
#include "snapshot_conf.h"
#include "storage_conf.h"
#include "viraccessapicheck.h"
#include "viralloc.h"
#include "virbuffer.h"
#include "virdomainobjlist.h"
#include "virerror.h"
#include "virfdstream.h"
#include "virfile.h"
#include "virhostcpu.h"
#include "virhostmem.h"
#include "virlog.h"
#include "virnetdevtap.h"
#include "virpidfile.h"
#include "virportallocator.h"
#include "virstring.h"
#include "virthread.h"
#include "virtypedparam.h"
#include "virutil.h"
#include "viruuid.h"
#include "virxml.h"

#include "macosvf_capabilities.h"
#include "macosvf_conf.h"
#include "macosvf_device.h"
#include "macosvf_domain.h"
#include "macosvf_driver.h"
#include "macosvf_vm.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macOSvf_driver");

struct _macosvfConn *macosvf_driver = NULL;

static int macosvfAutostartDomain(virDomainObj *vm,
                                  void *opaque G_GNUC_UNUSED) {
  macosvfDomainObjPrivate *priv;
  macosvfVMObject *vmobj = NULL;

  /* Skip if domain is already running */
  if (virDomainObjIsActive(vm))
    return 0;

  /* Check if domain has autostart enabled */
  if (!vm->autostart)
    return 0;

  VIR_INFO("Starting autostart domain '%s'", vm->def->name);

  priv = vm->privateData;

  /* Create VM object if needed */
  if (!priv->vm) {
    if (macosvfVMCreate(vm->def, &vmobj) < 0) {
      VIR_WARN("Failed to create VM object for autostart domain '%s'",
               vm->def->name);
      return 0;
    }
    priv->vm = vmobj;
  }

  /* Start the VM */
  if (macosvfVMStart((macosvfVMObject *)priv->vm) < 0) {
    VIR_WARN("Failed to start autostart domain '%s'", vm->def->name);
    return 0;
  }

  virDomainObjSetState(vm, VIR_DOMAIN_RUNNING, VIR_DOMAIN_RUNNING_BOOTED);
  VIR_INFO("Successfully started autostart domain '%s'", vm->def->name);
  return 0;
}

/* Save domain configuration to file */
static int macosvfSaveDomainConfig(macosvfConn *driver, virDomainObj *vm) {
  g_autofree char *configFile = NULL;
  g_autofree char *xml = NULL;

  if (!driver->configDir)
    return 0;

  /* Create config directory if it doesn't exist */
  if (!virFileExists(driver->configDir)) {
    if (g_mkdir_with_parents(driver->configDir, 0777) < 0) {
      virReportSystemError(errno, _("Cannot create config directory %1$s"),
                           driver->configDir);
      return -1;
    }
  }

  /* Generate config file path */
  configFile = g_strdup_printf("%s/%s.xml", driver->configDir, vm->def->name);

  /* Format domain XML */
  if (!(xml = virDomainDefFormat(vm->def, NULL, VIR_DOMAIN_XML_INACTIVE)))
    return -1;

  /* Write to file */
  if (virFileWriteStr(configFile, xml, 0600) < 0) {
    virReportSystemError(errno, _("Cannot write config file %1$s"), configFile);
    return -1;
  }

  VIR_DEBUG("Saved domain '%s' config to %s", vm->def->name, configFile);
  return 0;
}

/* Delete domain configuration file */
static int macosvfDeleteDomainConfig(macosvfConn *driver, virDomainObj *vm) {
  g_autofree char *configFile = NULL;

  if (!driver->configDir)
    return 0;

  configFile = g_strdup_printf("%s/%s.xml", driver->configDir, vm->def->name);

  if (virFileExists(configFile)) {
    if (unlink(configFile) < 0) {
      virReportSystemError(errno, _("Cannot delete config file %1$s"),
                           configFile);
      return -1;
    }
  }

  VIR_DEBUG("Deleted domain '%s' config file %s", vm->def->name, configFile);
  return 0;
}

/* Helper functions */

extern virDomainObj *macosvfDomObjFromDomain(virDomainPtr domain);

/* Driver interface functions */

static const char *macosvfConnectGetType(virConnectPtr conn G_GNUC_UNUSED) {
  return "macosvf";
}

static int macosvfConnectGetVersion(virConnectPtr conn,
                                    unsigned long *hvVer) {
  struct utsname utsname;
  unsigned long long version;

  if (virConnectGetVersionEnsureACL(conn) < 0)
    return -1;

  if (uname(&utsname) < 0) {
    virReportSystemError(errno, "%s", _("cannot get host version"));
    return -1;
  }

  if (virStringParseVersion(&version, utsname.release, true) < 0) {
    virReportError(VIR_ERR_INTERNAL_ERROR, _("cannot parse version %1$s"),
                   utsname.release);
    return -1;
  }

  *hvVer = (unsigned long)version;
  return 0;
}

static char *macosvfConnectGetHostname(virConnectPtr conn) {
  char *hostname;

  if (virConnectGetHostnameEnsureACL(conn) < 0)
    return NULL;

  if ((hostname = virGetHostname()) == NULL)
    return NULL;

  return hostname;
}

static int macosvfNodeGetInfo(virConnectPtr conn, virNodeInfoPtr nodeinfo) {
  if (virNodeGetInfoEnsureACL(conn) < 0)
    return -1;

  return virCapabilitiesGetNodeInfo(nodeinfo);
}

static char *macosvfConnectGetCapabilities(virConnectPtr conn) {
  macosvfConn *privconn = conn->privateData;
  g_autoptr(virCaps) caps = NULL;

  if (virConnectGetCapabilitiesEnsureACL(conn) < 0)
    return NULL;

  if (!(caps = macosvfDriverGetCapabilities(privconn))) {
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                   _("Unable to get Capabilities"));
    return NULL;
  }

  return virCapabilitiesFormatXML(caps);
}

static int macosvfConnectListDomains(virConnectPtr conn, int *ids, int maxids) {
  macosvfConn *privconn = conn->privateData;
  int n;

  if (virConnectListDomainsEnsureACL(conn) < 0)
    return -1;

  n = virDomainObjListGetActiveIDs(privconn->domains, ids, maxids,
                                   virConnectListDomainsCheckACL, conn);

  return n;
}

static int macosvfConnectNumOfDomains(virConnectPtr conn) {
  macosvfConn *privconn = conn->privateData;
  int n;

  if (virConnectNumOfDomainsEnsureACL(conn) < 0)
    return -1;

  n = virDomainObjListNumOfDomains(privconn->domains, true,
                                   virConnectNumOfDomainsCheckACL, conn);

  return n;
}

struct macosvfListData {
  virDomainPtr *doms;
  virConnectPtr conn;
  int count;
};

static int macosvfListCallback(virDomainObj *obj,
                                void *opaque)
{
  struct macosvfListData *data = opaque;

  virObjectLock(obj);
  data->doms[data->count] = virGetDomain(data->conn, obj->def->name,
                                          obj->def->uuid, -1);
  virObjectUnlock(obj);

  if (!data->doms[data->count])
    return -1;

  data->count++;
  return 0;
}

static int macosvfConnectListAllDomains(virConnectPtr conn,
                                        virDomainPtr **domains,
                                        unsigned int flags) {
  macosvfConn *privconn = conn->privateData;
  struct macosvfListData data = { NULL, conn, 0 };
  int n = 0;

  VIR_WARN("macosvfConnectListAllDomains: Entry, flags=%u", flags);

  if (virConnectListAllDomainsEnsureACL(conn) < 0)
    return -1;

  VIR_WARN("macosvfConnectListAllDomains: Counting domains");
  /* Count domains */
  n = virDomainObjListNumOfDomains(privconn->domains, false, NULL, NULL);

  VIR_WARN("macosvfConnectListAllDomains: Found %d domains", n);

  if (n <= 0)
    return n;

  data.doms = g_new0(virDomainPtr, n);
  if (!data.doms)
    return -1;

  VIR_WARN("macosvfConnectListAllDomains: Iterating domains");
  /* Collect domain references - virDomainObjListForEach handles locking internally */
  virDomainObjListForEach(privconn->domains, false,
                         macosvfListCallback, &data);

  *domains = data.doms;
  VIR_WARN("macosvfConnectListAllDomains: Returning %d", data.count);

  return data.count;
}

static virDomainPtr macosvfDomainLookupByID(virConnectPtr conn, int id) {
  macosvfConn *privconn = conn->privateData;
  virDomainObj *vm = NULL;
  virDomainPtr dom = NULL;

  if (!(vm = virDomainObjListFindByID(privconn->domains, id)))
    return NULL;

  if (virDomainLookupByIDEnsureACL(conn, vm->def) < 0) {
    virObjectUnref(vm);
    return NULL;
  }

  dom = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);
  virObjectUnref(vm);

  return dom;
}

static virDomainPtr macosvfDomainLookupByUUID(virConnectPtr conn,
                                              const unsigned char *uuid) {
  macosvfConn *privconn = conn->privateData;
  virDomainObj *vm = NULL;
  virDomainPtr dom = NULL;

  if (!(vm = virDomainObjListFindByUUID(privconn->domains, uuid)))
    return NULL;

  if (virDomainLookupByUUIDEnsureACL(conn, vm->def) < 0) {
    virObjectUnref(vm);
    return NULL;
  }

  dom = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);
  virObjectUnref(vm);

  return dom;
}

static virDomainPtr macosvfDomainLookupByName(virConnectPtr conn,
                                              const char *name) {
  macosvfConn *privconn = conn->privateData;
  virDomainObj *vm = NULL;
  virDomainPtr dom = NULL;

  if (!(vm = virDomainObjListFindByName(privconn->domains, name)))
    return NULL;

  if (virDomainLookupByNameEnsureACL(conn, vm->def) < 0) {
    virObjectUnref(vm);
    return NULL;
  }

  dom = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);
  virObjectUnref(vm);

  return dom;
}

static int macosvfConnectGetMaxVcpus(virConnectPtr conn G_GNUC_UNUSED,
                                     const char *type G_GNUC_UNUSED) {
  if (virConnectGetMaxVcpusEnsureACL(conn) < 0)
    return -1;

  /* Return a reasonable default for now */
  return 4;
}

static int macosvfDomainGetInfo(virDomainPtr dom, virDomainInfoPtr info) {
  virDomainObj *vm;
  macosvfDomainObjPrivate *priv;
  unsigned long long cpuTime = 0;
  unsigned long long memoryUsed = 0;
  virDomainMemoryStatStruct mstats[VIR_DOMAIN_MEMORY_STAT_NR];
  int nstats = 0;
  int i;

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainGetInfoEnsureACL(dom->conn, vm->def) < 0) {
    virObjectUnref(vm);
    return -1;
  }

  /* Get basic info */
  info->state = virDomainObjGetState(vm, NULL);
  info->maxMem = virDomainDefGetMemoryTotal(vm->def);
  info->nrVirtCpu = virDomainDefGetVcpus(vm->def);

  /* Get statistics if VM is running */
  priv = vm->privateData;
  if (priv && priv->vm) {
    macosvfVMGetCPUStats((macosvfVMObject *)priv->vm, &cpuTime);
    nstats = macosvfVMGetMemoryStats((macosvfVMObject *)priv->vm, mstats,
                                     VIR_DOMAIN_MEMORY_STAT_NR);
    for (i = 0; i < nstats; i++) {
      if (mstats[i].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE)
        memoryUsed = mstats[i].val;
    }
  }

  info->cpuTime = cpuTime;
  info->memory =
      memoryUsed > 0 ? memoryUsed : virDomainDefGetMemoryTotal(vm->def);

  virObjectUnref(vm);
  return 0;
}

static int macosvfDomainGetState(virDomainPtr dom, int *state, int *reason,
                                 unsigned int flags) {
  virDomainObj *vm;
  int ret = -1;

  virCheckFlags(0, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainGetStateEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  *state = virDomainObjGetState(vm, reason);
  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static char *macosvfDomainGetXMLDesc(virDomainPtr dom, unsigned int flags) {
  virDomainObj *vm;
  char *ret = NULL;

  virCheckFlags(VIR_DOMAIN_XML_INACTIVE | VIR_DOMAIN_XML_MIGRATABLE, NULL);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return NULL;

  if (virDomainGetXMLDescEnsureACL(dom->conn, vm->def, flags) < 0)
    goto cleanup;

  ret = virDomainDefFormat(vm->def, NULL, flags);

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainMemoryStats(virDomainPtr dom,
                                    virDomainMemoryStatPtr stats,
                                    unsigned int nr_stats, unsigned int flags) {
  virDomainObj *vm;
  int ret = -1;
  unsigned int i = 0;

  virCheckFlags(0, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainMemoryStatsEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  /* Only provide statistics if VM is running or paused */
  if (!virDomainObjIsActive(vm)) {
    virReportError(VIR_ERR_OPERATION_INVALID, "%s", _("domain is not running"));
    goto cleanup;
  }

  /* Provide available memory statistics */
  /* Since macOS Virtualization.Framework doesn't provide detailed memory stats,
   * we report the configured memory values */

  /* Available memory in KiB (total memory assigned to domain) */
  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_AVAILABLE;
    stats[i].val = vm->def->mem.cur_balloon;
    i++;
  }

  /* Actual balloon size (no ballooning in macOSVF, so same as available) */
  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON;
    stats[i].val = vm->def->mem.cur_balloon;
    i++;
  }

  /* RSS (Resident Set Size) - not available from framework */
  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_RSS;
    stats[i].val = 0; /* Not supported */
    i++;
  }

  /* Major page faults - not tracked */
  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_MAJOR_FAULT;
    stats[i].val = 0; /* Not supported */
    i++;
  }

  /* Minor page faults - not tracked */
  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_MINOR_FAULT;
    stats[i].val = 0; /* Not supported */
    i++;
  }

  ret = i;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static char *macosvfDomainGetSchedulerType(virDomainPtr dom,
                                           int *nparams) {
  virDomainObj *vm;
  char *ret = NULL;

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return NULL;

  if (virDomainGetSchedulerTypeEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  /* macOS Virtualization.Framework uses a fixed scheduler */
  if (nparams)
    *nparams = 1;

  ret = g_strdup("macosvf");

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainGetSchedulerParametersFlags(virDomainPtr dom,
                                                    virTypedParameterPtr params,
                                                    int *nparams,
                                                    unsigned int flags) {
  virDomainObj *vm;
  int ret = -1;

  virCheckFlags(0, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainGetSchedulerParametersEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  /* Report CPU shares - this is a basic scheduling parameter */
  if (virTypedParameterAssign(params, VIR_DOMAIN_SCHEDULER_CPU_SHARES,
                              VIR_TYPED_PARAM_ULLONG, 1024) < 0)
    goto cleanup;

  *nparams = 1;
  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainGetSchedulerParameters(virDomainPtr dom,
                                               virTypedParameterPtr params,
                                               int *nparams) {
  return macosvfDomainGetSchedulerParametersFlags(dom, params, nparams, 0);
}

static int macosvfDomainSetSchedulerParametersFlags(virDomainPtr dom,
                                                    virTypedParameterPtr params,
                                                    int nparams,
                                                    unsigned int flags) {
  virDomainObj *vm;
  int ret = -1;
  size_t i;

  virCheckFlags(0, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainSetSchedulerParametersEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  /* Validate parameters - macOSVF has limited scheduler tunables */
  if (virTypedParamsValidate(params, nparams, VIR_DOMAIN_SCHEDULER_CPU_SHARES,
                             VIR_TYPED_PARAM_ULLONG, NULL) < 0)
    goto cleanup;

  /* Since macOS Virtualization.Framework manages CPU scheduling internally,
   * we accept but don't enforce these parameters. This allows management
   * tools to set scheduler policies without errors, even though the
   * framework handles scheduling automatically. */
  for (i = 0; i < nparams; i++) {
    if (STREQ(params[i].field, VIR_DOMAIN_SCHEDULER_CPU_SHARES)) {
      VIR_DEBUG("Ignoring CPU shares request: %llu", params[i].value.ul);
      /* Framework manages CPU internally, we just acknowledge the request */
    }
  }

  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainSetSchedulerParameters(virDomainPtr dom,
                                               virTypedParameterPtr params,
                                               int nparams) {
  return macosvfDomainSetSchedulerParametersFlags(dom, params, nparams, 0);
}

static int macosvfDomainSetBlockIoTune(virDomainPtr dom, const char *path,
                                       virTypedParameterPtr params, int nparams,
                                       unsigned int flags) {
  virDomainObj *vm;
  virDomainDiskDef *disk;
  int ret = -1;

  virCheckFlags(VIR_DOMAIN_AFFECT_LIVE | VIR_DOMAIN_AFFECT_CONFIG, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainSetBlockIoTuneEnsureACL(dom->conn, vm->def, flags) < 0)
    goto cleanup;

  /* Find the disk */
  disk = virDomainDiskByTarget(vm->def, path);
  if (!disk) {
    virReportError(VIR_ERR_INVALID_ARG, _("invalid path: %1$s"), path);
    goto cleanup;
  }

  /* Validate parameters - macOSVF has limited I/O tuning capabilities */
  if (virTypedParamsValidate(
          params, nparams, VIR_DOMAIN_BLOCK_IOTUNE_TOTAL_BYTES_SEC,
          VIR_TYPED_PARAM_ULLONG, VIR_DOMAIN_BLOCK_IOTUNE_READ_BYTES_SEC,
          VIR_TYPED_PARAM_ULLONG, VIR_DOMAIN_BLOCK_IOTUNE_WRITE_BYTES_SEC,
          VIR_TYPED_PARAM_ULLONG, VIR_DOMAIN_BLOCK_IOTUNE_TOTAL_IOPS_SEC,
          VIR_TYPED_PARAM_ULLONG, VIR_DOMAIN_BLOCK_IOTUNE_READ_IOPS_SEC,
          VIR_TYPED_PARAM_ULLONG, VIR_DOMAIN_BLOCK_IOTUNE_WRITE_IOPS_SEC,
          VIR_TYPED_PARAM_ULLONG, NULL) < 0)
    goto cleanup;

  /* Since macOS Virtualization.Framework manages I/O internally,
   * we accept but don't enforce these parameters. This allows management
   * tools to set I/O policies without errors, even though the
   * framework handles I/O throttling automatically. */
  VIR_DEBUG("Ignoring I/O tune request for disk %s (framework manages I/O "
            "internally)",
            path);

  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainGetBlockIoTune(virDomainPtr dom, const char *path,
                                       virTypedParameterPtr params,
                                       int *nparams, unsigned int flags) {
  virDomainObj *vm;
  virDomainDiskDef *disk;
  int ret = -1;
  int i;

  virCheckFlags(VIR_DOMAIN_AFFECT_LIVE | VIR_DOMAIN_AFFECT_CONFIG, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainGetBlockIoTuneEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  /* Find the disk */
  disk = virDomainDiskByTarget(vm->def, path);
  if (!disk) {
    virReportError(VIR_ERR_INVALID_ARG, _("invalid path: %1$s"), path);
    goto cleanup;
  }

  /* Return parameter count if params is NULL */
  if (params == NULL) {
    *nparams = 6;
    ret = 0;
    goto cleanup;
  }

  /* Report I/O tuning parameters - all set to 0 (no throttling in macOSVF) */
  i = 0;
  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i],
                                VIR_DOMAIN_BLOCK_IOTUNE_TOTAL_BYTES_SEC,
                                VIR_TYPED_PARAM_ULLONG, 0) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i],
                                VIR_DOMAIN_BLOCK_IOTUNE_READ_BYTES_SEC,
                                VIR_TYPED_PARAM_ULLONG, 0) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i],
                                VIR_DOMAIN_BLOCK_IOTUNE_WRITE_BYTES_SEC,
                                VIR_TYPED_PARAM_ULLONG, 0) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i],
                                VIR_DOMAIN_BLOCK_IOTUNE_TOTAL_IOPS_SEC,
                                VIR_TYPED_PARAM_ULLONG, 0) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i],
                                VIR_DOMAIN_BLOCK_IOTUNE_READ_IOPS_SEC,
                                VIR_TYPED_PARAM_ULLONG, 0) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i],
                                VIR_DOMAIN_BLOCK_IOTUNE_WRITE_IOPS_SEC,
                                VIR_TYPED_PARAM_ULLONG, 0) < 0)
      goto cleanup;
    i++;
  }

  *nparams = i;
  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainSetMemoryParameters(virDomainPtr dom,
                                            virTypedParameterPtr params,
                                            int nparams, unsigned int flags) {
  virDomainObj *vm;
  virDomainDef *def;
  virDomainDef *persistentDef;
  int ret = -1;
  size_t i;

  virCheckFlags(VIR_DOMAIN_AFFECT_LIVE | VIR_DOMAIN_AFFECT_CONFIG |
                    VIR_TYPED_PARAM_STRING_OKAY,
                -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainSetMemoryParametersEnsureACL(dom->conn, vm->def, flags) < 0)
    goto cleanup;

  /* Validate parameters */
  if (virTypedParamsValidate(
          params, nparams, VIR_DOMAIN_MEMORY_HARD_LIMIT, VIR_TYPED_PARAM_ULLONG,
          VIR_DOMAIN_MEMORY_SOFT_LIMIT, VIR_TYPED_PARAM_ULLONG,
          VIR_DOMAIN_MEMORY_MIN_GUARANTEE, VIR_TYPED_PARAM_ULLONG,
          VIR_DOMAIN_MEMORY_SWAP_HARD_LIMIT, VIR_TYPED_PARAM_ULLONG, NULL) < 0)
    goto cleanup;

  if (!(def = virDomainObjGetOneDef(vm, flags)))
    goto cleanup;

  if (flags & VIR_DOMAIN_AFFECT_CONFIG) {
    if (!vm->persistent) {
      virReportError(
          VIR_ERR_OPERATION_INVALID, "%s",
          _("cannot change persistent config of a transient domain"));
      goto cleanup;
    }
    persistentDef = vm->newDef ? vm->newDef : vm->def;
  } else {
    persistentDef = NULL;
  }

  /* Apply memory parameters */
  for (i = 0; i < nparams; i++) {
    if (STREQ(params[i].field, VIR_DOMAIN_MEMORY_HARD_LIMIT)) {
      unsigned long long hard_limit = params[i].value.ul;
      /* Validate hard_limit */
      if (hard_limit != 0 && hard_limit < def->mem.cur_balloon) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("memory hard_limit tunable value must be higher than "
                         "current memory"));
        goto cleanup;
      }
      if (flags & VIR_DOMAIN_AFFECT_LIVE)
        def->mem.hard_limit = hard_limit;
      if (flags & VIR_DOMAIN_AFFECT_CONFIG && persistentDef)
        persistentDef->mem.hard_limit = hard_limit;
    } else if (STREQ(params[i].field, VIR_DOMAIN_MEMORY_SOFT_LIMIT)) {
      unsigned long long soft_limit = params[i].value.ul;
      if (flags & VIR_DOMAIN_AFFECT_LIVE)
        def->mem.soft_limit = soft_limit;
      if (flags & VIR_DOMAIN_AFFECT_CONFIG && persistentDef)
        persistentDef->mem.soft_limit = soft_limit;
    } else if (STREQ(params[i].field, VIR_DOMAIN_MEMORY_MIN_GUARANTEE)) {
      unsigned long long min_guarantee = params[i].value.ul;
      if (flags & VIR_DOMAIN_AFFECT_LIVE)
        def->mem.min_guarantee = min_guarantee;
      if (flags & VIR_DOMAIN_AFFECT_CONFIG && persistentDef)
        persistentDef->mem.min_guarantee = min_guarantee;
    } else if (STREQ(params[i].field, VIR_DOMAIN_MEMORY_SWAP_HARD_LIMIT)) {
      unsigned long long swap_hard_limit = params[i].value.ul;
      if (flags & VIR_DOMAIN_AFFECT_LIVE)
        def->mem.swap_hard_limit = swap_hard_limit;
      if (flags & VIR_DOMAIN_AFFECT_CONFIG && persistentDef)
        persistentDef->mem.swap_hard_limit = swap_hard_limit;
    }
  }

  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainGetMemoryParameters(virDomainPtr dom,
                                            virTypedParameterPtr params,
                                            int *nparams, unsigned int flags) {
  virDomainObj *vm;
  virDomainDef *def;
  int ret = -1;
  int i;

  virCheckFlags(VIR_DOMAIN_AFFECT_LIVE | VIR_DOMAIN_AFFECT_CONFIG |
                    VIR_TYPED_PARAM_STRING_OKAY,
                -1);

  if (*nparams == 0) {
    *nparams = 4;
    return 0;
  }

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainGetMemoryParametersEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  if (!(def = virDomainObjGetOneDef(vm, flags)))
    goto cleanup;

  /* Report memory parameters */
  i = 0;
  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_MEMORY_HARD_LIMIT,
                                VIR_TYPED_PARAM_ULLONG,
                                def->mem.hard_limit) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_MEMORY_SOFT_LIMIT,
                                VIR_TYPED_PARAM_ULLONG,
                                def->mem.soft_limit) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_MEMORY_MIN_GUARANTEE,
                                VIR_TYPED_PARAM_ULLONG,
                                def->mem.min_guarantee) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_MEMORY_SWAP_HARD_LIMIT,
                                VIR_TYPED_PARAM_ULLONG,
                                def->mem.swap_hard_limit) < 0)
      goto cleanup;
    i++;
  }

  *nparams = i;
  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainSetNumaParameters(virDomainPtr dom,
                                          virTypedParameterPtr params,
                                          int nparams, unsigned int flags) {
  virDomainObj *vm;
  virDomainDef *def;
  virDomainDef *persistentDef;
  int ret = -1;
  int mode = -1;
  char *nodeset = NULL;
  size_t i;

  virCheckFlags(VIR_DOMAIN_AFFECT_LIVE | VIR_DOMAIN_AFFECT_CONFIG |
                    VIR_TYPED_PARAM_STRING_OKAY,
                -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainSetNumaParametersEnsureACL(dom->conn, vm->def, flags) < 0)
    goto cleanup;

  /* Validate parameters */
  if (virTypedParamsValidate(params, nparams, VIR_DOMAIN_NUMA_MODE,
                             VIR_TYPED_PARAM_INT, VIR_DOMAIN_NUMA_NODESET,
                             VIR_TYPED_PARAM_STRING, NULL) < 0)
    goto cleanup;

  if (!(def = virDomainObjGetOneDef(vm, flags)))
    goto cleanup;

  if (flags & VIR_DOMAIN_AFFECT_CONFIG) {
    if (!vm->persistent) {
      virReportError(
          VIR_ERR_OPERATION_INVALID, "%s",
          _("cannot change persistent config of a transient domain"));
      goto cleanup;
    }
    persistentDef = vm->newDef ? vm->newDef : vm->def;
  } else {
    persistentDef = NULL;
  }

  /* Extract parameters */
  for (i = 0; i < nparams; i++) {
    if (STREQ(params[i].field, VIR_DOMAIN_NUMA_MODE)) {
      mode = params[i].value.i;
    } else if (STREQ(params[i].field, VIR_DOMAIN_NUMA_NODESET)) {
      nodeset = params[i].value.s;
    }
  }

  /* Validate NUMA mode */
  if (mode != -1 && mode != VIR_DOMAIN_NUMATUNE_MEM_STRICT &&
      mode != VIR_DOMAIN_NUMATUNE_MEM_PREFERRED &&
      mode != VIR_DOMAIN_NUMATUNE_MEM_INTERLEAVE) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("unsupported numa mode"));
    goto cleanup;
  }

  /* Apply NUMA parameters */
  /* Note: Apple Silicon uses unified memory architecture, so NUMA tuning
   * has limited effect. We accept the parameters for compatibility but
   * don't enforce strict NUMA policies. */
  if (mode != -1) {
    if (flags & VIR_DOMAIN_AFFECT_LIVE) {
      /* Can't change NUMA mode for running domain on macOSVF */
      if (virDomainObjIsActive(vm)) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("cannot change numa mode for running domain"));
        goto cleanup;
      }
    }
    if (flags & VIR_DOMAIN_AFFECT_CONFIG && persistentDef) {
      /* Store the mode - actual enforcement is limited on Apple Silicon */
      /* Note: We can't directly modify the numa structure, so we just
       * accept the parameter for compatibility */
      VIR_DEBUG("Setting NUMA mode to %d (framework manages NUMA internally)",
                mode);
    }
  }

  if (nodeset) {
    /* Validate and store nodeset */
    /* On Apple Silicon, we accept but don't enforce nodeset */
    VIR_DEBUG("Setting NUMA nodeset to %s (framework manages NUMA internally)",
              nodeset);
  }

  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainGetNumaParameters(virDomainPtr dom,
                                          virTypedParameterPtr params,
                                          int *nparams, unsigned int flags) {
  virDomainObj *vm;
  virDomainDef *def;
  virDomainNumatuneMemMode mode = VIR_DOMAIN_NUMATUNE_MEM_STRICT;
  g_autofree char *nodeset = NULL;
  int ret = -1;
  int i;

  virCheckFlags(VIR_DOMAIN_AFFECT_LIVE | VIR_DOMAIN_AFFECT_CONFIG |
                    VIR_TYPED_PARAM_STRING_OKAY,
                -1);

  if (*nparams == 0) {
    *nparams = 2;
    return 0;
  }

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainGetNumaParametersEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  if (!(def = virDomainObjGetOneDef(vm, flags)))
    goto cleanup;

  /* Get NUMA mode */
  ignore_value(virDomainNumatuneGetMode(def->numa, -1, &mode));

  /* Format nodeset */
  nodeset = virDomainNumatuneFormatNodeset(def->numa, NULL, -1);
  if (!nodeset)
    nodeset = g_strdup("");

  /* Report NUMA parameters */
  i = 0;
  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_NUMA_MODE,
                                VIR_TYPED_PARAM_INT, mode) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_NUMA_NODESET,
                                VIR_TYPED_PARAM_STRING, nodeset) < 0)
      goto cleanup;
    i++;
  }

  *nparams = i;
  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainSetInterfaceParameters(virDomainPtr dom,
                                               const char *device,
                                               virTypedParameterPtr params,
                                               int nparams,
                                               unsigned int flags) {
  virDomainObj *vm;
  virDomainDef *def;
  virDomainDef *persistentDef;
  virDomainNetDef *net = NULL;
  virNetDevBandwidth *bandwidth = NULL;
  bool inboundSpecified = false;
  bool outboundSpecified = false;
  int ret = -1;
  size_t i;

  virCheckFlags(VIR_DOMAIN_AFFECT_LIVE | VIR_DOMAIN_AFFECT_CONFIG, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainSetInterfaceParametersEnsureACL(dom->conn, vm->def, flags) < 0)
    goto cleanup;

  /* Validate parameters */
  if (virTypedParamsValidate(
          params, nparams, VIR_DOMAIN_BANDWIDTH_IN_AVERAGE,
          VIR_TYPED_PARAM_UINT, VIR_DOMAIN_BANDWIDTH_IN_PEAK,
          VIR_TYPED_PARAM_UINT, VIR_DOMAIN_BANDWIDTH_IN_BURST,
          VIR_TYPED_PARAM_UINT, VIR_DOMAIN_BANDWIDTH_IN_FLOOR,
          VIR_TYPED_PARAM_UINT, VIR_DOMAIN_BANDWIDTH_OUT_AVERAGE,
          VIR_TYPED_PARAM_UINT, VIR_DOMAIN_BANDWIDTH_OUT_PEAK,
          VIR_TYPED_PARAM_UINT, VIR_DOMAIN_BANDWIDTH_OUT_BURST,
          VIR_TYPED_PARAM_UINT, NULL) < 0)
    goto cleanup;

  if (!(def = virDomainObjGetOneDef(vm, flags)))
    goto cleanup;

  if (flags & VIR_DOMAIN_AFFECT_CONFIG) {
    if (!vm->persistent) {
      virReportError(
          VIR_ERR_OPERATION_INVALID, "%s",
          _("cannot change persistent config of a transient domain"));
      goto cleanup;
    }
    persistentDef = vm->newDef ? vm->newDef : vm->def;
  } else {
    persistentDef = NULL;
  }

  /* Find the network interface */
  net = virDomainNetFind(def, device);
  if (!net) {
    virReportError(VIR_ERR_INVALID_ARG, _("invalid interface name: %1$s"),
                   device);
    goto cleanup;
  }

  /* Extract bandwidth parameters */
  bandwidth = g_new0(virNetDevBandwidth, 1);

  for (i = 0; i < nparams; i++) {
    if (STREQ(params[i].field, VIR_DOMAIN_BANDWIDTH_IN_AVERAGE)) {
      if (!bandwidth->in)
        bandwidth->in = g_new0(virNetDevBandwidthRate, 1);
      bandwidth->in->average = params[i].value.ui;
      inboundSpecified = true;
    } else if (STREQ(params[i].field, VIR_DOMAIN_BANDWIDTH_IN_PEAK)) {
      if (!bandwidth->in)
        bandwidth->in = g_new0(virNetDevBandwidthRate, 1);
      bandwidth->in->peak = params[i].value.ui;
    } else if (STREQ(params[i].field, VIR_DOMAIN_BANDWIDTH_IN_BURST)) {
      if (!bandwidth->in)
        bandwidth->in = g_new0(virNetDevBandwidthRate, 1);
      bandwidth->in->burst = params[i].value.ui;
    } else if (STREQ(params[i].field, VIR_DOMAIN_BANDWIDTH_IN_FLOOR)) {
      if (!bandwidth->in)
        bandwidth->in = g_new0(virNetDevBandwidthRate, 1);
      bandwidth->in->floor = params[i].value.ui;
    } else if (STREQ(params[i].field, VIR_DOMAIN_BANDWIDTH_OUT_AVERAGE)) {
      if (!bandwidth->out)
        bandwidth->out = g_new0(virNetDevBandwidthRate, 1);
      bandwidth->out->average = params[i].value.ui;
      outboundSpecified = true;
    } else if (STREQ(params[i].field, VIR_DOMAIN_BANDWIDTH_OUT_PEAK)) {
      if (!bandwidth->out)
        bandwidth->out = g_new0(virNetDevBandwidthRate, 1);
      bandwidth->out->peak = params[i].value.ui;
    } else if (STREQ(params[i].field, VIR_DOMAIN_BANDWIDTH_OUT_BURST)) {
      if (!bandwidth->out)
        bandwidth->out = g_new0(virNetDevBandwidthRate, 1);
      bandwidth->out->burst = params[i].value.ui;
    }
  }

  /* Apply bandwidth settings to interface */
  /* Note: macOS Virtualization.Framework manages network interfaces internally,
   * so bandwidth limits are accepted but not enforced. This provides
   * compatibility with management tools that expect to set these parameters. */
  if (flags & VIR_DOMAIN_AFFECT_LIVE) {
    /* Store bandwidth in live definition */
    if (!net->bandwidth)
      net->bandwidth = g_steal_pointer(&bandwidth);
    else {
      if (bandwidth->in) {
        VIR_FREE(net->bandwidth->in);
        net->bandwidth->in = g_steal_pointer(&bandwidth->in);
      } else if (inboundSpecified) {
        VIR_FREE(net->bandwidth->in);
      }
      if (bandwidth->out) {
        VIR_FREE(net->bandwidth->out);
        net->bandwidth->out = g_steal_pointer(&bandwidth->out);
      } else if (outboundSpecified) {
        VIR_FREE(net->bandwidth->out);
      }
    }
    VIR_DEBUG("Setting interface bandwidth for %s (framework manages network "
              "internally)",
              device);
  }

  if (flags & VIR_DOMAIN_AFFECT_CONFIG && persistentDef) {
    virDomainNetDef *persistentNet = NULL;
    persistentNet = virDomainNetFind(persistentDef, device);
    if (!persistentNet) {
      virReportError(VIR_ERR_INVALID_ARG,
                     _("invalid interface name in persistent config: %1$s"),
                     device);
      goto cleanup;
    }

    if (!persistentNet->bandwidth)
      persistentNet->bandwidth = g_steal_pointer(&bandwidth);
    else {
      if (bandwidth->in) {
        VIR_FREE(persistentNet->bandwidth->in);
        persistentNet->bandwidth->in = g_steal_pointer(&bandwidth->in);
      } else if (inboundSpecified) {
        VIR_FREE(persistentNet->bandwidth->in);
      }
      if (bandwidth->out) {
        VIR_FREE(persistentNet->bandwidth->out);
        persistentNet->bandwidth->out = g_steal_pointer(&bandwidth->out);
      } else if (outboundSpecified) {
        VIR_FREE(persistentNet->bandwidth->out);
      }
    }
  }

  ret = 0;

cleanup:
  if (bandwidth) {
    virNetDevBandwidthFree(bandwidth);
  }
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainGetInterfaceParameters(virDomainPtr dom,
                                               const char *device,
                                               virTypedParameterPtr params,
                                               int *nparams,
                                               unsigned int flags) {
  virDomainObj *vm;
  virDomainDef *def;
  virDomainNetDef *net = NULL;
  virNetDevBandwidthRate in = {0};
  virNetDevBandwidthRate out = {0};
  int ret = -1;
  int i;

  virCheckFlags(VIR_DOMAIN_AFFECT_LIVE | VIR_DOMAIN_AFFECT_CONFIG, -1);

  if (*nparams == 0) {
    *nparams = 7;
    return 0;
  }

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainGetInterfaceParametersEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  if (!(def = virDomainObjGetOneDef(vm, flags)))
    goto cleanup;

  /* Find the network interface */
  net = virDomainNetFind(def, device);
  if (!net) {
    virReportError(VIR_ERR_INVALID_ARG, _("invalid interface name: %1$s"),
                   device);
    goto cleanup;
  }

  /* Get current bandwidth settings */
  if (net->bandwidth) {
    if (net->bandwidth->in)
      in = *net->bandwidth->in;
    if (net->bandwidth->out)
      out = *net->bandwidth->out;
  }

  /* Report bandwidth parameters */
  i = 0;
  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_BANDWIDTH_IN_AVERAGE,
                                VIR_TYPED_PARAM_UINT, in.average) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_BANDWIDTH_IN_PEAK,
                                VIR_TYPED_PARAM_UINT, in.peak) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_BANDWIDTH_IN_BURST,
                                VIR_TYPED_PARAM_UINT, in.burst) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_BANDWIDTH_IN_FLOOR,
                                VIR_TYPED_PARAM_UINT, in.floor) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_BANDWIDTH_OUT_AVERAGE,
                                VIR_TYPED_PARAM_UINT, out.average) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_BANDWIDTH_OUT_PEAK,
                                VIR_TYPED_PARAM_UINT, out.peak) < 0)
      goto cleanup;
    i++;
  }

  if (i < *nparams) {
    if (virTypedParameterAssign(&params[i], VIR_DOMAIN_BANDWIDTH_OUT_BURST,
                                VIR_TYPED_PARAM_UINT, out.burst) < 0)
      goto cleanup;
    i++;
  }

  *nparams = i;
  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

int macosvfDomainGetControlInfoFromObj(virDomainObj *vm,
                                       virDomainControlInfoPtr info) {
  int reason;

  /* Get domain state */
  virDomainObjGetState(vm, &reason);

  /* Control info is only meaningful for running domains */
  info->state = VIR_DOMAIN_CONTROL_OK;
  info->details = 0;
  info->stateTime = 0; /* No control state time tracking */

  return 0;
}

static int macosvfDomainGetControlInfo(virDomainPtr dom,
                                       virDomainControlInfoPtr info,
                                       unsigned int flags) {
  virDomainObj *vm;
  int ret = -1;

  virCheckFlags(0, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainGetControlInfoEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  ret = macosvfDomainGetControlInfoFromObj(vm, info);

cleanup:
  virObjectUnref(vm);
  return ret;
}

int macosvfDomainBlockStatsFromObj(virDomainObj *vm, const char *path,
                                   virDomainBlockStatsPtr stats) {
  virDomainDiskDef *disk;

  /* Initialize stats to zero */
  stats->rd_req = -1;   /* Not supported */
  stats->rd_bytes = -1; /* Not supported */
  stats->wr_req = -1;   /* Not supported */
  stats->wr_bytes = -1; /* Not supported */
  stats->errs = -1;     /* Not supported */

  /* Find the disk */
  disk = virDomainDiskByTarget(vm->def, path);
  if (!disk) {
    virReportError(VIR_ERR_INVALID_ARG, _("invalid path: %1$s"), path);
    return -1;
  }

  return 0;
}

static int macosvfDomainBlockStats(virDomainPtr dom, const char *path,
                                   virDomainBlockStatsPtr stats) {
  virDomainObj *vm;
  int ret = -1;

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainBlockStatsEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  ret = macosvfDomainBlockStatsFromObj(vm, path, stats);

cleanup:
  virObjectUnref(vm);
  return ret;
}

int macosvfDomainInterfaceStatsFromObj(virDomainObj *vm, const char *path,
                                       virDomainInterfaceStatsPtr stats) {
  virDomainNetDef *net;

  /* Initialize stats to zero */
  stats->rx_bytes = -1;   /* Not supported */
  stats->rx_packets = -1; /* Not supported */
  stats->rx_errs = -1;    /* Not supported */
  stats->rx_drop = -1;    /* Not supported */
  stats->tx_bytes = -1;   /* Not supported */
  stats->tx_packets = -1; /* Not supported */
  stats->tx_errs = -1;    /* Not supported */
  stats->tx_drop = -1;    /* Not supported */

  /* Find the network interface */
  net = virDomainNetFind(vm->def, path);
  if (!net) {
    virReportError(VIR_ERR_INVALID_ARG, _("invalid path: %1$s"), path);
    return -1;
  }

  return 0;
}

static int macosvfDomainInterfaceStats(virDomainPtr dom, const char *path,
                                       virDomainInterfaceStatsPtr stats) {
  virDomainObj *vm;
  int ret = -1;

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainInterfaceStatsEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  ret = macosvfDomainInterfaceStatsFromObj(vm, path, stats);

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainShutdownFlags(virDomainPtr dom, unsigned int flags) {
  virDomainObj *vm;
  macosvfDomainObjPrivate *priv;
  int ret = -1;

  virCheckFlags(0, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainShutdownFlagsEnsureACL(dom->conn, vm->def, flags) < 0)
    goto cleanup;

  priv = vm->privateData;
  if (!priv || !priv->vm) {
    virReportError(VIR_ERR_OPERATION_INVALID, "%s", _("domain is not running"));
    goto cleanup;
  }

  ret = macosvfVMStop((macosvfVMObject *)priv->vm, false);

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainShutdown(virDomainPtr dom) {
  return macosvfDomainShutdownFlags(dom, 0);
}

static int macosvfDomainDestroyFlags(virDomainPtr dom, unsigned int flags) {
  virDomainObj *vm;
  macosvfDomainObjPrivate *priv;
  int ret = -1;

  virCheckFlags(0, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainDestroyFlagsEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  priv = vm->privateData;
  if (!priv || !priv->vm) {
    virReportError(VIR_ERR_OPERATION_INVALID, "%s", _("domain is not running"));
    goto cleanup;
  }

  ret = macosvfVMStop((macosvfVMObject *)priv->vm, true);
  if (ret == 0) {
    virDomainObjSetState(vm, VIR_DOMAIN_SHUTOFF, VIR_DOMAIN_SHUTOFF_DESTROYED);
  }

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainDestroy(virDomainPtr dom) {
  return macosvfDomainDestroyFlags(dom, 0);
}

static int macosvfDomainSuspend(virDomainPtr dom) {
  virDomainObj *vm;
  macosvfDomainObjPrivate *priv;
  int ret = -1;

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainSuspendEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  priv = vm->privateData;
  if (!priv || !priv->vm) {
    virReportError(VIR_ERR_OPERATION_INVALID, "%s", _("domain is not running"));
    goto cleanup;
  }

  ret = macosvfVMPause((macosvfVMObject *)priv->vm);
  if (ret == 0) {
    virDomainObjSetState(vm, VIR_DOMAIN_PAUSED, VIR_DOMAIN_PAUSED_USER);
  }

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainResume(virDomainPtr dom) {
  virDomainObj *vm;
  macosvfDomainObjPrivate *priv;
  int ret = -1;

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainResumeEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  priv = vm->privateData;
  if (!priv || !priv->vm) {
    virReportError(VIR_ERR_OPERATION_INVALID, "%s", _("domain is not paused"));
    goto cleanup;
  }

  ret = macosvfVMResume((macosvfVMObject *)priv->vm);
  if (ret == 0) {
    virDomainObjSetState(vm, VIR_DOMAIN_RUNNING, VIR_DOMAIN_RUNNING_UNPAUSED);
  }

cleanup:
  virObjectUnref(vm);
  return ret;
}

static virDomainPtr macosvfDomainCreateXML(virConnectPtr conn, const char *xml,
                                           unsigned int flags) {
  macosvfConn *privconn = conn->privateData;
  virDomainPtr ret = NULL;
  g_autoptr(virDomainDef) def = NULL;
  virDomainObj *vm = NULL;
  macosvfDomainObjPrivate *priv;
  macosvfVMObject *vmobj = NULL;
  unsigned int parse_flags = VIR_DOMAIN_DEF_PARSE_INACTIVE;

  virCheckFlags(VIR_DOMAIN_START_VALIDATE | VIR_DOMAIN_START_PAUSED |
                    VIR_DOMAIN_START_AUTODESTROY,
                NULL);

  if (flags & VIR_DOMAIN_START_VALIDATE)
    parse_flags |= VIR_DOMAIN_DEF_PARSE_VALIDATE_SCHEMA;

  if (!(def =
            virDomainDefParseString(xml, privconn->xmlopt, NULL, parse_flags)))
    goto cleanup;

  if (virDomainCreateXMLEnsureACL(conn, def) < 0)
    goto cleanup;

  if (!(vm = virDomainObjListAdd(privconn->domains, &def, privconn->xmlopt, 0,
                                 NULL)))
    goto cleanup;

  priv = vm->privateData;

  /* Create the VM object */
  if (macosvfVMCreate(vm->def, &vmobj) < 0) {
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                   _("Failed to create VM object"));
    goto cleanup;
  }

  priv->vm = vmobj;

  /* Start the VM if not paused */
  if (!(flags & VIR_DOMAIN_START_PAUSED)) {
    if (macosvfVMStart(vmobj) < 0) {
      virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("Failed to start VM"));
      goto cleanup;
    }
    virDomainObjSetState(vm, VIR_DOMAIN_RUNNING, VIR_DOMAIN_RUNNING_BOOTED);
  } else {
    virDomainObjSetState(vm, VIR_DOMAIN_PAUSED, VIR_DOMAIN_PAUSED_USER);
  }

  ret = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);

cleanup:
  virDomainObjEndAPI(&vm);
  return ret;
}

static int macosvfDomainCreate(virDomainPtr dom) {
  virDomainObj *vm;
  macosvfDomainObjPrivate *priv;
  macosvfVMObject *vmobj = NULL;
  int ret = -1;

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainCreateEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  priv = vm->privateData;

  /* Create VM object if not exists */
  if (!priv->vm) {
    if (macosvfVMCreate(vm->def, &vmobj) < 0) {
      virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                     _("Failed to create VM object"));
      goto cleanup;
    }
    priv->vm = vmobj;
  }

  if (macosvfVMStart((macosvfVMObject *)priv->vm) < 0) {
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("Failed to start VM"));
    goto cleanup;
  }

  virDomainObjSetState(vm, VIR_DOMAIN_RUNNING, VIR_DOMAIN_RUNNING_BOOTED);
  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static char *macosvfDomainGetOSType(virDomainPtr dom) {
  virDomainObj *vm;
  char *ret = NULL;

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return NULL;

  if (virDomainGetOSTypeEnsureACL(dom->conn, vm->def) < 0) {
    virObjectUnref(vm);
    return NULL;
  }

  ret = g_strdup(virDomainOSTypeToString(vm->def->os.type));

  virObjectUnref(vm);
  return ret;
}

static unsigned long long macosvfDomainGetMaxMemory(virDomainPtr dom) {
  virDomainObj *vm;
  unsigned long long ret = 0;

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return 0;

  if (virDomainGetMaxMemoryEnsureACL(dom->conn, vm->def) < 0) {
    virObjectUnref(vm);
    return 0;
  }

  ret = virDomainDefGetMemoryTotal(vm->def);

  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainSetMemoryFlags(virDomainPtr dom,
                                       unsigned long memory G_GNUC_UNUSED,
                                       unsigned int flags) {
  virDomainObj *vm;
  int ret = -1;

  virCheckFlags(0, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainSetMemoryEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  /* Set memory will be implemented here */
  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainSetMemory(virDomainPtr dom, unsigned long memory) {
  return macosvfDomainSetMemoryFlags(dom, memory, 0);
}

static int macosvfConnectListDefinedDomains(virConnectPtr conn,
                                            char **const names, int maxnames) {
  macosvfConn *privconn = conn->privateData;
  int n;

  if (virConnectListDefinedDomainsEnsureACL(conn) < 0)
    return -1;

  n = virDomainObjListGetInactiveNames(privconn->domains, names, maxnames,
                                       virConnectListDefinedDomainsCheckACL,
                                       conn);

  return n;
}

static int macosvfConnectNumOfDefinedDomains(virConnectPtr conn) {
  macosvfConn *privconn = conn->privateData;
  int n;

  if (virConnectNumOfDefinedDomainsEnsureACL(conn) < 0)
    return -1;

  n = virDomainObjListNumOfDomains(privconn->domains, false,
                                   virConnectNumOfDefinedDomainsCheckACL, conn);

  return n;
}

static virDomainPtr macosvfDomainDefineXMLFlags(virConnectPtr conn,
                                                const char *xml,
                                                unsigned int flags) {
  macosvfConn *privconn = conn->privateData;
  virDomainPtr ret = NULL;
  g_autoptr(virDomainDef) def = NULL;
  virDomainObj *vm = NULL;
  unsigned int parse_flags = VIR_DOMAIN_DEF_PARSE_INACTIVE;

  virCheckFlags(VIR_DOMAIN_DEFINE_VALIDATE, NULL);

  if (flags & VIR_DOMAIN_DEFINE_VALIDATE)
    parse_flags |= VIR_DOMAIN_DEF_PARSE_VALIDATE_SCHEMA;

  if (!(def =
            virDomainDefParseString(xml, privconn->xmlopt, NULL, parse_flags)))
    goto cleanup;

  if (virDomainDefineXMLFlagsEnsureACL(conn, def) < 0)
    goto cleanup;

  if (!(vm = virDomainObjListAdd(privconn->domains, &def, privconn->xmlopt, 0,
                                 NULL)))
    goto cleanup;

  /* Save domain configuration */
  if (macosvfSaveDomainConfig(privconn, vm) < 0) {
    virDomainObjListRemove(privconn->domains, vm);
    goto cleanup;
  }

  ret = virGetDomain(conn, vm->def->name, vm->def->uuid, vm->def->id);

cleanup:
  virDomainObjEndAPI(&vm);
  return ret;
}

static virDomainPtr macosvfDomainDefineXML(virConnectPtr conn,
                                           const char *xml) {
  return macosvfDomainDefineXMLFlags(conn, xml, 0);
}

static int macosvfDomainUndefineFlags(virDomainPtr dom, unsigned int flags) {
  virDomainObj *vm;
  macosvfConn *privconn = dom->conn->privateData;
  int ret = -1;

  virCheckFlags(0, -1);

  if (!(vm = macosvfDomObjFromDomain(dom)))
    return -1;

  if (virDomainUndefineFlagsEnsureACL(dom->conn, vm->def) < 0)
    goto cleanup;

  if (virDomainObjIsActive(vm)) {
    virReportError(VIR_ERR_OPERATION_INVALID,
                   _("cannot undefine active domain '%1$s'"), vm->def->name);
    goto cleanup;
  }

  /* Delete domain configuration file */
  if (macosvfDeleteDomainConfig(privconn, vm) < 0)
    goto cleanup;

  virDomainObjListRemove(privconn->domains, vm);
  ret = 0;

cleanup:
  virObjectUnref(vm);
  return ret;
}

static int macosvfDomainUndefine(virDomainPtr dom) {
  return macosvfDomainUndefineFlags(dom, 0);
}

/* Connection handling */

static virDrvOpenStatus macosvfConnectOpen(virConnectPtr conn,
                                           virConnectAuthPtr auth G_GNUC_UNUSED,
                                           virConf *conf G_GNUC_UNUSED,
                                           unsigned int flags) {
  virCheckFlags(VIR_CONNECT_RO, VIR_DRV_OPEN_ERROR);

  VIR_WARN("macosvfConnectOpen called. URI=%s scheme=%s path=%s",
           conn->uri ? conn->uri->scheme : "null",
           conn->uri && conn->uri->scheme ? conn->uri->scheme : "null",
           conn->uri && conn->uri->path ? conn->uri->path : "null");

  /* Only support macosvf:/// URIs */
  if (conn->uri && STRNEQ(conn->uri->scheme, "macosvf") &&
      STRNEQ_NULLABLE(conn->uri->path, "/session") &&
      STRNEQ_NULLABLE(conn->uri->path, "/system"))
    return VIR_DRV_OPEN_DECLINED;

  if (macosvf_driver == NULL) {
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                   _("macosvf state driver is not active"));
    return VIR_DRV_OPEN_ERROR;
  }

  if (virConnectOpenEnsureACL(conn) < 0)
    return VIR_DRV_OPEN_ERROR;

  conn->privateData = macosvf_driver;

  return VIR_DRV_OPEN_SUCCESS;
}

static int macosvfConnectClose(virConnectPtr conn) {
  if (!conn->privateData)
    return 0;

  conn->privateData = NULL;
  return 0;
}

/* State driver functions */

static virDrvStateInitResult
macosvfStateInitialize(bool privileged, const char *root,
                       bool monolithic G_GNUC_UNUSED,
                       virStateInhibitCallback callback G_GNUC_UNUSED,
                       void *opaque G_GNUC_UNUSED) {
  macosvfConn *driver;
  char *configdir = NULL;
  char *rundir = NULL;

  VIR_WARN("macosvfStateInitialize called: privileged=%d", privileged);

  if (root != NULL) {
    virReportError(VIR_ERR_INVALID_ARG, "%s",
                   _("Driver does not support embedded mode"));
    return VIR_DRV_STATE_INIT_ERROR;
  }

  driver = g_new0(macosvfConn, 1);

  if (virMutexInit(&driver->lock) < 0) {
    VIR_FREE(driver);
    return VIR_DRV_STATE_INIT_ERROR;
  }

  if (!(driver->caps = macosvfCreateCapabilities()))
    goto error;

  if (!(driver->xmlopt = virDomainXMLOptionNew(
            NULL, &macosvfDriverPrivateDataCallbacks, NULL, NULL, NULL, NULL)))
    goto error;

  if (!(driver->domains = virDomainObjListNew()))
    goto error;

  if (!(driver->domainEventState = virObjectEventStateNew()))
    goto error;

  if (!(driver->config = macosvfDriverConfigNew()))
    goto error;

  /* Set up directories */
  if (privileged) {
    configdir = g_strdup(SYSCONFDIR "/libvirt/macosvf");
    rundir = g_strdup(RUNSTATEDIR "/libvirt/macosvf");
  } else {
    configdir = virGetUserConfigDirectory();
    rundir = virGetUserRuntimeDirectory();
  }

  driver->configDir = configdir;
  driver->stateDir = rundir;
  driver->privileged = privileged;

  /* Load configuration file */
  if (virFileExists(configdir)) {
    g_autofree char *configfile = NULL;
    configfile = g_strdup_printf("%s/macosvf.conf", configdir);
    if (configfile) {
      macosvfDriverLoadConfig(driver->config, configfile);
    }
  }

  /* Load existing domains from config directory */
  VIR_WARN("Checking for domains in config directory: %s", configdir);
  if (virFileExists(configdir)) {
    DIR *dir;
    struct dirent *entry;

    VIR_WARN("Config directory exists, attempting to open: %s", configdir);
    dir = opendir(configdir);
    if (dir) {
      VIR_WARN("Successfully opened config directory: %s", configdir);
      while ((entry = readdir(dir))) {
        char *suffix;

        /* Check if file ends with .xml */
        suffix = strstr(entry->d_name, ".xml");
        if (!suffix || strlen(suffix) != 4)
          continue;

        {
          g_autofree char *xmlFile = NULL;
          g_autofree char *xml = NULL;
          g_autoptr(virDomainDef) def = NULL;
          virDomainObj *vm = NULL;

          xmlFile = g_strdup_printf("%s/%s", configdir, entry->d_name);
          if (!xmlFile)
            continue;

          if (virFileReadAll(xmlFile, 10 * 1024 * 1024, &xml) < 0) {
            VIR_WARN("Failed to read file %s", xmlFile);
            continue;
          }

          VIR_WARN("Attempting to parse domain from %s", xmlFile);
          if (!(def = virDomainDefParseString(xml, driver->xmlopt, NULL,
                                              VIR_DOMAIN_DEF_PARSE_INACTIVE))) {
            VIR_WARN("Failed to parse domain definition from %s", xmlFile);
            continue;
          }

          if (!(vm = virDomainObjListAdd(driver->domains, &def, driver->xmlopt,
                                         VIR_DOMAIN_OBJ_LIST_ADD_LIVE |
                                         VIR_DOMAIN_OBJ_LIST_ADD_CHECK_LIVE,
                                         NULL)))
            continue;

          VIR_WARN("Loaded domain '%s' from %s", vm->def->name, xmlFile);
          virDomainObjEndAPI(&vm);
        }
      }
      closedir(dir);
    }
  }

  /* Start autostart domains if configured */
  if (driver->config->autoStart) {
    virDomainObjListForEach(driver->domains, false, macosvfAutostartDomain,
                            NULL);
  }

  macosvf_driver = driver;
  return VIR_DRV_STATE_INIT_COMPLETE;

error:
  virObjectUnref(driver->caps);
  virObjectUnref(driver->xmlopt);
  virObjectUnref(driver->domains);
  virObjectUnref(driver->domainEventState);
  virObjectUnref(driver->config);
  VIR_FREE(configdir);
  VIR_FREE(rundir);
  virMutexDestroy(&driver->lock);
  VIR_FREE(driver);
  return VIR_DRV_STATE_INIT_ERROR;
}

static int macosvfStateCleanup(void) {
  macosvfConn *driver = macosvf_driver;

  if (!driver)
    return 0;

  virObjectUnref(driver->domains);
  virObjectUnref(driver->domainEventState);
  virObjectUnref(driver->config);
  virObjectUnref(driver->caps);
  virObjectUnref(driver->xmlopt);

  VIR_FREE(driver->configDir);
  VIR_FREE(driver->stateDir);
  virMutexDestroy(&driver->lock);
  VIR_FREE(driver);

  macosvf_driver = NULL;
  return 0;
}

static int macosvfStateReload(void) {
  /* Reload will be implemented here */
  return 0;
}

static int macosvfStateStop(void) {
  /* Stop will be implemented here */
  return 0;
}

/* Driver structures */

static virHypervisorDriver macosvfHypervisorDriver = {
    .name = "macosvf",
    .connectOpen = macosvfConnectOpen,                       /* 10.10.0 */
    .connectClose = macosvfConnectClose,                     /* 10.10.0 */
    .connectGetType = macosvfConnectGetType,                 /* 10.10.0 */
    .connectGetVersion = macosvfConnectGetVersion,           /* 10.10.0 */
    .connectGetHostname = macosvfConnectGetHostname,         /* 10.10.0 */
    .connectGetMaxVcpus = macosvfConnectGetMaxVcpus,         /* 10.10.0 */
    .nodeGetInfo = macosvfNodeGetInfo,                       /* 10.10.0 */
    .connectGetCapabilities = macosvfConnectGetCapabilities, /* 10.10.0 */
    .connectListDomains = macosvfConnectListDomains,         /* 10.10.0 */
    .connectNumOfDomains = macosvfConnectNumOfDomains,       /* 10.10.0 */
    .connectListAllDomains = macosvfConnectListAllDomains,   /* 10.10.0 */
    .domainCreateXML = macosvfDomainCreateXML,               /* 10.10.0 */
    .domainLookupByID = macosvfDomainLookupByID,             /* 10.10.0 */
    .domainLookupByUUID = macosvfDomainLookupByUUID,         /* 10.10.0 */
    .domainLookupByName = macosvfDomainLookupByName,         /* 10.10.0 */
    .domainSuspend = macosvfDomainSuspend,                   /* 10.10.0 */
    .domainResume = macosvfDomainResume,                     /* 10.10.0 */
    .domainShutdown = macosvfDomainShutdown,                 /* 10.10.0 */
    .domainShutdownFlags = macosvfDomainShutdownFlags,       /* 10.10.0 */
    .domainDestroy = macosvfDomainDestroy,                   /* 10.10.0 */
    .domainDestroyFlags = macosvfDomainDestroyFlags,         /* 10.10.0 */
    .domainGetOSType = macosvfDomainGetOSType,               /* 10.10.0 */
    .domainGetMaxMemory = macosvfDomainGetMaxMemory,         /* 10.10.0 */
    .domainSetMemory = macosvfDomainSetMemory,               /* 10.10.0 */
    .domainGetInfo = macosvfDomainGetInfo,                   /* 10.10.0 */
    .domainGetState = macosvfDomainGetState,                 /* 10.10.0 */
    .domainGetXMLDesc = macosvfDomainGetXMLDesc,             /* 10.10.0 */
    .domainGetControlInfo = macosvfDomainGetControlInfo,     /* 10.10.0 */
    .domainBlockStats = macosvfDomainBlockStats,             /* 10.10.0 */
    .domainInterfaceStats = macosvfDomainInterfaceStats,     /* 10.10.0 */
    .domainMemoryStats = macosvfDomainMemoryStats,           /* 10.10.0 */
    .domainGetSchedulerType = macosvfDomainGetSchedulerType, /* 10.10.0 */
    .domainGetSchedulerParameters =
        macosvfDomainGetSchedulerParameters, /* 10.10.0 */
    .domainGetSchedulerParametersFlags =
        macosvfDomainGetSchedulerParametersFlags, /* 10.10.0 */
    .domainSetSchedulerParameters =
        macosvfDomainSetSchedulerParameters, /* 10.10.0 */
    .domainSetSchedulerParametersFlags =
        macosvfDomainSetSchedulerParametersFlags,                  /* 10.10.0 */
    .domainSetBlockIoTune = macosvfDomainSetBlockIoTune,           /* 10.10.0 */
    .domainGetBlockIoTune = macosvfDomainGetBlockIoTune,           /* 10.10.0 */
    .domainSetMemoryParameters = macosvfDomainSetMemoryParameters, /* 10.10.0 */
    .domainGetMemoryParameters = macosvfDomainGetMemoryParameters, /* 10.10.0 */
    .domainSetNumaParameters = macosvfDomainSetNumaParameters,     /* 10.10.0 */
    .domainGetNumaParameters = macosvfDomainGetNumaParameters,     /* 10.10.0 */
    .domainSetInterfaceParameters =
        macosvfDomainSetInterfaceParameters, /* 10.10.0 */
    .domainGetInterfaceParameters =
        macosvfDomainGetInterfaceParameters,                       /* 10.10.0 */
    .connectListDefinedDomains = macosvfConnectListDefinedDomains, /* 10.10.0 */
    .connectNumOfDefinedDomains =
        macosvfConnectNumOfDefinedDomains,               /* 10.10.0 */
    .domainCreate = macosvfDomainCreate,                 /* 10.10.0 */
    .domainDefineXML = macosvfDomainDefineXML,           /* 10.10.0 */
    .domainDefineXMLFlags = macosvfDomainDefineXMLFlags, /* 10.10.0 */
    .domainUndefine = macosvfDomainUndefine,             /* 10.10.0 */
    .domainUndefineFlags = macosvfDomainUndefineFlags,   /* 10.10.0 */
};

static virConnectDriver macosvfConnectDriver = {
    .localOnly = true,
    .uriSchemes = (const char *[]){"macosvf", NULL},
    .hypervisorDriver = &macosvfHypervisorDriver,
};

static virStateDriver macosvfStateDriver = {
    .name = "macosvf",
    .stateInitialize = macosvfStateInitialize,
    .stateCleanup = macosvfStateCleanup,
    .stateReload = macosvfStateReload,
    .stateStop = macosvfStateStop,
};

int macosvfRegister(void) {
  VIR_WARN("macosvfRegister called");
  if (virRegisterConnectDriver(&macosvfConnectDriver, true) < 0)
    return -1;
  if (virRegisterStateDriver(&macosvfStateDriver) < 0)
    return -1;
  return 0;
}

int virDriverInitialize(void);
int virDriverInitialize(void) { return macosvfRegister(); }
