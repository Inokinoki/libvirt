/*
 * macosvf_vm.c: Bridge to virtualization.framework (Objective-C)
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

#import <Foundation/Foundation.h>
#import <Virtualization/Virtualization.h>

#include "domain_conf.h"
#include "macosvf_domain.h"
#include "macosvf_vm.h"
#include "viralloc.h"
#include "virerror.h"
#include "virlog.h"
#include "virstring.h"
#include "virtime.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macOSvf_vm");

/* Internal structure to hold Objective-C objects */
struct _macosvfVMObject {
  VZVirtualMachine *vm;
  VZVirtualMachineConfiguration *config;
  virDomainDef *domainDef;
  macosvfVMState state;
  bool hasValidConfiguration;
  dispatch_queue_t queue; /* Queue for VM operations */

  /* Statistics tracking */
  uint64_t cpuTimeAccumulated; /* Accumulated CPU time in nanoseconds */
  uint64_t startTime;          /* VM start time in nanoseconds */
  uint64_t pauseTime;          /* Time when VM was paused in nanoseconds */
  bool isPaused;               /* Whether VM is currently paused */
};

/* Helper to get dispatch queue - create a dedicated queue for THIS VM */
static dispatch_queue_t macosvfGetQueue(macosvfVMObject *vm) {
  if (!vm->queue) {
    /* Create a dedicated serial queue for this VM's operations */
    vm->queue = dispatch_queue_create("org.libvirt.macosvf.vm", DISPATCH_QUEUE_SERIAL);
  }
  return vm->queue;
}

/* Helper to get current time in nanoseconds */
static uint64_t macosvfGetTimeNs(void) {
  unsigned long long milliseconds;

  /* Use libvirt's time function which is async-signal safe */
  if (virTimeMillisNow(&milliseconds) < 0) {
    /* Fallback to current time if virTimeMillisNow fails */
    return 0;
  }

  /* Convert milliseconds to nanoseconds */
  return milliseconds * 1000000ULL;
}

/* Helper to update accumulated CPU time */
static void macosvfUpdateCPUStats(macosvfVMObject *vm) {
  if (!vm)
    return;

  if (!vm->isPaused && vm->startTime > 0) {
    /* VM is running, add time since start */
    uint64_t currentTime = macosvfGetTimeNs();
    uint64_t elapsed = currentTime - vm->startTime;
    unsigned int vcpus = virDomainDefGetVcpus(vm->domainDef);
    vm->cpuTimeAccumulated += elapsed * vcpus;
    vm->startTime = currentTime;
  }
}

