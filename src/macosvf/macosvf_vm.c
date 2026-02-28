/*
 * macosvf_vm.c: Bridge to virtualization.framework via XPC helper
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

#include <Foundation/Foundation.h>
#include <os/log.h>

#include "domain_conf.h"
#include "macosvf_domain.h"
#include "macosvf_helper_client.h"
#include "macosvf_vm.h"
#include "viralloc.h"
#include "virerror.h"
#include "virlog.h"
#include "virstring.h"
#include "virtime.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macosvf_vm");

/* Internal structure to hold VM state */
struct _macosvfVMObject {
  macosvfHelperClient *helper;
  char *vmId;
  virDomainDef *domainDef;
  macosvfVMState state;
  char *consolePath;
  int consoleMasterFd; /* Master FD for PTY console */

  /* Statistics tracking */
  uint64_t cpuTimeAccumulated;
  uint64_t startTime;
  uint64_t pauseTime;
  bool isPaused;
};

/* Helper to get current time in nanoseconds */
static uint64_t macosvfGetTimeNs(void) {
  unsigned long long milliseconds;

  if (virTimeMillisNow(&milliseconds) < 0) {
    return 0;
  }

  return milliseconds * 1000000ULL;
}

/* Helper to update accumulated CPU time */
static void macosvfUpdateCPUStats(macosvfVMObject *vm) {
  if (!vm)
    return;

  if (!vm->isPaused && vm->startTime > 0) {
    uint64_t currentTime = macosvfGetTimeNs();
    uint64_t elapsed = currentTime - vm->startTime;
    unsigned int vcpus = virDomainDefGetVcpus(vm->domainDef);
    vm->cpuTimeAccumulated += elapsed * vcpus;
    vm->startTime = currentTime;
  }
}

/* Generate a unique VM ID from domain UUID */
static char *macosvfGenerateVMId(const virDomainDef *def) {
  char uuidstr[VIR_UUID_STRING_BUFLEN];

  virUUIDFormat(def->uuid, uuidstr);
  return g_strdup_printf("libvirt-%s", uuidstr);
}

