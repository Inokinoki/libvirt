/*
 * macosvf_conf.c: macosvf config handling
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

#include "macosvf_conf.h"
#include "macosvf_domain.h"
#include "domain_conf.h"
#include "viralloc.h"
#include "virclosecallbacks.h"
#include "virconf.h"
#include "virlog.h"
#include "virstring.h"
#include "configmake.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macOSvf_conf");

/* Class for macosvf driver config */
static virClass *macosvfDriverConfigClass;

static void
macosvfDriverConfigDispose(void *obj G_GNUC_UNUSED)
{
    /* Config cleanup will be implemented here */
}

static int
macosvfDriverConfigOnceInit(void)
{
    if (!VIR_CLASS_NEW(macosvfDriverConfig, virClassForObject()))
        return -1;

    return 0;

}

VIR_ONCE_GLOBAL_INIT(macosvfDriverConfig);

virCaps *
macosvfDriverGetCapabilities(macosvfConn *driver)
{
    if (!driver->caps) {
        driver->caps = macosvfCreateCapabilities();
    }
    return virObjectRef(driver->caps);
}

macosvfDriverConfig *
macosvfDriverConfigNew(void)
{
    macosvfDriverConfig *cfg;

    if (macosvfDriverConfigOnceInit() < 0)
        return NULL;

    if (!(cfg = virObjectNew(macosvfDriverConfigClass)))
        return NULL;

    /* Set default values */
    cfg->maxVcpus = 4;
    cfg->defaultMemory = 1024 * 1024; /* 1GB in KB */
    cfg->autoStart = false;

    return cfg;
}

macosvfDriverConfig *
macosvfDriverGetConfig(macosvfConn *driver)
{
    return virObjectRef(driver->config);
}

int
macosvfDriverLoadConfig(macosvfDriverConfig *cfg,
                         const char *filename)
{
    virConf *conf = NULL;
    int ret = -1;

    if (!filename) {
        VIR_INFO("No config file specified, using defaults");
        return 0;
    }

    if (!(conf = virConfReadFile(filename, 0))) {
        VIR_INFO("Failed to load config file %s, using defaults", filename);
        return 0;
    }

    if (virConfGetValue(conf, "max_vcpus")) {
        if (virConfGetValueUInt(conf, "max_vcpus", &cfg->maxVcpus) < 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Invalid max_vcpus setting"));
            goto cleanup;
        }
    }

    if (virConfGetValue(conf, "default_memory")) {
        unsigned long long memoryMB;
        if (virConfGetValueULLong(conf, "default_memory", &memoryMB) < 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Invalid default_memory setting"));
            goto cleanup;
        }
        cfg->defaultMemory = memoryMB * 1024; /* Convert MB to KB */
    }

    if (virConfGetValue(conf, "autostart")) {
        bool autoStart;
        if (virConfGetValueBool(conf, "autostart", &autoStart) < 0) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Invalid autostart setting"));
            goto cleanup;
        }
        cfg->autoStart = autoStart;
    }

    ret = 0;

cleanup:
    virConfFree(conf);
    return ret;
}

virDomainXMLOption *
virMacOSVFDriverCreateXMLConf(macosvfConn *driver)
{
    virDomainXMLOption *ret = NULL;

    /* Set the opaque pointer for the post-parse callback */
    virMacOSVFDriverDomainDefParserConfig.priv = driver;

    ret = virDomainXMLOptionNew(&virMacOSVFDriverDomainDefParserConfig,
                                &macosvfDriverPrivateDataCallbacks,
                                NULL, NULL, NULL, NULL);

    virDomainXMLOptionSetCloseCallbackAlloc(ret, virCloseCallbacksDomainAlloc);

    return ret;
}
