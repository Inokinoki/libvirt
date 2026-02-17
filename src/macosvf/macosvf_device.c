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

#include "domain_conf.h"
#include "macosvf_device.h"
#include "virerror.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macOSvf_device");

/* Validate disk device */
static int macosvfDomainDiskDefValidate(const virDomainDiskDef *disk) {
  /* macOS Virtualization.Framework supports:
   * - virtio disk bus
   * - file-based disk images
   * - raw and qcow2 formats
   * - disk and cdrom device types
   */
  if (disk->bus != VIR_DOMAIN_DISK_BUS_VIRTIO && disk->bus != 0) {
    VIR_DEBUG("Disk bus '%s' is not supported by macOS "
              "Virtualization.Framework, ignoring",
              virDomainDiskBusTypeToString(disk->bus));
  }

  if (disk->device != VIR_DOMAIN_DISK_DEVICE_DISK &&
      disk->device != VIR_DOMAIN_DISK_DEVICE_CDROM) {
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Disk device type '%1$s' is not supported by macOS "
                     "Virtualization.Framework. "
                     "Only 'disk' and 'cdrom' device types are supported."),
                   virDomainDiskDeviceTypeToString(disk->device));
    return -1;
  }

  if (virStorageSourceGetActualType(disk->src) != VIR_STORAGE_TYPE_FILE) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Disk source type '%1$s' is not supported by macOS "
          "Virtualization.Framework. "
          "Only file-based disk images are supported. "
          "Use file paths for disk images, not block devices or network "
          "storage."),
        virStorageTypeToString(virStorageSourceGetActualType(disk->src)));
    return -1;
  }

  /* Validate disk format - macOS Virtualization.Framework supports common
   * formats */
  if (disk->src->format != VIR_STORAGE_FILE_RAW &&
      disk->src->format != VIR_STORAGE_FILE_QCOW2 &&
      disk->src->format != VIR_STORAGE_FILE_NONE) {
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Disk format '%1$s' is not supported by macOS "
                     "Virtualization.Framework. "
                     "Supported formats are: raw, qcow2. "
                     "Convert your disk image using qemu-img: "
                     "'qemu-img convert -f source_fmt -O qcow2 source.qcow2 "
                     "target.qcow2'"),
                   virStorageFileFormatTypeToString(disk->src->format));
    return -1;
  }

  /* Validate disk I/O policy - accepted for XML parity */
  if (disk->iothread) {
    VIR_DEBUG("Disk I/O thread configuration is not supported by macOS "
              "Virtualization.Framework, ignoring");
  }

  /* Validate disk transient option - accepted for XML parity */
  if (disk->transient) {
    VIR_DEBUG("Disk transient option is not supported by macOS "
              "Virtualization.Framework, ignoring");
  }

  /* Validate disk shareable option - not supported */
  /* Note: shareable field has been removed from newer libvirt API */
  /* This check is no longer needed */

  /* Validate disk cache policy - macOS Virtualization.Framework doesn't
   * support custom cache modes, but we tolerate them in the configuration
   * for compatibility with generic XMLs. */
  if (disk->cachemode != VIR_DOMAIN_DISK_CACHE_DEFAULT &&
      disk->cachemode != 0) {
    VIR_DEBUG("Disk cache mode '%s' is not supported by macOS "
              "Virtualization.Framework, ignoring",
              virDomainDiskCacheTypeToString(disk->cachemode));
  }

  /* Validate disk discard mode - macOS Virtualization.Framework doesn't
   * support discard, but we tolerate it for compatibility. */
  if (disk->discard) {
    VIR_DEBUG("Disk discard mode is not supported by macOS "
              "Virtualization.Framework, ignoring");
  }

  /* Validate disk detect_zeroes mode - accepted for XML parity */
  if (disk->detect_zeroes != VIR_DOMAIN_DISK_DETECT_ZEROES_OFF &&
      disk->detect_zeroes != 0) {
    VIR_DEBUG("Disk detect_zeroes mode is not supported by macOS "
              "Virtualization.Framework, ignoring");
  }

  return 0;
}

