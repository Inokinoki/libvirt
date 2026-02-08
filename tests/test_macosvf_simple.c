#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include "macosvf/macosvf_capabilities.h"
#include "macosvf/macosvf_conf.h"

int main(void) {
    macosvfConn driver;
    virCaps *caps = NULL;
    virDomainXMLOption *xmlopt = NULL;

    printf("Testing macOSVF capabilities creation...\n");

    caps = macosvfCreateCapabilities();
    if (!caps) {
        printf("FAIL: Could not create capabilities\n");
        return 1;
    }
    printf("SUCCESS: Capabilities created\n");

    printf("Capabilities host arch: %s\n",
           virArchToString(caps->host.arch));

    virObjectUnref(caps);
    return 0;
}