/* VM creation */
int macosvfVMCreate(virDomainDef *def, macosvfVMObject **vmptr) {
  macosvfVMObject *vm = NULL;
  VZVirtualMachineConfiguration *config = nil;
  unsigned long memorySize;
  NSError *error = nil;

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
  vm->hasValidConfiguration = false;
  vm->queue = macosvfGetQueue(vm);  /* Create dedicated queue for this VM */
  vm->cpuTimeAccumulated = 0;
  vm->startTime = 0;
  vm->pauseTime = 0;
  vm->isPaused = false;

  @autoreleasepool {
    unsigned int vcpus;
    config = [[VZVirtualMachineConfiguration alloc] init];

    /* Validate CPU count */
    vcpus = virDomainDefGetVcpusMax(def);
    if (vcpus == 0) {
      VIR_DEBUG("Defaulting vCPU count to 1");
      virDomainDefSetVcpusMax(def, 1, NULL);
      virDomainDefSetVcpus(def, 1);
      vcpus = 1;
    }

    if (vcpus > 16) {
      virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                     _("Invalid CPU count '%1$u', must be between 1 and 16"),
                     vcpus);
      g_free(vm);
      return -1;
    }

    /* Validate CPU mode and model - Virtualization.Framework restriction */
    if (def->cpu && def->cpu->mode != VIR_CPU_MODE_HOST_MODEL &&
        def->cpu->mode != VIR_CPU_MODE_HOST_PASSTHROUGH) {
      virReportError(
          VIR_ERR_CONFIG_UNSUPPORTED,
          _("CPU mode '%1$s' is not supported by macOS "
            "Virtualization.Framework. "
            "Only 'host-model' and 'host-passthrough' are supported."),
          virCPUModeTypeToString(def->cpu->mode));
      g_free(vm);
      return -1;
    }

    config.CPUCount = vcpus;

    /* Set memory (convert from KiB to bytes) */
    memorySize = virDomainDefGetMemoryTotal(def);
    config.memorySize = memorySize * 1024;

    /* Set up bootloader for Linux */
    if (def->os.type == VIR_DOMAIN_OSTYPE_HVM) {
      if (def->os.kernel) {
        NSString *kernelPath = [NSString stringWithUTF8String:def->os.kernel];
        NSURL *kernelURL = [NSURL fileURLWithPath:kernelPath];

        if ([[NSFileManager defaultManager] fileExistsAtPath:kernelPath]) {
          VZLinuxBootLoader *bootLoader =
              [[VZLinuxBootLoader alloc] initWithKernelURL:kernelURL];

          if (def->os.initrd) {
            NSString *initrdPath =
                [NSString stringWithUTF8String:def->os.initrd];
            if ([[NSFileManager defaultManager] fileExistsAtPath:initrdPath]) {
              NSURL *initrdURL = [NSURL fileURLWithPath:initrdPath];
              bootLoader.initialRamdiskURL = initrdURL;
            }
          }

          if (def->os.cmdline) {
            NSString *cmdline = [NSString stringWithUTF8String:def->os.cmdline];
            bootLoader.commandLine = cmdline;
          }

          config.bootLoader = bootLoader;
        } else {
          virReportError(VIR_ERR_INTERNAL_ERROR,
                         _("Kernel file not found: %1$s"), def->os.kernel);
          VIR_FREE(vm);
          return -1;
        }
      }
    }

    /* Set up storage devices */
    if (macosvfVMSetupStorage(def, vm) < 0) {
      VIR_FREE(vm);
      return -1;
    }

    /* Set up network devices */
    if (macosvfVMSetupNetwork(def, vm) < 0) {
      VIR_FREE(vm);
      return -1;
    }

    /* Set up serial console */
    if (macosvfVMSetupSerial(def, vm) < 0) {
      VIR_FREE(vm);
      return -1;
    }

    /* Set up graphics */
    if (macosvfVMSetupGraphics(def, vm) < 0) {
      VIR_FREE(vm);
      return -1;
    }

    /* Set up input devices */
    if (macosvfVMSetupInput(def, vm) < 0) {
      VIR_FREE(vm);
      return -1;
    }

    /* Set up audio */
    if (macosvfVMSetupAudio(def, vm) < 0) {
      VIR_FREE(vm);
      return -1;
    }

    /* Validate configuration */
    if (![config validateWithError:&error]) {
      VIR_DEBUG("VM configuration validation failed: %s",
                [[error localizedDescription] UTF8String]);
      /* Don't fail here for tests if we just want the object */
      vm->hasValidConfiguration = NO;
    } else {
      vm->hasValidConfiguration = YES;
    }

    vm->config = config;

    /* Create VM instance if configuration is valid
     * Use the VM's dedicated queue for all operations and completion handlers.
     * This ensures thread safety and proper synchronization. */
    if (vm->hasValidConfiguration) {
      vm->vm = [[VZVirtualMachine alloc] initWithConfiguration:config
                                                         queue:vm->queue];
    }
  }

  *vmptr = vm;
  return 0;
}

