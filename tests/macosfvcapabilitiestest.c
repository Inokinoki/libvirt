/*
 * macosfvcapabilitiestest.c: test macOS Virtualization.Framework capabilities
 *
 * Copyright (C) 2025
 */

#include <config.h>
#include "testutils.h"

#ifdef WITH_MACOSVF
# include "macosvf/macosvf_capabilities.h"

# define VIR_FROM_THIS VIR_FROM_NONE

static int
testCapabilities(const void *data G_GNUC_UNUSED)
{
    g_autoptr(virCaps) caps = NULL;

    virTestSetHostArch(VIR_ARCH_AARCH64);

    if ((caps = macosvfCreateCapabilities()) == NULL) {
        fprintf(stderr, "Failed to create capabilities\n");
        return -1;
    }

    if (caps->host.arch != VIR_ARCH_AARCH64) {
        fprintf(stderr, "Expected host arch AARCH64, got %s\n",
                virArchToString(caps->host.arch));
        return -1;
    }

    if (caps->guests == NULL || caps->nguests < 1) {
        fprintf(stderr, "No guests defined in capabilities\n");
        return -1;
    }

    virCapsGuest *guest = &caps->guests[0];
    if (guest->os.type != VIR_DOMAIN_OSTYPE_HVM) {
        fprintf(stderr, "Expected guest os type HVM, got %s\n",
                virDomainOSTypeToString(guest->os.type));
        return -1;
    }

    if (guest->arch != VIR_ARCH_AARCH64) {
        fprintf(stderr, "Expected guest arch AARCH64, got %s\n",
                virArchToString(guest->arch));
        return -1;
    }

    if (guest->archInfo.ndomains < 1) {
        fprintf(stderr, "No domains defined for guest\n");
        return -1;
    }

    if (guest->archInfo.domains[0].type != VIR_DOMAIN_VIRT_MACOSVF) {
        fprintf(stderr, "Expected domain type MACOSVF, got %s\n",
                virDomainVirtTypeToString(guest->archInfo.domains[0].type));
        return -1;
    }

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