/* Validate network device */
static int macosvfDomainNetDefValidate(const virDomainNetDef *net) {
  /* macOS Virtualization.Framework supports:
   * - virtio network model
   * - bridge, network, and user network types
   */
  if (net->model != VIR_DOMAIN_NET_MODEL_VIRTIO && net->model != 0) {
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Network model '%1$s' is not supported by macOS "
                     "Virtualization.Framework. "
                     "Only virtio network model is supported. "
                     "Remove the model attribute or change to 'virtio'."),
                   virDomainNetModelTypeToString(net->model));
    return -1;
  }

  if (net->type != VIR_DOMAIN_NET_TYPE_BRIDGE &&
      net->type != VIR_DOMAIN_NET_TYPE_NETWORK &&
      net->type != VIR_DOMAIN_NET_TYPE_USER) {
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Network type '%1$s' is not supported by macOS "
                     "Virtualization.Framework. "
                     "Supported types are: bridge, network, user. "
                     "Change the network type to a supported option."),
                   virDomainNetTypeToString(net->type));
    return -1;
  }

  /* Validate network bandwidth limiting */
  if (net->bandwidth) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Network bandwidth limiting is not supported by macOS "
          "Virtualization.Framework. "
          "Remove the bandwidth element from network configuration."));
    return -1;
  }

  /* Validate network filter (firewall) */
  if (net->filter) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Network filters (firewall) are not supported by macOS "
          "Virtualization.Framework. "
          "Remove the filter attribute from network configuration."));
    return -1;
  }

  /* Validate network script */
  if (net->script) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Network hook scripts are not supported by macOS "
          "Virtualization.Framework. "
          "Remove the script attribute from network configuration."));
    return -1;
  }

  /* Validate port forwarding */
  if (net->guestIP.nips > 0) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Network port forwarding is not supported by macOS "
          "Virtualization.Framework. "
          "Remove the port forwarding configuration from network interface."));
    return -1;
  }

  /* Validate network coalesce settings */
  if (net->coalesce) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Network coalesce settings are not supported by macOS "
          "Virtualization.Framework. "
          "Remove the coalesce element from network configuration."));
    return -1;
  }

  /* Validate network link state - all states are supported */
  /* Link state can be up, down, or absent (default up) */
  /* No validation needed as all states are valid */

  /* Network driver validation - virtio driver settings are generally tolerated
   */
  /* No specific validation needed as driver settings are mostly compatible */

  return 0;
}

/* Validate console device */
static int macosvfDomainConsoleDefValidate(const virDomainChrDef *console) {
  /* macOS Virtualization.Framework supports:
   * - serial console
   * - pty, file, null, unix, and tcp source types
   * - virtio console type for compatibility
   */
  if (console->targetType != VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_SERIAL &&
      console->targetType != VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_NONE &&
      console->targetType != VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_VIRTIO) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Console target type '%1$s' is not supported by macOS "
          "Virtualization.Framework, only serial and virtio are supported"),
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
                     _("Console type '%1$s' is not supported by macOS "
                       "Virtualization.Framework"),
                     virDomainChrTypeToString(console->source->type));
      return -1;
    }

    /* Validate TCP protocol - only support raw protocol */
    if (console->source->type == VIR_DOMAIN_CHR_TYPE_TCP) {
      if (console->source->data.tcp.protocol !=
          VIR_DOMAIN_CHR_TCP_PROTOCOL_RAW) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("Console TCP protocol is not supported by macOS "
                         "Virtualization.Framework, only 'raw' is supported"));
        return -1;
      }
    }
  }

  return 0;
}

/* Validate serial device */
static int macosvfDomainSerialDefValidate(const virDomainChrDef *serial) {
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
      virReportError(
          VIR_ERR_CONFIG_UNSUPPORTED,
          _("Serial TCP protocol is not supported, only 'raw' is supported"));
      return -1;
    }
  }

  return 0;
}