/* VM creation */
int macosvfVMCreate(virDomainDef *def, macosvfVMObject **vmptr) {
  macosvfVMObject *vm = NULL;
  macosvfHelperVMConfig config;
  int ptmfd = -1;
  char *ptspath = NULL;

  if (!vmptr)
    return -1;

  if (!def) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Domain definition is NULL"));
    return -1;
  }

  vm = g_new0(macosvfVMObject, 1);
  if (!vm)
    return -1;

  vm->domainDef = def;
  vm->state = MACOSVF_VM_STATE_STOPPED;
  vm->cpuTimeAccumulated = 0;
  vm->startTime = 0;
  vm->pauseTime = 0;
  vm->isPaused = false;
  vm->consolePath = NULL;
  vm->consoleMasterFd = -1;

  /* Generate VM ID from domain UUID */
  vm->vmId = macosvfGenerateVMId(def);
  if (!vm->vmId) {
    virReportError(VIR_ERR_NO_MEMORY, "%s", _("Failed to generate VM ID"));
    g_free(vm);
    return -1;
  }

  /* Get or create the helper client */
  vm->helper = macosvfHelperClientGet();
  if (!vm->helper) {
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                   _("Failed to get helper client"));
    VIR_FREE(vm->vmId);
    g_free(vm);
    return -1;
  }

  /* Initialize the helper if this is the first client */
  if (macosvfHelperClientInit(vm->helper) < 0) {
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                   _("Failed to initialize helper client"));
    VIR_FREE(vm->vmId);
    g_free(vm);
    return -1;
  }

  /* Create PTY for console if domain has serial console */
  if (def->nserials > 0) {
    virDomainChrDef *serial = def->serials[0];
    VIR_DEBUG("Found serial device: type=%d", serial->source->type);

    if (serial->source->type != VIR_DOMAIN_CHR_TYPE_PTY) {
      /* Set it to PTY if not already */
      serial->source->type = VIR_DOMAIN_CHR_TYPE_PTY;
    }

    ptmfd = posix_openpt(O_RDWR | O_NOCTTY);
    if (ptmfd < 0) {
      virReportSystemError(errno, "%s", _("Failed to create PTY"));
      macosvfVMFree(vm);
      return -1;
    }
    grantpt(ptmfd);
    unlockpt(ptmfd);

    ptspath = ptsname(ptmfd);
    if (!ptspath) {
      virReportSystemError(errno, "%s", _("Failed to get PTY path"));
      close(ptmfd);
      macosvfVMFree(vm);
      return -1;
    }

    /* Store PTY path in domain definition for both serial and console */
    def->serials[0]->source->data.file.path = g_strdup(ptspath);
    VIR_INFO("Stored PTY path '%s' in serial[0] for domain '%s'", ptspath,
             def->name);

    /* Also set console path if console device exists */
    if (def->nconsoles > 0 &&
        def->consoles[0]->source->type == VIR_DOMAIN_CHR_TYPE_PTY) {
      def->consoles[0]->source->data.file.path = g_strdup(ptspath);
      VIR_INFO("Stored PTY path '%s' in console[0] for domain '%s'", ptspath,
               def->name);
    }

    /* Also store for console access */
    vm->consolePath = g_strdup(ptspath);

    /* Store master FD for console access */
    vm->consoleMasterFd = ptmfd;

    VIR_INFO("Created PTY %s for domain '%s'", ptspath, def->name);
  }

  /* Build VM configuration */
  memset(&config, 0, sizeof(config));
  config.cpuCount = virDomainDefGetVcpusMax(def);
  if (config.cpuCount == 0)
    config.cpuCount = 1;

  config.memorySize =
      virDomainDefGetMemoryTotal(def) * 1024; /* Convert to bytes */
  config.useNetwork = 1;                      /* Enable network by default */

  /* Set kernel path if defined */
  if (def->os.kernel)
    config.kernelPath = def->os.kernel;

  /* Set initrd path if defined */
  if (def->os.initrd)
    config.initrdPath = def->os.initrd;

  /* Set command line if defined */
  if (def->os.cmdline)
    config.cmdline = def->os.cmdline;

  /* Set PTY path for console */
  if (ptspath)
    config.ptyPath = ptspath;

  /* Parse port forwarding from network interfaces */
  config.numPortForwards = 0;
  for (size_t i = 0;
       i < def->nnets && config.numPortForwards < MACOSVF_MAX_PORT_FORWARDS;
       i++) {
    virDomainNetDef *net = def->nets[i];
    if (net->type == VIR_DOMAIN_NET_TYPE_USER) {
      /* User mode networking - check for port forward rules */
      for (size_t j = 0; j < net->nPortForwards &&
                         config.numPortForwards < MACOSVF_MAX_PORT_FORWARDS;
           j++) {
        virDomainNetPortForward *pf = net->portForwards[j];
        /* Parse port ranges */
        for (size_t k = 0; k < pf->nRanges &&
                           config.numPortForwards < MACOSVF_MAX_PORT_FORWARDS;
             k++) {
          virDomainNetPortForwardRange *range = pf->ranges[k];
          config.portForwards[config.numPortForwards].hostPort = range->start;
          config.portForwards[config.numPortForwards].guestPort =
              range->to > 0 ? range->to : range->start;
          config.portForwards[config.numPortForwards].protocol =
              pf->proto == VIR_DOMAIN_NET_PROTO_UDP ? 1 : 0;
          config.numPortForwards++;
          VIR_DEBUG("Added port forward: host=%d guest=%d proto=%s",
                    range->start,
                    config.portForwards[config.numPortForwards - 1].guestPort,
                    pf->proto == VIR_DOMAIN_NET_PROTO_UDP ? "UDP" : "TCP");
        }
      }
    }
  }

  /* Add disk paths */
  config.numDisks = 0;
  for (size_t i = 0; i < def->ndisks && config.numDisks < MACOSVF_MAX_DISKS;
       i++) {
    virDomainDiskDef *disk = def->disks[i];
    if (disk->src && disk->src->path) {
      config.diskPaths[config.numDisks++] = disk->src->path;
    }
  }

  /* Check for desktop graphics */
  config.enableDisplay = false;
  for (size_t i = 0; i < def->ngraphics; i++) {
    if (def->graphics[i]->type == VIR_DOMAIN_GRAPHICS_TYPE_DESKTOP) {
      config.enableDisplay = true;
      break;
    }
  }

  /* Create VM via helper */
  if (macosvfHelperCreateVM(vm->helper, vm->vmId, &config) < 0) {
    VIR_ERROR("Failed to create VM '%s' via helper", vm->vmId);
    macosvfVMFree(vm);
    return -1;
  }

  VIR_DEBUG("VM '%s' created successfully via helper", vm->vmId);
  *vmptr = vm;
  return 0;
}

