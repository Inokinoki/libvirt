#include <config.h>
#include "testutils.h"

#ifdef WITH_MACOSVF
# include "macosvf/macosvf_capabilities.h"

static int
testCapabilities(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virCaps) caps = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    if (!(caps = macosvfCreateCapabilities()))
        return -1;

    if (caps->host.arch != VIR_ARCH_AARCH64)
        return -1;

    if (!caps->guests || caps->nguests < 1)
        return -1;

    return 0;
}

static int
mymain(void)
{
    int ret = 0;

    if (virTestRun("MACOSVF Capabilities", testCapabilities, NULL) < 0)
        ret = -1;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

VIR_TEST_MAIN(mymain)

#else
int main(void) { return EXIT_AM_SKIP; }
#endif
