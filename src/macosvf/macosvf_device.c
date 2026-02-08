/*
 * macosvf_device.c: Device management for macosvf driver
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

#include "macosvf_device.h"
#include "virlog.h"
#include "virerror.h"
#include "virstring.h"
#include "domain_conf.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macOSvf_device");

/* Validate disk device */
static int
macosvfDomainDiskDefValidate(const virDomainDiskDef *disk)
{
    /* macOS Virtualization.Framework supports:
     * - virtio disk bus
     * - file-based disk images
     * - raw and qcow2 formats
     * - disk and cdrom device types
     */
    if (disk->bus != VIR_DOMAIN_DISK_BUS_VIRTIO &&
        disk->bus != 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Disk bus '%1$s' is not supported by macOS Virtualization.Framework. "
                         "Only virtio disk bus is supported. "
                         "Remove the bus attribute or change to 'virtio'."),
                       virDomainDiskBusTypeToString(disk->bus));
        return -1;
    }

    if (disk->device != VIR_DOMAIN_DISK_DEVICE_DISK &&
        disk->device != VIR_DOMAIN_DISK_DEVICE_CDROM) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Disk device type '%1$s' is not supported by macOS Virtualization.Framework. "
                         "Only 'disk' and 'cdrom' device types are supported."),
                       virDomainDiskDeviceTypeToString(disk->device));
        return -1;
    }

    if (virStorageSourceGetActualType(disk->src) != VIR_STORAGE_TYPE_FILE) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Disk source type '%1$s' is not supported by macOS Virtualization.Framework. "
                         "Only file-based disk images are supported. "
                         "Use file paths for disk images, not block devices or network storage."),
                       virStorageTypeToString(virStorageSourceGetActualType(disk->src)));
        return -1;
    }

    /* Validate disk format - macOS Virtualization.Framework supports common formats */
    if (disk->src->format != VIR_STORAGE_FILE_RAW &&
        disk->src->format != VIR_STORAGE_FILE_QCOW2 &&
        disk->src->format != VIR_STORAGE_FILE_NONE) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Disk format '%1$s' is not supported by macOS Virtualization.Framework. "
                         "Supported formats are: raw, qcow2. "
                         "Convert your disk image using qemu-img: "
                         "'qemu-img convert -f source_fmt -O qcow2 source.qcow2 target.qcow2'"),
                       virStorageFileFormatTypeToString(disk->src->format));
        return -1;
    }

    /* Validate disk I/O policy - only default is supported */
    if (disk->iothread) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Disk I/O thread configuration is not supported by macOS Virtualization.Framework. "
                         "Remove the iothread attribute from disk configuration."));
        return -1;
    }

    /* Validate disk transient option - not supported */
    if (disk->transient) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Disk transient option is not supported by macOS Virtualization.Framework. "
                         "Remove the transient attribute from disk configuration."));
        return -1;
    }

    /* Validate disk shareable option - not supported */
    /* Note: shareable field has been removed from newer libvirt API */
    /* This check is no longer needed */

    /* Validate disk cache policy - only default is supported */
    if (disk->cachemode != VIR_DOMAIN_DISK_CACHE_DEFAULT &&
        disk->cachemode != 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Disk cache mode '%1$s' is not supported by macOS Virtualization.Framework. "
                         "Only default cache mode is supported. "
                         "Remove the cache attribute or set it to 'default'."),
                       virDomainDiskCacheTypeToString(disk->cachemode));
        return -1;
    }

    /* Validate disk discard mode - not supported */
    if (disk->discard) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Disk discard mode is not supported by macOS Virtualization.Framework. "
                         "Remove the discard attribute from disk configuration."));
        return -1;
    }

    /* Validate disk detect_zeroes mode - not supported */
    if (disk->detect_zeroes != VIR_DOMAIN_DISK_DETECT_ZEROES_OFF &&
        disk->detect_zeroes != 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Disk detect_zeroes mode '%1$s' is not supported by macOS Virtualization.Framework. "
                         "Remove the detect_zeroes attribute from disk configuration."),
                       virDomainDiskDetectZeroesTypeToString(disk->detect_zeroes));
        return -1;
    }

    return 0;
}

