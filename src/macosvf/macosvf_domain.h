/*
 * macosvf_domain.h: macosvf domain private state
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

#pragma once

#include "conf/domain_conf.h"
#include "macosvf_conf.h"

/* Private domain data */
typedef struct _macosvfDomainObjPrivate macosvfDomainObjPrivate;

struct _macosvfDomainObjPrivate {
    /* Pointer to the virtualization.framework VM object */
    void *vm;  /* Opaque pointer to VZVirtualMachine */
};

/* Private data callbacks */
extern virDomainXMLPrivateDataCallbacks macosvfDriverPrivateDataCallbacks;

/* Domain parser config - must be set by driver initialization */
extern virDomainDefParserConfig virMacOSVFDriverDomainDefParserConfig;

virDomainObj *macosvfDomObjFromDomain(virDomainPtr domain);
void macosvfDomainObjPrivateFree(void *obj);