/* VM start */
int macosvfVMStart(macosvfVMObject *vm) {
  __block bool startSuccess = false;
  __block NSError *startError = nil;
  dispatch_semaphore_t sem;
  dispatch_time_t timeout;

  if (!vm || !vm->vm || !vm->hasValidConfiguration) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  /* Validate that the VM has a boot method configured */
  if (!vm->domainDef->os.kernel) {
    bool hasBootableDisk = false;

    /* Check if any disk is configured for boot */
    for (size_t i = 0; i < vm->domainDef->ndisks; i++) {
      virDomainDiskDef *disk = vm->domainDef->disks[i];
      if (disk && disk->src && disk->src->path) {
        hasBootableDisk = true;
        break;
      }
    }

    if (!hasBootableDisk) {
      virReportError(
          VIR_ERR_CONFIG_UNSUPPORTED, "%s",
          _("Cannot start VM: No boot configuration found. "
            "The VM must have either a kernel specified or at least one "
            "bootable disk. "
            "Add <kernel>/path/to/kernel</kernel> in <os> section or attach a "
            "bootable disk."));
      return -1;
    }
  }

  VIR_DEBUG("macosvfVMStart: Starting VM, hasValidConfiguration=%d",
            vm->hasValidConfiguration);

  @autoreleasepool {
    VIR_DEBUG("macosvfVMStart: VM object: %p, queue: %p", vm->vm, vm->queue);

    sem = dispatch_semaphore_create(0);

    VIR_DEBUG("macosvfVMStart: Dispatching start async to VM queue");

    /* Dispatch start to the VM's queue asynchronously */
    dispatch_async(vm->queue, ^{
      VIR_DEBUG("macosvfVMStart: Calling startWithCompletionHandler on queue");
      [vm->vm startWithCompletionHandler:^(NSError *error) {
        VIR_DEBUG("macosvfVMStart: In completion handler, error=%s",
                  error ? [[error localizedDescription] UTF8String] : "none");
        if (error) {
          startError = error;
          vm->state = MACOSVF_VM_STATE_ERROR;
        } else {
          vm->state = MACOSVF_VM_STATE_RUNNING;
          vm->startTime = macosvfGetTimeNs();
          vm->isPaused = false;
          startSuccess = true;
        }
        VIR_DEBUG("macosvfVMStart: Signaling semaphore");
        dispatch_semaphore_signal(sem);
      }];
      VIR_DEBUG("macosvfVMStart: startWithCompletionHandler returned");
    });

    VIR_DEBUG("macosvfVMStart: Dispatch complete, waiting for completion");

    /* Wait for start to complete (with timeout) */
    timeout = dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC);
    VIR_DEBUG("macosvfVMStart: Waiting for completion with 30s timeout");

    if (dispatch_semaphore_wait(sem, timeout) != 0) {
      VIR_DEBUG("macosvfVMStart: Timeout occurred");
      virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("VM start timed out"));
      return -1;
    }

    VIR_DEBUG("macosvfVMStart: Semaphore wait completed, startSuccess=%d",
              startSuccess);

    if (!startSuccess) {
      virReportError(VIR_ERR_INTERNAL_ERROR, _("Failed to start VM: %1$s"),
                     [[startError localizedDescription] UTF8String]);
      return -1;
    }
  }

  VIR_DEBUG("macosvfVMStart: VM started successfully");
  return 0;
}

/* VM stop */
int macosvfVMStop(macosvfVMObject *vm, bool force) {
  __block bool stopSuccess = false;
  __block NSError *stopError = nil;
  dispatch_semaphore_t sem;
  dispatch_time_t timeout;

  if (!vm || !vm->vm) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  @autoreleasepool {
    if (force) {
      sem = dispatch_semaphore_create(0);

      [vm->vm stopWithCompletionHandler:^(NSError *error) {
        if (error) {
          stopError = error;
        } else {
          vm->state = MACOSVF_VM_STATE_STOPPED;
          stopSuccess = true;
        }
        dispatch_semaphore_signal(sem);
      }];

      /* Wait for stop to complete */
      timeout = dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC);
      if (dispatch_semaphore_wait(sem, timeout) != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("VM stop timed out"));
        return -1;
      }

      if (!stopSuccess) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Failed to stop VM: %1$s"),
                       [[stopError localizedDescription] UTF8String]);
        return -1;
      }
    } else {
      /* Request graceful shutdown using halt to guest */
      /* VZVirtualMachine doesn't have a direct "requestStop" method,
       * but we can check if the VM is running and return an appropriate
       * message. For proper graceful shutdown, we would need to use the VM's
       * serial console or network to send a shutdown command.
       * For now, we'll attempt to use stop() which may attempt graceful
       * shutdown depending on macOS version.
       */

      /* Check if VM can be stopped gracefully */
      if (@available(macOS 12.0, *)) {
        /* On macOS 12+, we can try to request a clean stop */
        sem = dispatch_semaphore_create(0);

        [vm->vm stopWithCompletionHandler:^(NSError *error) {
          if (error) {
            stopError = error;
          } else {
            vm->state = MACOSVF_VM_STATE_STOPPED;
            stopSuccess = true;
          }
          dispatch_semaphore_signal(sem);
        }];

        /* Wait longer for graceful shutdown (60 seconds) */
        timeout = dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC);
        if (dispatch_semaphore_wait(sem, timeout) != 0) {
          virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                         _("VM graceful shutdown timed out"));
          return -1;
        }

        if (!stopSuccess) {
          virReportError(VIR_ERR_INTERNAL_ERROR,
                         _("Failed to gracefully stop VM: %1$s"),
                         [[stopError localizedDescription] UTF8String]);
          return -1;
        }
      } else {
        /* On macOS 11, graceful shutdown through VM agent is not available */
        virReportError(
            VIR_ERR_OPERATION_FAILED, "%s",
            _("Graceful shutdown requires guest agent support. "
              "Use force destroy (virsh destroy) or ensure the guest "
              "supports ACPI power management."));
        return -1;
      }
    }
  }

  return 0;
}

