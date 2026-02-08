/*
 * macosvf_capabilities.c: macosvf capabilities
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

#include "macosvf_capabilities.h"
#include "viralloc.h"
#include "virstring.h"
#include "cpu/cpu.h"
#include "virobject.h"
#include "virlog.h"
#include "virerror.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macOSvf_capabilities");

virCaps *
macosvfCreateCapabilities(void)
{
    virCaps *caps;
    virCapsGuest *guest;
    virArch hostarch = virArchFromHost();

    VIR_DEBUG("Creating macosvf capabilities for host arch %d (%s)",
              hostarch, virArchToString(hostarch));

    /* Create capabilities structure for ARM64/Apple Silicon */
    if ((caps = virCapabilitiesNew(hostarch, false, false)) == NULL)
        return NULL;

    /* ARM64 only - Apple Silicon */
    if (hostarch == VIR_ARCH_AARCH64) {
        VIR_DEBUG("Adding ARM64 guest to capabilities");
        guest = virCapabilitiesAddGuest(caps, VIR_DOMAIN_OSTYPE_HVM,
                                        hostarch, "macosvf",
                                        NULL, 0, NULL);
        if (!guest) {
            virObjectUnref(caps);
            return NULL;
        }

        /* Add domain info */
        if (virCapabilitiesAddGuestDomain(guest, VIR_DOMAIN_VIRT_MACOSVF,
                                          NULL, NULL, 0, NULL) == NULL) {
            virObjectUnref(caps);
            return NULL;
        }
    } else {
        /* Intel x86_64 not supported in this implementation */
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("macOS Virtualization.Framework driver only supports Apple Silicon (ARM64)"));
        virObjectUnref(caps);
        return NULL;
    }

    /* Probe host CPU */
    if (!(caps->host.cpu = virCPUProbeHost(hostarch)))
        VIR_WARN("Failed to get host CPU");

    return caps;
}
