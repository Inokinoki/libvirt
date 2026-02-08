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

#include "macosvf_vm.h"
#include "macosvf_domain.h"
#include "virerror.h"
#include "viralloc.h"
#include "virlog.h"
#include "virstring.h"
#include "virtime.h"
#include "domain_conf.h"

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
    uint64_t cpuTimeAccumulated;  /* Accumulated CPU time in nanoseconds */
    uint64_t startTime;           /* VM start time in nanoseconds */
    uint64_t pauseTime;           /* Time when VM was paused in nanoseconds */
    bool isPaused;                /* Whether VM is currently paused */
};

/* Helper to get dispatch queue */
static dispatch_queue_t
macosvfGetQueue(void)
{
    static dispatch_queue_t queue = NULL;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        queue = dispatch_queue_create("org.libvirt.macosvf", DISPATCH_QUEUE_SERIAL);
    });
    return queue;
}

/* Helper to get current time in nanoseconds */
static uint64_t
macosvfGetTimeNs(void)
{
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
static void
macosvfUpdateCPUStats(macosvfVMObject *vm)
{
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
int
macosvfVMCreate(virDomainDef *def,
                 macosvfVMObject **vmptr)
{
    macosvfVMObject *vm = NULL;
    VZVirtualMachineConfiguration *config = nil;
    unsigned long memorySize;
    NSError *error = nil;

    vm = g_new0(macosvfVMObject, 1);
    if (!vm)
        return -1;

    vm->domainDef = def;
    vm->state = MACOSVF_VM_STATE_STOPPED;
    vm->hasValidConfiguration = false;
    vm->queue = macosvfGetQueue();
    vm->cpuTimeAccumulated = 0;
    vm->startTime = 0;
    vm->pauseTime = 0;
    vm->isPaused = false;

    @autoreleasepool {
        config = [[VZVirtualMachineConfiguration alloc] init];

        /* Set CPU count */
        config.CPUCount = virDomainDefGetVcpus(def);

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
                        NSString *initrdPath = [NSString stringWithUTF8String:def->os.initrd];
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
                                   _("Kernel file not found: %1$s"),
                                   def->os.kernel);
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

        /* Validate configuration */
        if (![config validateWithError:&error]) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Invalid VM configuration: %1$s"),
                           [[error localizedDescription] UTF8String]);
            VIR_FREE(vm);
            return -1;
        }

        vm->config = config;
        vm->hasValidConfiguration = YES;

        /* Create VM instance */
        vm->vm = [[VZVirtualMachine alloc]
                    initWithConfiguration:config
                                 queue:vm->queue];
    }

    *vmptr = vm;
    return 0;
}

/* VM start */
int
macosvfVMStart(macosvfVMObject *vm)
{
    __block bool startSuccess = false;
    __block NSError *startError = nil;
    dispatch_semaphore_t sem;
    dispatch_time_t timeout;

    if (!vm || !vm->vm || !vm->hasValidConfiguration) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Invalid VM object"));
        return -1;
    }

    @autoreleasepool {
        sem = dispatch_semaphore_create(0);

        [vm->vm startWithCompletionHandler:^(NSError *error) {
            if (error) {
                startError = error;
                vm->state = MACOSVF_VM_STATE_ERROR;
            } else {
                vm->state = MACOSVF_VM_STATE_RUNNING;
                vm->startTime = macosvfGetTimeNs();
                vm->isPaused = false;
                startSuccess = true;
            }
            dispatch_semaphore_signal(sem);
        }];

        /* Wait for start to complete (with timeout) */
        timeout = dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC);
        if (dispatch_semaphore_wait(sem, timeout) != 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("VM start timed out"));
            return -1;
        }

        if (!startSuccess) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Failed to start VM: %1$s"),
                           [[startError localizedDescription] UTF8String]);
            return -1;
        }
    }

    return 0;
}