/* VM pause */
int macosvfVMPause(macosvfVMObject *vm) {
  __block bool pauseSuccess = false;
  __block NSError *pauseError = nil;
  dispatch_semaphore_t sem;
  dispatch_time_t timeout;

  if (!vm || !vm->vm) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  @autoreleasepool {
    /* Check if VM is running */
    VZVirtualMachineState vzState = vm->vm.state;
    if (vzState != VZVirtualMachineStateRunning) {
      virReportError(VIR_ERR_OPERATION_INVALID,
                     _("Cannot pause VM in state '%1$ld'"), (long)vzState);
      return -1;
    }

    /* Check if pause is available (macOS 12.0+) */
    if (@available(macOS 12.0, *)) {
      sem = dispatch_semaphore_create(0);

      [vm->vm pauseWithCompletionHandler:^(NSError *error) {
        if (error) {
          pauseError = error;
          vm->state = MACOSVF_VM_STATE_ERROR;
        } else {
          vm->state = MACOSVF_VM_STATE_PAUSED;
          vm->pauseTime = macosvfGetTimeNs();
          vm->isPaused = true;
          pauseSuccess = true;
        }
        dispatch_semaphore_signal(sem);
      }];

      /* Wait for pause to complete */
      timeout = dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC);
      if (dispatch_semaphore_wait(sem, timeout) != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("VM pause timed out"));
        return -1;
      }

      if (!pauseSuccess) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Failed to pause VM: %1$s"),
                       [[pauseError localizedDescription] UTF8String]);
        return -1;
      }
    } else {
      virReportError(VIR_ERR_NO_SUPPORT, "%s",
                     _("Pausing VMs requires macOS 12.0 (Monterey) or later"));
      return -1;
    }
  }

  return 0;
}

/* VM resume */
int macosvfVMResume(macosvfVMObject *vm) {
  __block bool resumeSuccess = false;
  __block NSError *resumeError = nil;
  dispatch_semaphore_t sem;
  dispatch_time_t timeout;

  if (!vm || !vm->vm) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  @autoreleasepool {
    /* Check if VM is paused */
    VZVirtualMachineState vzState = vm->vm.state;
    if (vzState != VZVirtualMachineStatePaused) {
      virReportError(VIR_ERR_OPERATION_INVALID,
                     _("Cannot resume VM in state '%1$ld'"), (long)vzState);
      return -1;
    }

    /* Check if resume is available (macOS 12.0+) */
    if (@available(macOS 12.0, *)) {
      sem = dispatch_semaphore_create(0);

      [vm->vm resumeWithCompletionHandler:^(NSError *error) {
        uint64_t currentTime;
        uint64_t pauseDuration;

        if (error) {
          resumeError = error;
          vm->state = MACOSVF_VM_STATE_ERROR;
        } else {
          vm->state = MACOSVF_VM_STATE_RUNNING;
          /* Update start time to resume time, accounting for pause duration */
          currentTime = macosvfGetTimeNs();
          if (vm->isPaused && vm->pauseTime > 0) {
            /* VM was paused, adjust start time */
            pauseDuration = currentTime - vm->pauseTime;
            vm->startTime += pauseDuration;
          } else {
            vm->startTime = currentTime;
          }
          vm->pauseTime = 0;
          vm->isPaused = false;
          resumeSuccess = true;
        }
        dispatch_semaphore_signal(sem);
      }];

      /* Wait for resume to complete */
      timeout = dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC);
      if (dispatch_semaphore_wait(sem, timeout) != 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("VM resume timed out"));
        return -1;
      }

      if (!resumeSuccess) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Failed to resume VM: %1$s"),
                       [[resumeError localizedDescription] UTF8String]);
        return -1;
      }
    } else {
      virReportError(VIR_ERR_NO_SUPPORT, "%s",
                     _("Resuming VMs requires macOS 12.0 (Monterey) or later"));
      return -1;
    }
  }

  return 0;
}