/* VM start */
int macosvfVMStart(macosvfVMObject *vm) {
  if (!vm || !vm->helper || !vm->vmId) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  VIR_DEBUG("Starting VM '%s'", vm->vmId);

  if (macosvfHelperStartVM(vm->helper, vm->vmId) < 0) {
    VIR_ERROR("Failed to start VM '%s'", vm->vmId);
    return -1;
  }

  vm->state = MACOSVF_VM_STATE_RUNNING;
  vm->startTime = macosvfGetTimeNs();
  vm->isPaused = false;

  VIR_DEBUG("VM '%s' started successfully", vm->vmId);
  return 0;
}

/* VM stop */
int macosvfVMStop(macosvfVMObject *vm, bool force ATTRIBUTE_UNUSED) {
  if (!vm || !vm->helper || !vm->vmId) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  VIR_DEBUG("Stopping VM '%s'", vm->vmId);

  if (macosvfHelperStopVM(vm->helper, vm->vmId) < 0) {
    VIR_ERROR("Failed to stop VM '%s'", vm->vmId);
    return -1;
  }

  vm->state = MACOSVF_VM_STATE_STOPPED;
  macosvfUpdateCPUStats(vm);

  VIR_DEBUG("VM '%s' stopped successfully", vm->vmId);
  return 0;
}

/* VM pause */
int macosvfVMPause(macosvfVMObject *vm) {
  if (!vm || !vm->helper || !vm->vmId) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  VIR_DEBUG("Pausing VM '%s'", vm->vmId);

  if (macosvfHelperPauseVM(vm->helper, vm->vmId) < 0) {
    VIR_ERROR("Failed to pause VM '%s'", vm->vmId);
    return -1;
  }

  vm->state = MACOSVF_VM_STATE_PAUSED;
  vm->isPaused = true;
  macosvfUpdateCPUStats(vm);

  VIR_DEBUG("VM '%s' paused successfully", vm->vmId);
  return 0;
}

/* VM resume */
int macosvfVMResume(macosvfVMObject *vm) {
  if (!vm || !vm->helper || !vm->vmId) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  VIR_DEBUG("Resuming VM '%s'", vm->vmId);

  if (macosvfHelperResumeVM(vm->helper, vm->vmId) < 0) {
    VIR_ERROR("Failed to resume VM '%s'", vm->vmId);
    return -1;
  }

  vm->state = MACOSVF_VM_STATE_RUNNING;
  vm->isPaused = false;
  vm->startTime = macosvfGetTimeNs();

  VIR_DEBUG("VM '%s' resumed successfully", vm->vmId);
  return 0;
}

