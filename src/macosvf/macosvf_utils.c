/*
 * macosvf_utils.c: Utility functions for macosvf driver
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

#include "macosvf_utils.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macOSvf_utils");

/* Format VZVirtualMachineState for logging */
const char *
macosvfVMStateToString(macosvfVMState state)
{
    switch (state) {
    case MACOSVF_VM_STATE_STOPPED:
        return "stopped";
    case MACOSVF_VM_STATE_RUNNING:
        return "running";
    case MACOSVF_VM_STATE_PAUSED:
        return "paused";
    case MACOSVF_VM_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}