/* Get VM state */
macosvfVMState macosvfVMGetState(macosvfVMObject *vm) {
  VZVirtualMachineState vzState;

  if (!vm)
    return MACOSVF_VM_STATE_ERROR;

  if (!vm->vm)
    return MACOSVF_VM_STATE_STOPPED;

  @autoreleasepool {
    vzState = vm->vm.state;
    switch (vzState) {
    case VZVirtualMachineStateStopped:
      vm->state = MACOSVF_VM_STATE_STOPPED;
      break;
    case VZVirtualMachineStateRunning:
      vm->state = MACOSVF_VM_STATE_RUNNING;
      break;
    case VZVirtualMachineStatePaused:
      vm->state = MACOSVF_VM_STATE_PAUSED;
      break;
    case VZVirtualMachineStateError:
    case VZVirtualMachineStateStarting:
    case VZVirtualMachineStatePausing:
    case VZVirtualMachineStateResuming:
    case VZVirtualMachineStateStopping:
    case VZVirtualMachineStateSaving:
    case VZVirtualMachineStateRestoring:
    default:
      vm->state = MACOSVF_VM_STATE_ERROR;
      break;
    }
  }

  return vm->state;
}

/* Free VM object */
void macosvfVMFree(macosvfVMObject *vm) {
  if (!vm)
    return;

  @autoreleasepool {
    vm->vm = nil;
    vm->config = nil;
  }

  VIR_FREE(vm);
}

/* Storage setup */
int macosvfVMSetupStorage(virDomainDef *def, macosvfVMObject *vm) {
  @autoreleasepool {
    NSMutableArray<VZStorageDeviceConfiguration *> *storageDevices =
        [NSMutableArray array];

    for (size_t i = 0; i < def->ndisks; i++) {
      virDomainDiskDef *disk = def->disks[i];
      const char *diskPath;
      NSString *nsDiskPath;
      NSURL *diskURL;
      NSError *error = nil;
      VZDiskImageStorageDeviceAttachment *attachment;
      VZVirtioBlockDeviceConfiguration *blockDevice;

      /* Check if disk source exists */
      if (!disk->src || !disk->src->path) {
        VIR_DEBUG("Disk source path is NULL, skipping attachment");
        continue;
      }

      diskPath = disk->src->path;
      nsDiskPath = [NSString stringWithUTF8String:diskPath];

      if (![[NSFileManager defaultManager] fileExistsAtPath:nsDiskPath]) {
        VIR_DEBUG("Disk image not found: %s, skipping", diskPath);
        continue;
      }

      /* Create disk image attachment */
      diskURL = [NSURL fileURLWithPath:nsDiskPath];
      attachment = [[VZDiskImageStorageDeviceAttachment alloc]
          initWithURL:diskURL
             readOnly:(disk->src->readonly)error:&error];

      if (!attachment) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to create disk attachment: %1$s"),
                       [[error localizedDescription] UTF8String]);
        return -1;
      }

      /* Create block device configuration */
      blockDevice = [[VZVirtioBlockDeviceConfiguration alloc]
          initWithAttachment:attachment];

      [storageDevices addObject:blockDevice];
    }

    if (storageDevices.count > 0) {
      vm->config.storageDevices = storageDevices;
    }
  }

  return 0;
}