/* Validate filesystem (shared folder) device */
static int macosvfDomainFSDefValidate(const virDomainFSDef *fs) {
  /* macOS Virtualization.Framework supports:
   * - virtio-9p filesystem model for shared folders
   * - mount tag for identifying the shared folder
   * - file or directory sources
   */
  if (fs->type != VIR_DOMAIN_FS_TYPE_MOUNT) {
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Filesystem type '%1$s' is not supported by macOS "
                     "Virtualization.Framework. "
                     "Only 'mount' type is supported for shared folders. "
                     "Change the filesystem type to 'mount'."),
                   virDomainFSTypeToString(fs->type));
    return -1;
  }

  if (fs->fsdriver != VIR_DOMAIN_FS_DRIVER_TYPE_VIRTIOFS &&
      fs->fsdriver != VIR_DOMAIN_FS_DRIVER_TYPE_DEFAULT && fs->fsdriver != 0) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Filesystem driver '%1$s' is not supported by macOS "
          "Virtualization.Framework. "
          "Only 'virtiofs' filesystem driver is supported for shared folders. "
          "Change the fsdriver to 'virtiofs' or remove the fsdriver "
          "attribute."),
        virDomainFSDriverTypeToString(fs->fsdriver));
    return -1;
  }

  /* Validate that source path is specified */
  if (!fs->src) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Filesystem source must be specified for shared folders. "
          "Add a 'source' element with the directory path to share."));
    return -1;
  }

  /* Validate that mount tag (target) is specified */
  if (!fs->dst || fs->dst[0] == '\0') {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Filesystem target (mount tag) must be specified for shared folders. "
          "Add a 'target' element with the mount tag name."));
    return -1;
  }

  /* Validate access mode - default and mapped are supported */
  if (fs->accessmode != VIR_DOMAIN_FS_ACCESSMODE_PASSTHROUGH &&
      fs->accessmode != VIR_DOMAIN_FS_ACCESSMODE_MAPPED &&
      fs->accessmode != 0) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Filesystem access mode '%1$s' is not supported by macOS "
          "Virtualization.Framework. "
          "Supported modes are: 'passthrough', 'mapped'. "
          "Remove the accessmode attribute or use a supported mode."),
        virDomainFSAccessModeTypeToString(fs->accessmode));
    return -1;
  }

  /* Validate wrpolicy - only default is supported */
  if (fs->wrpolicy != VIR_DOMAIN_FS_WRPOLICY_DEFAULT && fs->wrpolicy != 0) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Filesystem write policy is not supported by macOS "
          "Virtualization.Framework. "
          "Remove the wrpolicy attribute from filesystem configuration."));
    return -1;
  }

  /* Validate filesystem format - not applicable for virtio-9p */
  if (fs->format) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Filesystem format is not supported for virtio-9p shared folders. "
          "Remove the format element from filesystem configuration."));
    return -1;
  }

  /* Validate readonly mode is not set (shared folders should be writable) */
  /* Note: readonly is supported but with limitations */
  /* No validation needed for readonly */

  return 0;
}

/* Validate controller device */
static int
macosvfDomainControllerDefValidate(const virDomainControllerDef *controller) {
  /* macOS Virtualization.Framework supports:
   * - PCI controllers (auto-added, only pci-root model)
   * - ISA controllers (for serial/console devices)
   */
  switch ((virDomainControllerType)controller->type) {
  case VIR_DOMAIN_CONTROLLER_TYPE_PCI:
    /* Most PCI models are tolerated for configuration parity */
    break;

  case VIR_DOMAIN_CONTROLLER_TYPE_ISA:
    /* These are supported */
    break;

  case VIR_DOMAIN_CONTROLLER_TYPE_USB:
  case VIR_DOMAIN_CONTROLLER_TYPE_SCSI:
  case VIR_DOMAIN_CONTROLLER_TYPE_IDE:
  case VIR_DOMAIN_CONTROLLER_TYPE_FDC:
  case VIR_DOMAIN_CONTROLLER_TYPE_SATA:
    /* Tolerated for compatibility but not actively supported */
    VIR_DEBUG("Controller type is not supported by macOS "
              "Virtualization.Framework, ignoring");
    break;

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

/* Validate graphics device */
static int
macosvfDomainGraphicsDefValidate(const virDomainGraphicsDef *graphics) {
  /* macOS Virtualization.Framework supports graphics devices
   * Graphics are provided through VZGraphicsDeviceConfiguration
   * Only basic graphics is supported - no VNC, SPICE, etc.
   */
  switch (graphics->type) {
  case VIR_DOMAIN_GRAPHICS_TYPE_VNC:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("VNC graphics are not supported by macOS Virtualization.Framework. "
          "The framework provides native graphics output. "
          "Remove the <graphics type='vnc'> element from your configuration."));
    return -1;

  case VIR_DOMAIN_GRAPHICS_TYPE_SPICE:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("SPICE graphics are not supported by macOS Virtualization.Framework. "
          "The framework provides native graphics output. "
          "Remove the <graphics type='spice'> element from your "
          "configuration."));
    return -1;

  case VIR_DOMAIN_GRAPHICS_TYPE_RDP:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("RDP graphics are not supported by macOS Virtualization.Framework. "
          "The framework provides native graphics output. "
          "Remove the <graphics type='rdp'> element from your configuration."));
    return -1;

  case VIR_DOMAIN_GRAPHICS_TYPE_DESKTOP:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Desktop graphics are not supported by macOS "
                     "Virtualization.Framework. "
                     "The framework provides native graphics output. "
                     "Remove the <graphics type='desktop'> element from your "
                     "configuration."));
    return -1;

  case VIR_DOMAIN_GRAPHICS_TYPE_SDL:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("SDL graphics are not supported by macOS Virtualization.Framework. "
          "The framework provides native graphics output. "
          "Remove the <graphics type='sdl'> element from your configuration."));
    return -1;

  case VIR_DOMAIN_GRAPHICS_TYPE_EGL_HEADLESS:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("EGL headless graphics are not supported by macOS "
                     "Virtualization.Framework. "
                     "The framework provides native graphics output. "
                     "Remove the <graphics type='egl-headless'> element from "
                     "your configuration."));
    return -1;

  case VIR_DOMAIN_GRAPHICS_TYPE_DBUS:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("D-Bus graphics are not supported by macOS Virtualization.Framework. "
          "The framework provides native graphics output. "
          "Remove the <graphics type='dbus'> element from your "
          "configuration."));
    return -1;

  case VIR_DOMAIN_GRAPHICS_TYPE_LAST:
    break;

  default:
    /* For compatibility, we accept graphics elements but only use the video
     * device to enable the graphics device in the VM configuration */
    break;
  }

  return 0;
}