/* VM stop */
int
macosvfVMStop(macosvfVMObject *vm,
               bool force)
{
    __block bool stopSuccess = false;
    __block NSError *stopError = nil;
    dispatch_semaphore_t sem;
    dispatch_time_t timeout;

    if (!vm || !vm->vm) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Invalid VM object"));
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
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("VM stop timed out"));
                return -1;
            }

            if (!stopSuccess) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Failed to stop VM: %1$s"),
                               [[stopError localizedDescription] UTF8String]);
                return -1;
            }
        } else {
            /* Request graceful shutdown using halt to guest */
            /* VZVirtualMachine doesn't have a direct "requestStop" method,
             * but we can check if the VM is running and return an appropriate message.
             * For proper graceful shutdown, we would need to use the VM's
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
                virReportError(VIR_ERR_OPERATION_FAILED,
                               "%s", _("Graceful shutdown requires guest agent support. "
                                       "Use force destroy (virsh destroy) or ensure the guest "
                                       "supports ACPI power management."));
                return -1;
            }
        }
    }

    return 0;
}

/* VM pause */
int
macosvfVMPause(macosvfVMObject *vm)
{
    __block bool pauseSuccess = false;
    __block NSError *pauseError = nil;
    dispatch_semaphore_t sem;
    dispatch_time_t timeout;

    if (!vm || !vm->vm) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Invalid VM object"));
        return -1;
    }

    @autoreleasepool {
        /* Check if VM is running */
        VZVirtualMachineState vzState = vm->vm.state;
        if (vzState != VZVirtualMachineStateRunning) {
            virReportError(VIR_ERR_OPERATION_INVALID,
                           _("Cannot pause VM in state '%1$ld'"),
                           (long)vzState);
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
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("VM pause timed out"));
                return -1;
            }

            if (!pauseSuccess) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Failed to pause VM: %1$s"),
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
int
macosvfVMResume(macosvfVMObject *vm)
{
    __block bool resumeSuccess = false;
    __block NSError *resumeError = nil;
    dispatch_semaphore_t sem;
    dispatch_time_t timeout;

    if (!vm || !vm->vm) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Invalid VM object"));
        return -1;
    }

    @autoreleasepool {
        /* Check if VM is paused */
        VZVirtualMachineState vzState = vm->vm.state;
        if (vzState != VZVirtualMachineStatePaused) {
            virReportError(VIR_ERR_OPERATION_INVALID,
                           _("Cannot resume VM in state '%1$ld'"),
                           (long)vzState);
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
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("VM resume timed out"));
                return -1;
            }

            if (!resumeSuccess) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Failed to resume VM: %1$s"),
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
macosvfVMState
macosvfVMGetState(macosvfVMObject *vm)
{
    VZVirtualMachineState vzState;

    if (!vm || !vm->vm)
        return MACOSVF_VM_STATE_ERROR;

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
void
macosvfVMFree(macosvfVMObject *vm)
{
    if (!vm)
        return;

    @autoreleasepool {
        vm->vm = nil;
        vm->config = nil;
    }

    VIR_FREE(vm);
}

/* Storage setup */
int
macosvfVMSetupStorage(virDomainDef *def,
                       macosvfVMObject *vm)
{
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
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("Disk source path is NULL"));
                return -1;
            }

            diskPath = disk->src->path;
            nsDiskPath = [NSString stringWithUTF8String:diskPath];

            if (![[NSFileManager defaultManager] fileExistsAtPath:nsDiskPath]) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Disk image not found: %1$s"), diskPath);
                return -1;
            }

            /* Create disk image attachment */
            diskURL = [NSURL fileURLWithPath:nsDiskPath];
            attachment = [[VZDiskImageStorageDeviceAttachment alloc]
                initWithURL:diskURL
                     readOnly:(disk->src->readonly)
                       error:&error];

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
int
macosvfVMSetupNetwork(virDomainDef *def,
                       macosvfVMObject *vm)
{
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
                    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                                   _("No bridge interfaces available"));
                    return -1;
                }

                bridgeAttachment =
                    [[VZBridgedNetworkDeviceAttachment alloc]
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
int
macosvfVMSetupSerial(virDomainDef *def,
                      macosvfVMObject *vm)
{
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
                    virReportSystemError(errno, "%s",
                                           _("Failed to create PTY"));
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
                    virReportSystemError(errno,
                                           _("Failed to create directory %1$s"),
                                           dir);
                    VIR_FREE(dir);
                    return -1;
                }
                VIR_FREE(dir);

                fd = open(path, O_RDWR | O_CREAT | O_APPEND, 0600);
                if (fd < 0) {
                    virReportSystemError(errno,
                                           _("Failed to open serial file %1$s"),
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
                               _("Unsupported serial type: %1$d"), serial->source->type);
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
int
macosvfVMSetupConsole(virDomainDef *def,
                       macosvfVMObject *vm)
{
    /* Console is just the first serial port in our implementation */
    return macosvfVMSetupSerial(def, vm);
}

/* CPU setup (already done in VMCreate) */
int
macosvfVMSetupCPUs(virDomainDef *def G_GNUC_UNUSED,
                    macosvfVMObject *vm G_GNUC_UNUSED)
{
    /* CPU count already set in VMCreate */
    return 0;
}

/* Memory setup (already done in VMCreate) */
int
macosvfVMSetupMemory(virDomainDef *def G_GNUC_UNUSED,
                      macosvfVMObject *vm G_GNUC_UNUSED)
{
    /* Memory already set in VMCreate */
    return 0;
}

/* Bootloader setup (already done in VMCreate) */
int
macosvfVMSetupBootloader(virDomainDef *def G_GNUC_UNUSED,
                         macosvfVMObject *vm G_GNUC_UNUSED)
{
    /* Bootloader already set in VMCreate */
    return 0;
}

/* Get CPU statistics */
int
macosvfVMGetCPUStats(macosvfVMObject *vm,
                    unsigned long long *cpuTime)
{
    if (!vm || !vm->vm) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Invalid VM object"));
        return -1;
    }

    if (!cpuTime) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Invalid cpuTime pointer"));
        return -1;
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
int
macosvfVMGetMemoryStats(macosvfVMObject *vm,
                       unsigned long long *memoryUsed)
{
    if (!vm || !vm->vm) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Invalid VM object"));
        return -1;
    }

    if (!memoryUsed) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Invalid memoryUsed pointer"));
        return -1;
    }

    @autoreleasepool {
        /* Check if VM is running */
        VZVirtualMachineState vzState = vm->vm.state;
        if (vzState != VZVirtualMachineStateRunning) {
            /* VM not running, no memory used */
            *memoryUsed = 0;
            return 0;
        }

        /* macOS Virtualization.Framework doesn't directly expose memory usage.
         * The memory is allocated from the host, but we can't easily query
         * actual usage without potentially expensive operations.
         * For now, return the configured memory size.
         */
        if (vm->domainDef) {
            *memoryUsed = virDomainDefGetMemoryTotal(vm->domainDef);
        } else {
            *memoryUsed = 0;
        }

        /* TODO: Implement proper memory usage tracking using:
         * - VZVirtualMachine's memory accounting (if exposed in future macOS versions)
         * - task info APIs on the VM queue
         * - vm_stat or similar system utilities
         */
    }

    return 0;
}