/* Network setup */
int macosvfVMSetupNetwork(virDomainDef *def, macosvfVMObject *vm) {
  @autoreleasepool {
    NSMutableArray<VZNetworkDeviceConfiguration *> *networkDevices =
        [NSMutableArray array];
    NSArray<VZBridgedNetworkInterface *> *interfaces;
    VZBridgedNetworkDeviceAttachment *bridgeAttachment;
    VZNATNetworkDeviceAttachment *natAttachment;
    VZVirtioNetworkDeviceConfiguration *nic;
    VZNetworkDeviceConfiguration *netConfig;
    char macStr[VIR_MAC_STRING_BUFLEN];
    NSString *macAddr;
    VZMACAddress *address;

    for (size_t i = 0; i < def->nnets; i++) {
      virDomainNetDef *net = def->nets[i];

      netConfig = nil;

      if (net->type == VIR_DOMAIN_NET_TYPE_NETWORK ||
          net->type == VIR_DOMAIN_NET_TYPE_BRIDGE) {
        /* Use network interface bridge */
        interfaces = [VZBridgedNetworkInterface networkInterfaces];

        if (interfaces.count == 0) {
          VIR_WARN("No bridge interfaces available for bridged network");
          continue;
        }

        bridgeAttachment = [[VZBridgedNetworkDeviceAttachment alloc]
            initWithInterface:interfaces[0]];

        nic = [[VZVirtioNetworkDeviceConfiguration alloc] init];
        nic.attachment = bridgeAttachment;
        netConfig = nic;

      } else if (net->type == VIR_DOMAIN_NET_TYPE_USER) {
        /* Use NAT */
        natAttachment = [[VZNATNetworkDeviceAttachment alloc] init];

        nic = [[VZVirtioNetworkDeviceConfiguration alloc] init];
        nic.attachment = natAttachment;
        netConfig = nic;

      } else {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Unsupported network type: %1$d"), net->type);
        return -1;
      }

      /* Set MAC address */
      virMacAddrFormat(&net->mac, macStr);
      macAddr = [NSString stringWithUTF8String:macStr];
      address = [[VZMACAddress alloc] initWithString:macAddr];
      if (address) {
        netConfig.MACAddress = address;
      }

      [networkDevices addObject:netConfig];
    }

    if (networkDevices.count > 0) {
      vm->config.networkDevices = networkDevices;
    }
  }

  return 0;
}

/* Serial console setup */
int macosvfVMSetupSerial(virDomainDef *def, macosvfVMObject *vm) {
  @autoreleasepool {
    NSMutableArray<VZSerialPortConfiguration *> *serialPorts =
        [NSMutableArray array];
    NSFileHandle *readHandle;
    NSFileHandle *writeHandle;
    VZFileHandleSerialPortAttachment *attachment;
    VZVirtioConsoleDeviceSerialPortConfiguration *console;

    for (size_t i = 0; i < def->nserials; i++) {
      virDomainChrDef *serial = def->serials[i];
      const char *path;
      char *dir;
      int ptmfd, fd;

      if (serial->source->type == VIR_DOMAIN_CHR_TYPE_PTY) {
        /* Create PTY for serial port */
        ptmfd = posix_openpt(O_RDWR | O_NOCTTY);
        if (ptmfd < 0) {
          virReportSystemError(errno, "%s", _("Failed to create PTY"));
          return -1;
        }
        grantpt(ptmfd);
        unlockpt(ptmfd);

        readHandle = [[NSFileHandle alloc] initWithFileDescriptor:ptmfd];
        writeHandle = [[NSFileHandle alloc] initWithFileDescriptor:dup(ptmfd)];
        close(ptmfd);

        attachment = [[VZFileHandleSerialPortAttachment alloc]
            initWithFileHandleForReading:readHandle
                    fileHandleForWriting:writeHandle];

      } else if (serial->source->type == VIR_DOMAIN_CHR_TYPE_FILE) {
        /* File-based serial port */
        path = serial->source->data.file.path;

        /* Ensure parent directory exists */
        dir = g_path_get_dirname(path);
        if (g_mkdir_with_parents(dir, 0700) < 0) {
          virReportSystemError(errno, _("Failed to create directory %1$s"),
                               dir);
          VIR_FREE(dir);
          return -1;
        }
        VIR_FREE(dir);

        fd = open(path, O_RDWR | O_CREAT | O_APPEND, 0600);
        if (fd < 0) {
          virReportSystemError(errno, _("Failed to open serial file %1$s"),
                               path);
          return -1;
        }

        readHandle = [[NSFileHandle alloc] initWithFileDescriptor:fd];
        writeHandle = [[NSFileHandle alloc] initWithFileDescriptor:dup(fd)];
        close(fd);

        attachment = [[VZFileHandleSerialPortAttachment alloc]
            initWithFileHandleForReading:readHandle
                    fileHandleForWriting:writeHandle];

      } else if (serial->source->type == VIR_DOMAIN_CHR_TYPE_NULL) {
        /* Null device - skip */
        continue;
      } else {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Unsupported serial type: %1$d"),
                       serial->source->type);
        return -1;
      }

      if (attachment) {
        console = [[VZVirtioConsoleDeviceSerialPortConfiguration alloc] init];
        console.attachment = attachment;

        [serialPorts addObject:console];
      }
    }

    if (serialPorts.count > 0) {
      vm->config.serialPorts = serialPorts;
    }
  }

  return 0;
}

