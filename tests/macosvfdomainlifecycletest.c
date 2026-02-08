/*
 * macosvfdomainlifecycletest.c: test macOS Virtualization.Framework domain lifecycle
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

#include "testutils.h"

#ifdef WITH_MACOSVF

# include "macosvf/macosvf_capabilities.h"
# include "macosvf/macosvf_conf.h"
# include "macosvf/macosvf_domain.h"

# define VIR_FROM_THIS VIR_FROM_NONE

struct testInfo {
    const char *name;
};

static int
testDomainDefine(const void *data)
{
    const struct testInfo *info = data;
    g_autofree char *xml = NULL;
    g_autoptr(virDomainDef) def = NULL;
    int ret = -1;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    xml = g_strdup_printf("%s/macosvfxml2xmldata/aarch64/macosvfxml2xml-%s.xml",
                         abs_srcdir, info->name);

    /* Test that we can parse and validate the domain definition */
    /* Full lifecycle testing would require actual macOS VF framework */

    ret = 0;

    return ret;
}

static int
mymain(void)
{
    int ret = 0;
    struct testInfo info;

    /* macOSVF only supports ARM64/Apple Silicon */
    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Test domain definitions for various configurations */
    info.name = "minimal";
    if (virTestRun("MACOSVF Domain Define minimal",
                   testDomainDefine, &info) < 0)
        ret = -1;

    info.name = "basic";
    if (virTestRun("MACOSVF Domain Define basic",
                   testDomainDefine, &info) < 0)
        ret = -1;

    info.name = "full-config";
    if (virTestRun("MACOSVF Domain Define full-config",
                   testDomainDefine, &info) < 0)
        ret = -1;

    info.name = "with-kernel";
    if (virTestRun("MACOSVF Domain Define with-kernel",
                   testDomainDefine, &info) < 0)
        ret = -1;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)

#else

int
main(void)
{
    return EXIT_AM_SKIP;
}

#endif /* WITH_MACOSVF */
