/*
 * Simple program to regenerate macosvf XML test output files
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#ifdef WITH_MACOSVF
# include "macosvf/macosvf_capabilities.h"
# include "macosvf/macosvf_domain.h"
# include "macosvf/macosvf_conf.h"

int main(int argc, char **argv)
{
    macosvfConn driver;
    virCaps *caps = NULL;
    virDomainXMLOption *xmlopt = NULL;
    virDomainDef *def = NULL;
    char *xml_in = NULL;
    char *xml_out = NULL;
    const char *infile = NULL;
    const char *outfile = NULL;
    int ret = 0;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.xml> <output.xml>\n", argv[0]);
        return 1;
    }

    infile = argv[1];
    outfile = argv[2];

    /* Set architecture to ARM64 */
    virTestSetHostArch(VIR_ARCH_AARCH64);

    /* Create capabilities */
    if ((caps = macosvfCreateCapabilities()) == NULL) {
        fprintf(stderr, "Failed to create capabilities\n");
        return 1;
    }

    /* Create XML config */
    if ((xmlopt = virMacOSVFDriverCreateXMLConf(&driver)) == NULL) {
        fprintf(stderr, "Failed to create XML config\n");
        ret = 1;
        goto cleanup;
    }

    /* Read input file */
    if (virFileReadAll(infile, 1024 * 1024, &xml_in) < 0) {
        fprintf(stderr, "Failed to read input file\n");
        ret = 1;
        goto cleanup;
    }

    /* Parse domain definition */
    if ((def = virDomainDefParseString(xml_in, caps, xmlopt,
                                       VIR_DOMAIN_DEF_PARSE_INACTIVE)) == NULL) {
        fprintf(stderr, "Failed to parse domain XML\n");
        ret = 1;
        goto cleanup;
    }

    /* Format domain definition */
    if ((xml_out = virDomainDefFormat(def, xmlopt, VIR_DOMAIN_DEF_FORMAT_SECURE)) == NULL) {
        fprintf(stderr, "Failed to format domain XML\n");
        ret = 1;
        goto cleanup;
    }

    /* Write output file */
    if (virFileWriteStr(outfile, xml_out, 0644) < 0) {
        fprintf(stderr, "Failed to write output file\n");
        ret = 1;
        goto cleanup;
    }

    fprintf(stderr, "Generated: %s\n", outfile);

cleanup:
    VIR_FREE(xml_in);
    VIR_FREE(xml_out);
    virObjectUnref(def);
    virObjectUnref(xmlopt);
    virObjectUnref(caps);
    return ret;
}

#else
int main(void) { return 1; }
#endif
