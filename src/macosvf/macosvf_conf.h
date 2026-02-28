/*
 * macosvf_conf.h: macosvf config file
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

#include "virconf.h"
#include "virthread.h"
#include "virobject.h"
#include "virdomainobjlist.h"
#include "domain_event.h"
#include "domain_conf.h"

typedef struct _macosvfConn macosvfConn;
typedef struct _macosvfDriverConfig macosvfDriverConfig;

struct _macosvfDriverConfig {
    virObject parent;
    char *configBaseDir;
    char *stateDir;
    char *logDir;
    unsigned int maxVcpus;
    unsigned long defaultMemory;
    bool autoStart;
};

struct _macosvfConn {
    virMutex lock;

    virCaps *caps;
    macosvfDriverConfig *config;
    virDomainObjList *domains;
    virDomainXMLOption *xmlopt;
    virObjectEventState *domainEventState;

    char *stateDir;
    char *configDir;
    char *logDir;
    char *pidFile;

    bool privileged;
    int lastvmid; /* For allocating domain IDs */
};

virCaps *macosvfCreateCapabilities(void);
virCaps *macosvfDriverGetCapabilities(macosvfConn *driver);
macosvfDriverConfig *macosvfDriverConfigNew(void);
macosvfDriverConfig *macosvfDriverGetConfig(macosvfConn *driver);
int macosvfDriverLoadConfig(macosvfDriverConfig *cfg, const char *filename);
virDomainXMLOption *virMacOSVFDriverCreateXMLConf(macosvfConn *driver);