/* Validate sound device */
static int macosvfDomainSoundDefValidate(const virDomainSoundDef *sound) {
  /* macOS Virtualization.Framework supports audio devices
   * through VZVirtioSoundDeviceConfiguration (macOS 12+)
   * Only virtio audio model is supported
   */
  switch (sound->model) {
  case VIR_DOMAIN_SOUND_MODEL_VIRTIO:
    /* Virtio audio is supported */
    break;

  case VIR_DOMAIN_SOUND_MODEL_ICH6:
  case VIR_DOMAIN_SOUND_MODEL_ICH7:
  case VIR_DOMAIN_SOUND_MODEL_ICH9:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Intel ICH audio is not supported by macOS Virtualization.Framework. "
          "Only virtio audio is supported. "
          "Change the sound model to 'virtio' in your configuration."));
    return -1;

  case VIR_DOMAIN_SOUND_MODEL_AC97:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("AC97 audio is not supported by macOS Virtualization.Framework. "
          "Only virtio audio is supported. "
          "Change the sound model to 'virtio' in your configuration."));
    return -1;

  case VIR_DOMAIN_SOUND_MODEL_ES1370:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("ES1370 audio is not supported by macOS Virtualization.Framework. "
          "Only virtio audio is supported. "
          "Change the sound model to 'virtio' in your configuration."));
    return -1;

  case VIR_DOMAIN_SOUND_MODEL_SB16:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Sound Blaster 16 audio is not supported by macOS "
          "Virtualization.Framework. "
          "Only virtio audio is supported. "
          "Change the sound model to 'virtio' in your configuration."));
    return -1;

  case VIR_DOMAIN_SOUND_MODEL_USB:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("USB audio is not supported by macOS Virtualization.Framework. "
          "Only virtio audio is supported. "
          "Change the sound model to 'virtio' in your configuration."));
    return -1;

  case VIR_DOMAIN_SOUND_MODEL_PCSPK:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("PC speaker audio is not supported by macOS "
          "Virtualization.Framework. "
          "Only virtio audio is supported. "
          "Change the sound model to 'virtio' in your configuration."));
    return -1;

  case VIR_DOMAIN_SOUND_MODEL_LAST:
    break;

  default:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Audio device model '%1$s' is not supported by macOS "
          "Virtualization.Framework. "
          "Only virtio audio is supported. "
          "Change the sound model to 'virtio' in your configuration."),
        virDomainSoundModelTypeToString(sound->model));
    return -1;
  }

  return 0;
}