/* Get VM state */
macosvfVMState macosvfVMGetState(macosvfVMObject *vm) {
  macosvfHelperVMState helperState;

  if (!vm || !vm->helper || !vm->vmId)
    return MACOSVF_VM_STATE_ERROR;

  if (macosvfHelperGetVMState(vm->helper, vm->vmId, &helperState) < 0) {
    return MACOSVF_VM_STATE_ERROR;
  }

  /* Translate helper state to our state */
  switch (helperState) {
  case MACOSVF_HELPER_VM_STATE_UNKNOWN:
  case MACOSVF_HELPER_VM_STATE_STOPPED:
    return MACOSVF_VM_STATE_STOPPED;
  case MACOSVF_HELPER_VM_STATE_RUNNING:
    return MACOSVF_VM_STATE_RUNNING;
  case MACOSVF_HELPER_VM_STATE_PAUSED:
    return MACOSVF_VM_STATE_PAUSED;
  case MACOSVF_HELPER_VM_STATE_ERROR:
    return MACOSVF_VM_STATE_ERROR;
  case MACOSVF_HELPER_VM_STATE_STARTING:
  case MACOSVF_HELPER_VM_STATE_STOPPING:
  default:
    return MACOSVF_VM_STATE_RUNNING; /* Transitional states */
  }
}

/* Free VM object */
void macosvfVMFree(macosvfVMObject *vm) {
  if (!vm)
    return;

  /* Destroy VM via helper if it exists */
  if (vm->helper && vm->vmId) {
    macosvfHelperDestroyVM(vm->helper, vm->vmId);
  }

  /* Release helper client */
  if (vm->helper) {
    macosvfHelperClientFree(vm->helper);
  }

  /* Close console master FD if open */
  if (vm->consoleMasterFd >= 0) {
    close(vm->consoleMasterFd);
  }

  VIR_FREE(vm->vmId);
  VIR_FREE(vm->consolePath);
  g_free(vm);
}

/* Get CPU statistics */
int macosvfVMGetCPUStats(macosvfVMObject *vm, unsigned long long *cpuTime) {
  macosvfHelperVMState state;

  if (!vm) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  if (!cpuTime) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid cpuTime pointer"));
    return -1;
  }

  if (!vm->helper || !vm->vmId) {
    *cpuTime = 0;
    return 0;
  }

  if (macosvfHelperGetVMState(vm->helper, vm->vmId, &state) < 0 ||
      state != MACOSVF_HELPER_VM_STATE_RUNNING) {
    *cpuTime = vm->cpuTimeAccumulated / 1000;
    return 0;
  }

  macosvfUpdateCPUStats(vm);
  *cpuTime = vm->cpuTimeAccumulated / 1000;
  return 0;
}

/* Get memory statistics */
int macosvfVMGetMemoryStats(macosvfVMObject *vm, virDomainMemoryStatPtr stats,
                            unsigned int nr_stats) {
  unsigned int i = 0;
  virDomainDef *def;

  if (!vm || !stats || nr_stats == 0)
    return 0;

  def = vm->domainDef;

  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_AVAILABLE;
    stats[i].val = def ? def->mem.cur_balloon : 0;
    i++;
  }

  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON;
    stats[i].val = def ? def->mem.cur_balloon : 0;
    i++;
  }

  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_RSS;
    stats[i].val = def ? virDomainDefGetMemoryTotal(def) : 0;
    i++;
  }

  return i;
}

/* Get console path */
int macosvfVMGetConsolePath(macosvfVMObject *vm, char **path) {
  if (!vm || !path)
    return -1;

  if (vm->consolePath) {
    *path = g_strdup(vm->consolePath);
    return 0;
  }

  if (vm->helper && vm->vmId) {
    return macosvfHelperGetConsolePath(vm->helper, vm->vmId, path);
  }

  return -1;
}

/* Get console master FD */
int macosvfVMGetConsoleMasterFd(macosvfVMObject *vm) {
  if (!vm)
    return -1;

  return vm->consoleMasterFd;
}
