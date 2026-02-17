#include <config.h>
#include <testutils.h>
#include <libvirt/libvirt.h>

static int
testDefineUbuntuVM(const void *opaque G_GNUC_UNUSED)
{
    virConnectPtr conn = NULL;
    virDomainPtr dom = NULL;
    char *xml = NULL;
    int ret = -1;

    /* Read XML file */
    if (virFileReadAll("/Users/inoki/Builds/srcs/libvirt/ubuntu-test-vm.xml",
                       1024 * 100, &xml) < 0) {
        fprintf(stderr, "Failed to read XML file\n");
        return -1;
    }

    /* Connect to macosvf */
    conn = virConnectOpen("macosvf:///session");
    if (!conn) {
        fprintf(stderr, "Failed to connect to macosvf:///session\n");
        VIR_FREE(xml);
        return -1;
    }

    printf("Connected to macosvf driver\n");

    /* Define domain */
    dom = virDomainDefineXML(conn, xml);
    if (!dom) {
        fprintf(stderr, "Failed to define domain\n");
        goto cleanup;
    }

    printf("Domain defined successfully: %s\n", virDomainGetName(dom));
    ret = 0;

cleanup:
    virObjectUnref(dom);
    virObjectUnref(conn);
    VIR_FREE(xml);
    return ret;
}

int
main(void)
{
    int ret = 0;

    if (virTestRun("Define Ubuntu VM", testDefineUbuntuVM, NULL) < 0)
        ret = -1;

    return ret;
}