/* Validate network device */
static int
macosvfDomainNetDefValidate(const virDomainNetDef *net)
{
    /* macOS Virtualization.Framework supports:
     * - virtio network model
     * - bridge, network, and user network types
     */
    if (net->model != VIR_DOMAIN_NET_MODEL_VIRTIO &&
        net->model != 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Network model '%1$s' is not supported by macOS Virtualization.Framework. "
                         "Only virtio network model is supported. "
                         "Remove the model attribute or change to 'virtio'."),
                       virDomainNetModelTypeToString(net->model));
        return -1;
    }

    if (net->type != VIR_DOMAIN_NET_TYPE_BRIDGE &&
        net->type != VIR_DOMAIN_NET_TYPE_NETWORK &&
        net->type != VIR_DOMAIN_NET_TYPE_USER) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Network type '%1$s' is not supported by macOS Virtualization.Framework. "
                         "Supported types are: bridge, network, user. "
                         "Change the network type to a supported option."),
                       virDomainNetTypeToString(net->type));
        return -1;
    }

    /* Validate network bandwidth limiting */
    if (net->bandwidth) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Network bandwidth limiting is not supported by macOS Virtualization.Framework. "
                         "Remove the bandwidth element from network configuration."));
        return -1;
    }

    /* Validate network filter (firewall) */
    if (net->filter) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Network filters (firewall) are not supported by macOS Virtualization.Framework. "
                         "Remove the filter attribute from network configuration."));
        return -1;
    }

    /* Validate network script */
    if (net->script) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Network hook scripts are not supported by macOS Virtualization.Framework. "
                         "Remove the script attribute from network configuration."));
        return -1;
    }

    /* Validate port forwarding */
    if (net->guestIP.nips > 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Network port forwarding is not supported by macOS Virtualization.Framework. "
                         "Remove the port forwarding configuration from network interface."));
        return -1;
    }

    /* Validate network coalesce settings */
    if (net->coalesce) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Network coalesce settings are not supported by macOS Virtualization.Framework. "
                         "Remove the coalesce element from network configuration."));
        return -1;
    }

    /* Validate network link state - all states are supported */
    /* Link state can be up, down, or absent (default up) */
    /* No validation needed as all states are valid */

    /* Network driver validation - virtio driver settings are generally tolerated */
    /* No specific validation needed as driver settings are mostly compatible */

    return 0;
}

/* Validate console device */
static int
macosvfDomainConsoleDefValidate(const virDomainChrDef *console)
{
    /* macOS Virtualization.Framework supports:
     * - serial console
     * - pty, file, null, unix, and tcp source types
     * - virtio console type for compatibility
     */
    if (console->targetType != VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_SERIAL &&
        console->targetType != VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_NONE &&
        console->targetType != VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_VIRTIO) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Console target type '%1$s' is not supported by macOS Virtualization.Framework, only serial and virtio are supported"),
                       virDomainChrConsoleTargetTypeToString(console->targetType));
        return -1;
    }

    /* Validate console source type matches serial support */
    if (console->source) {
        if (console->source->type != VIR_DOMAIN_CHR_TYPE_PTY &&
            console->source->type != VIR_DOMAIN_CHR_TYPE_FILE &&
            console->source->type != VIR_DOMAIN_CHR_TYPE_NULL &&
            console->source->type != VIR_DOMAIN_CHR_TYPE_UNIX &&
            console->source->type != VIR_DOMAIN_CHR_TYPE_TCP) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Console type '%1$s' is not supported by macOS Virtualization.Framework"),
                           virDomainChrTypeToString(console->source->type));
            return -1;
        }

        /* Validate TCP protocol - only support raw protocol */
        if (console->source->type == VIR_DOMAIN_CHR_TYPE_TCP) {
            if (console->source->data.tcp.protocol != VIR_DOMAIN_CHR_TCP_PROTOCOL_RAW) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("Console TCP protocol is not supported by macOS Virtualization.Framework, only 'raw' is supported"));
                return -1;
            }
        }
    }

    return 0;
}

/* Validate serial device */
static int
macosvfDomainSerialDefValidate(const virDomainChrDef *serial)
{
    /* macOS Virtualization.Framework supports:
     * - pty, file, null, unix, and tcp source types
     */
    if (serial->source->type != VIR_DOMAIN_CHR_TYPE_PTY &&
        serial->source->type != VIR_DOMAIN_CHR_TYPE_FILE &&
        serial->source->type != VIR_DOMAIN_CHR_TYPE_NULL &&
        serial->source->type != VIR_DOMAIN_CHR_TYPE_UNIX &&
        serial->source->type != VIR_DOMAIN_CHR_TYPE_TCP) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Serial type '%1$s' is not supported"),
                       virDomainChrTypeToString(serial->source->type));
        return -1;
    }

    /* Validate TCP protocol - only support raw protocol */
    if (serial->source->type == VIR_DOMAIN_CHR_TYPE_TCP) {
        if (serial->source->data.tcp.protocol != VIR_DOMAIN_CHR_TCP_PROTOCOL_RAW) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Serial TCP protocol is not supported, only 'raw' is supported"));
            return -1;
        }
    }

    return 0;
}

