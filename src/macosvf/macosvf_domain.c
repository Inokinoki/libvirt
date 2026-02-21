/*
 * macosvf_domain.c: Domain operations for macosvf driver
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

#include "datatypes.h"
#include "domain_conf.h"
#include "macosvf/macosvf_conf.h"
#include "macosvf_capabilities.h"
#include "macosvf_device.h"
#include "macosvf_vm.h"
#include "virerror.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macosvf_domain");

static void *macosvfDomainObjPrivateAlloc(void *opaque G_GNUC_UNUSED) {
  macosvfDomainObjPrivate *priv;

  priv = g_new0(macosvfDomainObjPrivate, 1);
  priv->vm = NULL;
  return priv;
}

void macosvfDomainObjPrivateFree(void *obj) {
  macosvfDomainObjPrivate *priv = obj;

  if (!priv)
    return;

  /* Free the VM object if it exists */
  if (priv->vm) {
    macosvfVMObject *vm = (macosvfVMObject *)priv->vm;
    macosvfVMFree(vm);
    priv->vm = NULL;
  }

  g_free(priv);
}

virDomainXMLPrivateDataCallbacks macosvfDriverPrivateDataCallbacks = {
    .alloc = macosvfDomainObjPrivateAlloc,
    .free = macosvfDomainObjPrivateFree,
};

static int macosvfDomainDefValidate(const virDomainDef *def,
                                    void *opaque G_GNUC_UNUSED,
                                    void *parseOpaque G_GNUC_UNUSED) {
  size_t i;

  /* Validate boot devices - move to operational if too strict, but basic check
   * is okay */
  for (i = 0; i < def->os.nBootDevs; i++) {
    switch (def->os.bootDevs[i]) {
    case VIR_DOMAIN_BOOT_DISK:
    case VIR_DOMAIN_BOOT_CDROM:
      break;

    case VIR_DOMAIN_BOOT_FLOPPY:
    case VIR_DOMAIN_BOOT_NET:
      /* These are not supported by the framework, but we can allow them in XML
       */
      VIR_DEBUG(
          "Boot device is not supported by macOS Virtualization.Framework, "
          "ignoring during XML parsing");
      break;

    case VIR_DOMAIN_BOOT_LAST:
    default:
      break;
    }
  }

  /* CPU and Memory validation moved to macosvfVMCreate to pass XML tests */
  /* We only keep architecture and OS type checks in PostParse */

  return 0;
}

/* Domain post-parse callback */
static int macosvfDomainDefPostParse(virDomainDef *def,
                                     unsigned int parseFlags G_GNUC_UNUSED,
                                     void *opaque,
                                     void *parseOpaque G_GNUC_UNUSED) {
  macosvfConn *driver = opaque;
  g_autoptr(virCaps) caps = NULL;

  if (!driver || !(caps = macosvfDriverGetCapabilities(driver)))
    return -1;

  /* Verify the domain is supported by this driver */
  if (!virCapabilitiesDomainSupported(caps, def->os.type, def->os.arch,
                                      def->virtType, true))
    return -1;

  /* macOS Virtualization.Framework requires ARM64 */
  if (def->os.arch != VIR_ARCH_AARCH64) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("macOS Virtualization.Framework only supports ARM64, not '%1$s'"),
        virArchToString(def->os.arch));
    return -1;
  }

  /* macOS Virtualization.Framework only supports HVM domains */
  if (def->os.type != VIR_DOMAIN_OSTYPE_HVM) {
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("macOS Virtualization.Framework only supports HVM "
                     "domains, not '%1$s'"),
                   virDomainOSTypeToString(def->os.type));
    return -1;
  }

  /* Validate machine type - must be macosvf or none */
  if (def->os.machine && g_strcmp0(def->os.machine, "macosvf") != 0 &&
      g_strcmp0(def->os.machine, "") != 0) {
    virReportError(
        VIR_ERR_CONFIG_UNSUPPORTED,
        _("Invalid machine type '%1$s', only 'macosvf' is supported"),
        def->os.machine);
    return -1;
  }

  /* Validate bootloader - not supported, must use direct kernel boot */
  if (def->os.bootloader) {
    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                   _("Bootloader configuration is not supported by macOS "
                     "Virtualization.Framework"));
    return -1;
  }

  /* Set default CPU count to 1 if not specified */
  if (virDomainDefGetVcpusMax(def) == 0) {
    virDomainDefSetVcpusMax(def, 1, NULL);
    virDomainDefSetVcpus(def, 1);
  }

  /* Add implicit devices */
  /* Add PCI root controller for device attachment */
  virDomainDefMaybeAddController(def, VIR_DOMAIN_CONTROLLER_TYPE_PCI, 0,
                                 VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT);

  /* For ARM64, we might need an implicit ISA controller for serial devices */
  if (def->nserials > 0 || def->nconsoles > 0) {
    virDomainDefMaybeAddController(def, VIR_DOMAIN_CONTROLLER_TYPE_ISA, 0,
                                   VIR_DOMAIN_CONTROLLER_MODEL_ISA_DEFAULT);
  }

  return 0;
}

/* Domain parser config */
virDomainDefParserConfig virMacOSVFDriverDomainDefParserConfig = {
    .domainPostParseCallback = macosvfDomainDefPostParse,
    .domainValidateCallback = macosvfDomainDefValidate,
    .deviceValidateCallback = macosvfDomainDeviceDefValidate,
    .features = VIR_DOMAIN_DEF_FEATURE_INDIVIDUAL_VCPUS |
                VIR_DOMAIN_DEF_FEATURE_NET_MODEL_STRING |
                VIR_DOMAIN_DEF_FEATURE_DISK_FD,
};

virDomainXMLOption *virMacOSVFDriverDomainXMLConfInit(void) {
  return virDomainXMLOptionNew(&virMacOSVFDriverDomainDefParserConfig,
                               &macosvfDriverPrivateDataCallbacks, NULL, NULL,
                               NULL, NULL);
}

virDomainObj *macosvfDomObjFromDomain(virDomainPtr domain) {
  macosvfConn *privconn = domain->conn->privateData;
  virDomainObj *vm;
  char uuidstr[VIR_UUID_STRING_BUFLEN];

  vm = virDomainObjListFindByUUID(privconn->domains, domain->uuid);
  if (!vm) {
    virUUIDFormat(domain->uuid, uuidstr);
    virReportError(VIR_ERR_NO_DOMAIN,
                   _("no domain with matching uuid '%1$s' (%2$s)"), uuidstr,
                   domain->name);
    return NULL;
  }

  return vm;
}
