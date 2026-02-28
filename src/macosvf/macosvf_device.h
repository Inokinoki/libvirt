/*
 * macosvf_device.h: Device management for macosvf driver
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
#include "macosvf_vm.h"

/* Device setup functions */
int macosvfDomainDeviceDefValidate(const virDomainDeviceDef *dev,
                                   const virDomainDef *def,
                                   void *opaque,
                                   void *parseOpaque);

/* Device-specific functions will be added here */