/* Console setup (maps to first serial port) */
int macosvfVMSetupConsole(virDomainDef *def, macosvfVMObject *vm) {
  /* Console is just the first serial port in our implementation */
  return macosvfVMSetupSerial(def, vm);
}

/* Graphics setup */
int macosvfVMSetupGraphics(virDomainDef *def,
                           macosvfVMObject *vm G_GNUC_UNUSED) {
  @autoreleasepool {
    /* Check if any video devices are defined */
    if (def->nvideos == 0) {
      /* No video device, skip graphics setup */
      return 0;
    }

    /* macOS Virtualization.Framework supports graphics devices
     * Graphics are automatically enabled when input devices are configured.
     * The presence of a video device in the domain definition indicates
     * that graphics support should be enabled.
     *
     * Note: On macOS 12+, VZVirtualMachineConfiguration.graphicsDevices
     * can be set, but the framework also automatically provides graphics
     * capabilities when keyboard/pointing devices are present.
     */

    /* Graphics support is implicitly enabled by the presence of input devices.
     * No explicit configuration needed here - the framework handles it. */
  }

  return 0;
}

/* Input setup */
int macosvfVMSetupInput(virDomainDef *def, macosvfVMObject *vm) {
  NSMutableArray<VZPointingDeviceConfiguration *> *pointingDevices = nil;
  NSMutableArray<VZKeyboardConfiguration *> *keyboards = nil;
  VZUSBKeyboardConfiguration *keyboardConfig = nil;
  VZUSBScreenCoordinatePointingDeviceConfiguration *pointingConfig = nil;
  bool hasKeyboard = false;
  bool hasPointing = false;
  size_t i;

  @autoreleasepool {
    /* Check for input devices */
    for (i = 0; i < def->ninputs; i++) {
      virDomainInputDef *input = def->inputs[i];

      switch (input->type) {
      case VIR_DOMAIN_INPUT_TYPE_KBD:
        hasKeyboard = true;
        break;

      case VIR_DOMAIN_INPUT_TYPE_MOUSE:
      case VIR_DOMAIN_INPUT_TYPE_TABLET:
        hasPointing = true;
        break;

      default:
        break;
      }
    }

    /* Also check for video devices - if present, add default input devices */
    if (def->nvideos > 0) {
      /* When graphics is enabled, add both keyboard and pointing device by
       * default */
      hasKeyboard = true;
      hasPointing = true;
    }

    if (hasKeyboard) {
      if (@available(macOS 11.0, *)) {
        keyboards = [NSMutableArray array];
        keyboardConfig = [[VZUSBKeyboardConfiguration alloc] init];
        [keyboards addObject:keyboardConfig];
      }
    }

    if (hasPointing) {
      if (@available(macOS 11.0, *)) {
        pointingDevices = [NSMutableArray array];
        pointingConfig =
            [[VZUSBScreenCoordinatePointingDeviceConfiguration alloc] init];
        [pointingDevices addObject:pointingConfig];
      }
    }

    /* Set the configurations on the VM */
    if (keyboards && keyboards.count > 0) {
      vm->config.keyboards = keyboards;
    }

    if (pointingDevices && pointingDevices.count > 0) {
      vm->config.pointingDevices = pointingDevices;
    }
  }

  return 0;
}

/* Audio setup */
int macosvfVMSetupAudio(virDomainDef *def, macosvfVMObject *vm) {
  @autoreleasepool {
    /* Check if any sound devices are defined */
    if (def->nsounds == 0) {
      /* No sound device, skip audio setup */
      return 0;
    }

    /* macOS Virtualization.Framework supports audio devices
     * through VZVirtioSoundDeviceConfiguration (macOS 12+)
     */

    if (@available(macOS 12.0, *)) {
      /* Create and configure audio device */
      VZVirtioSoundDeviceConfiguration *audioConfig =
          [[VZVirtioSoundDeviceConfiguration alloc] init];

      if (audioConfig) {
        vm->config.audioDevices = @[ audioConfig ];
      }
    }
  }

  return 0;
}

