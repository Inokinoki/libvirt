/*
 * macosvf_vm.h: Bridge to virtualization.framework (Objective-C)
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
#include "macosvf_domain.h"

/* Opaque pointer to Objective-C VM object */
typedef struct _macosvfVMObject macosvfVMObject;

/* VM state tracking */
typedef enum {
    MACOSVF_VM_STATE_STOPPED = 0,
    MACOSVF_VM_STATE_RUNNING = 1,
    MACOSVF_VM_STATE_PAUSED = 2,
    MACOSVF_VM_STATE_ERROR = 3,
} macosvfVMState;

/* VM operations */
int macosvfVMCreate(virDomainDef *def, macosvfVMObject **vmptr);
int macosvfVMStart(macosvfVMObject *vm);
int macosvfVMStop(macosvfVMObject *vm, bool force);
int macosvfVMPause(macosvfVMObject *vm);
int macosvfVMResume(macosvfVMObject *vm);
macosvfVMState macosvfVMGetState(macosvfVMObject *vm);
void macosvfVMFree(macosvfVMObject *vm);

/* Configuration helpers */
int macosvfVMSetupBootloader(virDomainDef *def, macosvfVMObject *vm);
int macosvfVMSetupCPUs(virDomainDef *def, macosvfVMObject *vm);
int macosvfVMSetupMemory(virDomainDef *def, macosvfVMObject *vm);
int macosvfVMSetupStorage(virDomainDef *def, macosvfVMObject *vm);
int macosvfVMSetupNetwork(virDomainDef *def, macosvfVMObject *vm);
int macosvfVMSetupSerial(virDomainDef *def, macosvfVMObject *vm);
int macosvfVMSetupConsole(virDomainDef *def, macosvfVMObject *vm);
int macosvfVMSetupGraphics(virDomainDef *def, macosvfVMObject *vm);
int macosvfVMSetupInput(virDomainDef *def, macosvfVMObject *vm);
int macosvfVMSetupAudio(virDomainDef *def, macosvfVMObject *vm);

/* Statistics */
int macosvfVMGetCPUStats(macosvfVMObject *vm, unsigned long long *cpuTime);
int macosvfVMGetMemoryStats(macosvfVMObject *vm, unsigned long long *memoryUsed);