/* Validate video device */
static int macosvfDomainVideoDefValidate(const virDomainVideoDef *video) {
  /* macOS Virtualization.Framework supports video devices
   * Graphics are provided through VZGraphicsDeviceConfiguration
   */
  switch (video->type) {
  case VIR_DOMAIN_VIDEO_TYPE_VGA:
  case VIR_DOMAIN_VIDEO_TYPE_CIRRUS:
  case VIR_DOMAIN_VIDEO_TYPE_VMVGA:
  case VIR_DOMAIN_VIDEO_TYPE_QXL:
  case VIR_DOMAIN_VIDEO_TYPE_VIRTIO:
  case VIR_DOMAIN_VIDEO_TYPE_DEFAULT:
  case VIR_DOMAIN_VIDEO_TYPE_XEN:
  case VIR_DOMAIN_VIDEO_TYPE_VBOX:
  case VIR_DOMAIN_VIDEO_TYPE_GOP:
  case VIR_DOMAIN_VIDEO_TYPE_BOCHS:
  case VIR_DOMAIN_VIDEO_TYPE_PARALLELS:
  case VIR_DOMAIN_VIDEO_TYPE_NONE:
  case VIR_DOMAIN_VIDEO_TYPE_RAMFB:
    /* All video types are accepted - the framework handles graphics internally
     */
    break;

  case VIR_DOMAIN_VIDEO_TYPE_LAST:
    break;

  default:
    /* Accept all video types - the framework handles graphics internally */
    break;
  }

  return 0;
}

/* Validate input device */
static int macosvfDomainInputDefValidate(const virDomainInputDef *input) {
  /* macOS Virtualization.Framework supports input devices
   * Keyboard and mouse/tablet input are provided through
   * VZUSBKeyboardConfiguration and
   * VZUSBScreenCoordinatePointingDeviceConfiguration
   */
  switch (input->type) {
  case VIR_DOMAIN_INPUT_TYPE_KBD:
    /* Keyboard input is supported */
    break;

  case VIR_DOMAIN_INPUT_TYPE_MOUSE:
    /* Mouse input is supported via pointing device */
    break;

  case VIR_DOMAIN_INPUT_TYPE_TABLET:
    /* Tablet input is supported via pointing device */
    break;

  case VIR_DOMAIN_INPUT_TYPE_LAST:
    break;

  default:
    /* Accept all input types */
    break;
  }

  return 0;
}

/* Validate RNG (random number generator) device */
static int
macosvfDomainRNGDefValidate(const virDomainRNGDef *rng)
{
  /* macOS Virtualization.Framework supports virtio RNG devices
   * through VZVirtioEntropyDeviceConfiguration
   * The framework provides entropy from the host to the guest
   */
  switch (rng->model) {
  case VIR_DOMAIN_RNG_MODEL_VIRTIO:
  case VIR_DOMAIN_RNG_MODEL_VIRTIO_TRANSITIONAL:
  case VIR_DOMAIN_RNG_MODEL_VIRTIO_NON_TRANSITIONAL:
    /* All virtio RNG variants are supported */
    break;

  case VIR_DOMAIN_RNG_MODEL_LAST:
    break;

  default:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("RNG model '%1$s' is not supported by macOS "
                     "Virtualization.Framework. "
                     "Only 'virtio' model variants are supported. "
                     "Use <rng model='virtio'> in your configuration."),
                   virDomainRNGModelTypeToString(rng->model));
    return -1;
  }

  /* Validate backend */
  switch (rng->backend) {
  case VIR_DOMAIN_RNG_BACKEND_RANDOM:
    /* /dev/random, /dev/urandom, and similar devices are supported */
    if (!rng->source.file) {
      virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                     _("RNG device with 'random' backend requires a source file. "
                       "Specify <backend model='random'>/dev/urandom</backend> "
                       "or similar."));
      return -1;
    }
    break;

  case VIR_DOMAIN_RNG_BACKEND_EGD:
    /* EGD (Entropy Gathering Daemon) protocol is not supported */
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("EGD protocol backend is not supported by macOS "
                     "Virtualization.Framework. "
                     "Use <backend model='random'>/dev/urandom</backend> instead."));
    return -1;

  case VIR_DOMAIN_RNG_BACKEND_BUILTIN:
    /* Builtin backend is not applicable for virtio-rng */
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Builtin RNG backend is not supported by macOS "
                     "Virtualization.Framework. "
                     "Use <backend model='random'>/dev/urandom</backend> instead."));
    return -1;

  case VIR_DOMAIN_RNG_BACKEND_LAST:
    break;
  }

  return 0;
}