/* Validate controller device */
static int
macosvfDomainControllerDefValidate(const virDomainControllerDef *controller)
{
    /* macOS Virtualization.Framework supports:
     * - PCI controllers (auto-added, only pci-root model)
     * - ISA controllers (for serial/console devices)
     */
    switch ((virDomainControllerType)controller->type) {
    case VIR_DOMAIN_CONTROLLER_TYPE_PCI:
        /* Only pci-root model is supported */
        if (controller->model != VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT &&
            controller->model != VIR_DOMAIN_CONTROLLER_MODEL_PCI_DEFAULT) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("PCI controller model is not supported, only 'pci-root' is supported"));
            return -1;
        }
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_ISA:
        /* These are supported */
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_USB:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("USB controllers are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_CONTROLLER_TYPE_SCSI:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("SCSI controllers are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_CONTROLLER_TYPE_IDE:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("IDE controllers are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_CONTROLLER_TYPE_FDC:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Floppy controllers are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_CONTROLLER_TYPE_SATA:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("SATA controllers are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_CONTROLLER_TYPE_VIRTIO_SERIAL:
        /* Virtio-serial controllers are tolerated for compatibility
         * but not actively supported */
        break;

    case VIR_DOMAIN_CONTROLLER_TYPE_CCID:
    case VIR_DOMAIN_CONTROLLER_TYPE_XENBUS:
    case VIR_DOMAIN_CONTROLLER_TYPE_NVME:
    case VIR_DOMAIN_CONTROLLER_TYPE_LAST:
        /* Unsupported but silently ignored for compatibility */
        break;

    default:
        /* Other controller types - silently ignore for compatibility */
        break;
    }

    return 0;
}

int
macosvfDomainDeviceDefValidate(const virDomainDeviceDef *dev,
                                const virDomainDef *def G_GNUC_UNUSED,
                                void *opaque G_GNUC_UNUSED,
                                void *parseOpaque G_GNUC_UNUSED)
{
    switch (dev->type) {
    case VIR_DOMAIN_DEVICE_DISK:
        return macosvfDomainDiskDefValidate(dev->data.disk);

    case VIR_DOMAIN_DEVICE_NET:
        return macosvfDomainNetDefValidate(dev->data.net);

    case VIR_DOMAIN_DEVICE_CHR:
        if (dev->data.chr->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_SERIAL)
            return macosvfDomainSerialDefValidate(dev->data.chr);
        if (dev->data.chr->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_CONSOLE)
            return macosvfDomainConsoleDefValidate(dev->data.chr);
        break;

    /* Unsupported device types - reject with clear errors */
    case VIR_DOMAIN_DEVICE_CONTROLLER:
        return macosvfDomainControllerDefValidate(dev->data.controller);

    case VIR_DOMAIN_DEVICE_INPUT:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Input devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_SOUND:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Sound devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_VIDEO:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Video devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_GRAPHICS:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Graphics devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_HOSTDEV:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Host device passthrough is not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_WATCHDOG:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Watchdog devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_HUB:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("USB hubs are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_SMARTCARD:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Smartcard devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_MEMBALLOON:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Memory balloon devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_RNG:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("RNG devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_TPM:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("TPM devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_VSOCK:
        /* VSOCK is tolerated for compatibility but not actively supported */
        break;

    case VIR_DOMAIN_DEVICE_REDIRDEV:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Redirected devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_SHMEM:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Shared memory devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_PANIC:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Panic devices are not supported by macOS Virtualization.Framework"));
        return -1;

    case VIR_DOMAIN_DEVICE_IOMMU:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("IOMMU devices are not supported by macOS Virtualization.Framework"));
        return -1;

    /* Silently ignore these optional device types */
    case VIR_DOMAIN_DEVICE_NONE:
    case VIR_DOMAIN_DEVICE_LEASE:
    case VIR_DOMAIN_DEVICE_FS:
    case VIR_DOMAIN_DEVICE_NVRAM:
    case VIR_DOMAIN_DEVICE_MEMORY:
    case VIR_DOMAIN_DEVICE_AUDIO:
    case VIR_DOMAIN_DEVICE_CRYPTO:
    case VIR_DOMAIN_DEVICE_PSTORE:
    case VIR_DOMAIN_DEVICE_LAST:
        break;
    }

    return 0;
}