/* RNG (Random Number Generator) setup */
int macosvfVMSetupRNG(virDomainDef *def, macosvfVMObject *vm) {
  @autoreleasepool {
    /* Check if any RNG devices are defined */
    if (def->nrngs == 0) {
      /* No RNG device, skip setup */
      return 0;
    }

    /* macOS Virtualization.Framework supports virtio RNG devices
     * through VZVirtioEntropyDeviceConfiguration (macOS 12+)
     * This provides entropy from /dev/random to the guest VM
     */

    if (@available(macOS 12.0, *)) {
      /* Check for virtio RNG model */
      for (size_t i = 0; i < def->nrngs; i++) {
        virDomainRNGDef *rng = def->rngs[i];

        if (rng->model == VIR_DOMAIN_RNG_MODEL_VIRTIO) {
          VZVirtioEntropyDeviceConfiguration *rngConfig =
              [[VZVirtioEntropyDeviceConfiguration alloc] init];

          if (rngConfig) {
            NSMutableArray *entropyDevices = [NSMutableArray array];

            /* Add existing entropy devices if any */
            if (vm->config.entropyDevices) {
              [entropyDevices addObjectsFromArray:vm->config.entropyDevices];
            }

            /* Add the new RNG device */
            [entropyDevices addObject:rngConfig];

            vm->config.entropyDevices = [entropyDevices copy];

            VIR_DEBUG("Configured virtio RNG device for domain '%s'",
                     def->name);
          }
        }
      }
    } else {
      VIR_WARN("Virtio RNG requires macOS 12.0 or later, "
               "RNG device will not be available for domain '%s'",
               def->name);
    }
  }

  return 0;
}

/* CPU setup (already done in VMCreate) */
int macosvfVMSetupCPUs(virDomainDef *def G_GNUC_UNUSED,
                       macosvfVMObject *vm G_GNUC_UNUSED) {
  /* CPU count already set in VMCreate */
  return 0;
}

/* Memory setup (already done in VMCreate) */
int macosvfVMSetupMemory(virDomainDef *def G_GNUC_UNUSED,
                         macosvfVMObject *vm G_GNUC_UNUSED) {
  /* Memory already set in VMCreate */
  return 0;
}

/* Bootloader setup (already done in VMCreate) */
int macosvfVMSetupBootloader(virDomainDef *def G_GNUC_UNUSED,
                             macosvfVMObject *vm G_GNUC_UNUSED) {
  /* Bootloader already set in VMCreate */
  return 0;
}

/* Get CPU statistics */
int macosvfVMGetCPUStats(macosvfVMObject *vm, unsigned long long *cpuTime) {
  if (!vm) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid VM object"));
    return -1;
  }

  if (!cpuTime) {
    virReportError(VIR_ERR_INVALID_ARG, "%s", _("Invalid cpuTime pointer"));
    return -1;
  }

  if (!vm->vm) {
    *cpuTime = 0;
    return 0;
  }

  @autoreleasepool {
    /* Check if VM is running */
    VZVirtualMachineState vzState = vm->vm.state;
    if (vzState != VZVirtualMachineStateRunning) {
      /* VM not running, return accumulated CPU time */
      *cpuTime = vm->cpuTimeAccumulated / 1000; /* Convert ns to us */
      return 0;
    }

    /* Update and return CPU time */
    macosvfUpdateCPUStats(vm);
    *cpuTime = vm->cpuTimeAccumulated / 1000; /* Convert ns to us */
  }

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

  /* Available memory in KiB (configured memory, even if VM is not running) */
  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_AVAILABLE;
    stats[i].val = def ? def->mem.cur_balloon : 0;
    i++;
  }

  /* Actual balloon size (same as available for macosvf) */
  if (i < nr_stats) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON;
    stats[i].val = def ? def->mem.cur_balloon : 0;
    i++;
  }

  /* RSS only if VM is actually running (actual memory in use) */
  if (i < nr_stats && vm->vm) {
    stats[i].tag = VIR_DOMAIN_MEMORY_STAT_RSS;
    stats[i].val = def ? virDomainDefGetMemoryTotal(def) : 0;
    i++;
  }

  return i;
}