/* Validate watchdog device */
static int
macosvfDomainWatchdogDefValidate(const virDomainWatchdogDef *watchdog) {
  /* macOS Virtualization.Framework does not support watchdog devices
   * VM monitoring/hardware watchdog is not available
   */
  switch (watchdog->model) {
  case VIR_DOMAIN_WATCHDOG_MODEL_I6300ESB:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Intel 6300ESB watchdog is not supported by macOS "
                     "Virtualization.Framework. "
                     "Hardware watchdog devices are not available. "
                     "Remove the <watchdog model='i6300esb'/> element from "
                     "your configuration."));
    return -1;

  case VIR_DOMAIN_WATCHDOG_MODEL_IB700:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("IB700 watchdog is not supported by macOS Virtualization.Framework. "
          "Hardware watchdog devices are not available. "
          "Remove the <watchdog model='ib700'/> element from your "
          "configuration."));
    return -1;

  case VIR_DOMAIN_WATCHDOG_MODEL_DIAG288:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("diag288 watchdog is not supported by macOS "
                     "Virtualization.Framework. "
                     "Hardware watchdog devices are not available. "
                     "Remove the <watchdog model='diag288'/> element from your "
                     "configuration."));
    return -1;

  case VIR_DOMAIN_WATCHDOG_MODEL_ITCO:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Intel TCO watchdog is not supported by macOS "
                     "Virtualization.Framework. "
                     "Hardware watchdog devices are not available. "
                     "Remove the <watchdog model='itco'/> element from your "
                     "configuration."));
    return -1;

  case VIR_DOMAIN_WATCHDOG_MODEL_LAST:
    break;

  default:
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Watchdog devices are not supported by macOS "
          "Virtualization.Framework. "
          "Hardware watchdog devices are not available. "
          "Remove any <watchdog> elements from your configuration."));
    return -1;
  }

  return 0;
}

int macosvfDomainDeviceDefValidate(const virDomainDeviceDef *dev,
                                   const virDomainDef *def G_GNUC_UNUSED,
                                   void *opaque G_GNUC_UNUSED,
                                   void *parseOpaque G_GNUC_UNUSED) {
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
    return macosvfDomainInputDefValidate(dev->data.input);

  case VIR_DOMAIN_DEVICE_SOUND:
    return macosvfDomainSoundDefValidate(dev->data.sound);

  case VIR_DOMAIN_DEVICE_VIDEO:
    return macosvfDomainVideoDefValidate(dev->data.video);

  case VIR_DOMAIN_DEVICE_GRAPHICS:
    return macosvfDomainGraphicsDefValidate(dev->data.graphics);

  case VIR_DOMAIN_DEVICE_HOSTDEV:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Host device passthrough is not supported by macOS "
                     "Virtualization.Framework"));
    return -1;

  case VIR_DOMAIN_DEVICE_WATCHDOG:
    /* We don't support watchdog hardware, but we allow it in XML for parity */
    VIR_DEBUG(
        "Watchdog devices are not supported by macOS Virtualization.Framework, "
        "ignoring during XML parsing");
    break;

  case VIR_DOMAIN_DEVICE_HUB:
  case VIR_DOMAIN_DEVICE_SMARTCARD:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Smartcard devices are not supported by macOS "
                     "Virtualization.Framework"));
    return -1;

  case VIR_DOMAIN_DEVICE_MEMBALLOON:
    VIR_DEBUG("Memory balloon devices are not supported by macOS "
              "Virtualization.Framework, ignoring");
    break;

  case VIR_DOMAIN_DEVICE_RNG:
    return macosvfDomainRNGDefValidate(dev->data.rng);

  case VIR_DOMAIN_DEVICE_TPM:
    VIR_DEBUG(
        "TPM devices are not supported by macOS Virtualization.Framework, "
        "ignoring");
    break;

  case VIR_DOMAIN_DEVICE_VSOCK:
    /* VSOCK is tolerated for compatibility but not actively supported */
    break;

  case VIR_DOMAIN_DEVICE_FS:
    return macosvfDomainFSDefValidate(dev->data.fs);

  case VIR_DOMAIN_DEVICE_REDIRDEV:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Redirected devices are not supported by macOS "
                     "Virtualization.Framework"));
    return -1;

  case VIR_DOMAIN_DEVICE_SHMEM:
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Shared memory devices are not supported by macOS "
                     "Virtualization.Framework"));
    return -1;

  case VIR_DOMAIN_DEVICE_PANIC:
    VIR_DEBUG(
        "Panic devices are not supported by macOS Virtualization.Framework, "
        "ignoring");
    break;

  case VIR_DOMAIN_DEVICE_IOMMU:
    VIR_DEBUG(
        "IOMMU devices are not supported by macOS Virtualization.Framework, "
        "ignoring");
    break;

  /* Silently ignore these optional device types */
  case VIR_DOMAIN_DEVICE_NONE:
  case VIR_DOMAIN_DEVICE_LEASE:
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
