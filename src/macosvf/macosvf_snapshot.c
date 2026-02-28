/*
 * macosvf_snapshot.c: macOS Virtualization.Framework domain snapshot operations
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

#include "virerror.h"
#include "datatypes.h"
#include "viralloc.h"
#include "virlog.h"
#include "virstring.h"
#include "virfile.h"
#include "virdomainmomentobjlist.h"
#include "snapshot_conf.h"
#include "viraccessapicheck.h"
#include "viruuid.h"
#include "conf/domain_conf.h"

#include "macosvf_driver.h"
#include "macosvf_domain.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macOSvf_snapshot");

/* Helper to get domain object from snapshot */
static virDomainObj *
macosvfDomObjFromSnapshot(virDomainSnapshotPtr snapshot)
{
    return macosvfDomObjFromDomain(snapshot->domain);
}

/* Generate snapshot directory path */
static char *
macosvfSnapshotDir(virDomainObj *vm)
{
    macosvfConn *driver = vm->conn->privateData;
    return g_strdup_printf("%s/%s", driver->snapshotDir, vm->def->name);
}

/* Generate snapshot file path */
static char *
macosvfSnapshotFile(virDomainObj *vm,
                   const char *name)
{
    g_autofree char *snapDir = macosvfSnapshotDir(vm);
    return g_strdup_printf("%s/%s.xml", snapDir, name);
}

/* Ensure snapshot directory exists */
static int
macosvfEnsureSnapshotDir(virDomainObj *vm)
{
    g_autofree char *snapDir = macosvfSnapshotDir(vm);

    if (!virFileExists(snapDir)) {
        if (g_mkdir_with_parents(snapDir, 0700) < 0) {
            virReportSystemError(errno,
                                 _("Cannot create snapshot directory %1$s"),
                                 snapDir);
            return -1;
        }
    }

    return 0;
}

/* Write snapshot metadata to disk */
static int
macosvfSnapshotWriteMetadata(virDomainObj *vm,
                             virDomainMomentObj *snap)
{
    g_autofree char *snapFile = NULL;
    g_autofree char *xml = NULL;
    g_autofree char *uuidstr = NULL;
    virDomainDef *def = vm->def;

    snapFile = macosvfSnapshotFile(vm, snap->def->name);

    uuidstr = g_new0(char, VIR_UUID_STRING_BUFLEN);
    virUUIDFormat(def->uuid, uuidstr);

    xml = virDomainSnapshotDefFormat(uuidstr, (virDomainSnapshotDef *) snap->def,
                                     virDomainObjGetXMLOpt(vm),
                                     VIR_DOMAIN_SNAPSHOT_FORMAT_INTERNAL);
    if (!xml)
        return -1;

    if (virFileWriteStr(snapFile, xml, 0600) < 0) {
        virReportSystemError(errno,
                             _("Cannot write snapshot file %1$s"),
                             snapFile);
        return -1;
    }

    VIR_DEBUG("Wrote snapshot '%s' metadata to %s", snap->def->name, snapFile);
    return 0;
}

/* Delete snapshot metadata from disk */
static int
macosvfSnapshotDeleteMetadata(virDomainObj *vm,
                              virDomainMomentObj *snap)
{
    g_autofree char *snapFile = NULL;

    snapFile = macosvfSnapshotFile(vm, snap->def->name);

    if (virFileExists(snapFile)) {
        if (unlink(snapFile) < 0) {
            virReportSystemError(errno,
                                 _("Cannot delete snapshot file %1$s"),
                                 snapFile);
            return -1;
        }
    }

    VIR_DEBUG("Deleted snapshot '%s' metadata from %s", snap->def->name, snapFile);
    return 0;
}

/* Create a domain snapshot */
static virDomainSnapshotPtr
macosvfDomainSnapshotCreateXML(virDomainPtr domain,
                               const char *xmlDesc,
                               unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    virDomainSnapshotPtr snapshot = NULL;
    g_autoptr(virDomainSnapshotDef) def = NULL;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_CREATE_VALIDATE |
                  VIR_DOMAIN_SNAPSHOT_CREATE_REDEFINE, NULL);

    if (!(vm = macosvfDomObjFromDomain(domain)))
        return NULL;

    if (virDomainSnapshotCreateXMLEnsureACL(domain->conn, vm->def, flags) < 0)
        goto cleanup;

    /* Parse snapshot XML */
    def = virDomainSnapshotDefParseString(xmlDesc,
                                          virDomainObjGetXMLOpt(vm),
                                          NULL, NULL,
                                          VIR_DOMAIN_SNAPSHOT_PARSE_VALIDATE);
    if (!def)
        goto cleanup;

    /* Create snapshot directory if needed */
    if (macosvfEnsureSnapshotDir(vm) < 0)
        goto cleanup;

    /* Create snapshot object */
    snap = virDomainSnapshotObjListAdd(vm->snapshots, &def->parent,
                                       VIR_DOMAIN_SNAPSHOT_LIST_INTERNAL,
                                       -1, NULL);
    if (!snap) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Failed to create snapshot object for '%1$s'"),
                       def->parent.name);
        goto cleanup;
    }
    def = NULL; /* Ownership transferred to snapshot */

    /* Set snapshot state based on current VM state */
    ((virDomainSnapshotDef *) snap->def)->state = virDomainObjGetState(vm, NULL);

    /* Write snapshot metadata to disk */
    if (macosvfSnapshotWriteMetadata(vm, snap) < 0) {
        virDomainMomentObjListRemove(vm->snapshots, snap);
        goto cleanup;
    }

    snapshot = virGetDomainSnapshot(domain, snap->def->name);
    VIR_INFO("Created snapshot '%s' for domain '%s'", snap->def->name, vm->def->name);

cleanup:
    virDomainObjEndAPI(&vm);
    return snapshot;
}

/* Get snapshot XML description */
static char *
macosvfDomainSnapshotGetXMLDesc(virDomainSnapshotPtr snapshot,
                                unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    char *xml = NULL;
    g_autofree char *uuidstr = NULL;
    virDomainSnapshotDef *snapdef;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_XML_SECURE, NULL);

    if (!(vm = macosvfDomObjFromSnapshot(snapshot)))
        return NULL;

    if (virDomainSnapshotGetXMLDescEnsureACL(snapshot->domain->conn, vm->def, flags) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjFindByName(vm->snapshots, snapshot->name);
    if (!snap) {
        virReportError(VIR_ERR_NO_DOMAIN_SNAPSHOT,
                       _("no snapshot with matching name '%1$s'"),
                       snapshot->name);
        goto cleanup;
    }

    snapdef = (virDomainSnapshotDef *) snap->def;

    uuidstr = g_new0(char, VIR_UUID_STRING_BUFLEN);
    virUUIDFormat(vm->def->uuid, uuidstr);

    xml = virDomainSnapshotDefFormat(uuidstr, snapdef,
                                     virDomainObjGetXMLOpt(vm),
                                     VIR_DOMAIN_SNAPSHOT_FORMAT_SECURE |
                                     (flags & VIR_DOMAIN_SNAPSHOT_XML_SECURE ?
                                      VIR_DOMAIN_SNAPSHOT_FORMAT_SECURE : 0));

cleanup:
    virDomainObjEndAPI(&vm);
    return xml;
}

/* List snapshot names */
static int
macosvfDomainSnapshotListNames(virDomainPtr domain,
                               char **names,
                               int nameslen,
                               unsigned int flags)
{
    virDomainObj *vm = NULL;
    int n = -1;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_LIST_ROOTS |
                  VIR_DOMAIN_SNAPSHOT_LIST_DESCENDANTS |
                  VIR_DOMAIN_SNAPSHOT_LIST_LEAVES |
                  VIR_DOMAIN_SNAPSHOT_LIST_NO_METADATA |
                  VIR_DOMAIN_SNAPSHOT_LIST_EXTERNAL, -1);

    if (!(vm = macosvfDomObjFromDomain(domain)))
        return -1;

    if (virDomainSnapshotListNamesEnsureACL(domain->conn, vm->def) < 0)
        goto cleanup;

    n = virDomainSnapshotObjListGetNames(vm->snapshots, NULL, names, nameslen, flags);
    if (n < 0)
        goto cleanup;

cleanup:
    virDomainObjEndAPI(&vm);
    return n;
}

/* Get number of snapshots */
static int
macosvfDomainSnapshotNum(virDomainPtr domain,
                         unsigned int flags)
{
    virDomainObj *vm = NULL;
    int n = -1;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_LIST_ROOTS |
                  VIR_DOMAIN_SNAPSHOT_LIST_DESCENDANTS |
                  VIR_DOMAIN_SNAPSHOT_LIST_LEAVES |
                  VIR_DOMAIN_SNAPSHOT_LIST_NO_METADATA |
                  VIR_DOMAIN_SNAPSHOT_LIST_EXTERNAL, -1);

    if (!(vm = macosvfDomObjFromDomain(domain)))
        return -1;

    if (virDomainSnapshotNumEnsureACL(domain->conn, vm->def) < 0)
        goto cleanup;

    n = virDomainSnapshotObjListNum(vm->snapshots, NULL, flags);

cleanup:
    virDomainObjEndAPI(&vm);
    return n;
}

/* Lookup snapshot by name */
static virDomainSnapshotPtr
macosvfDomainSnapshotLookupByName(virDomainPtr domain,
                                  const char *name,
                                  unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    virDomainSnapshotPtr snapshot = NULL;

    virCheckFlags(0, NULL);

    if (!(vm = macosvfDomObjFromDomain(domain)))
        return NULL;

    if (virDomainSnapshotLookupByNameEnsureACL(domain->conn, vm->def) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjFindByName(vm->snapshots, name);
    if (!snap) {
        virReportError(VIR_ERR_NO_DOMAIN_SNAPSHOT,
                       _("no snapshot with matching name '%1$s'"), name);
        goto cleanup;
    }

    snapshot = virGetDomainSnapshot(domain, name);

cleanup:
    virDomainObjEndAPI(&vm);
    return snapshot;
}

/* Check if domain has a current snapshot */
static int
macosvfDomainHasCurrentSnapshot(virDomainPtr domain,
                                unsigned int flags)
{
    virDomainObj *vm = NULL;
    int current = -1;

    virCheckFlags(0, -1);

    if (!(vm = macosvfDomObjFromDomain(domain)))
        return -1;

    if (virDomainHasCurrentSnapshotEnsureACL(domain->conn, vm->def) < 0)
        goto cleanup;

    current = virDomainSnapshotObjListNum(vm->snapshots, NULL, 0) > 0;

cleanup:
    virDomainObjEndAPI(&vm);
    return current;
}

/* Get current snapshot */
static virDomainSnapshotPtr
macosvfDomainSnapshotCurrent(virDomainPtr domain,
                             unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    virDomainSnapshotPtr snapshot = NULL;

    virCheckFlags(0, NULL);

    if (!(vm = macosvfDomObjFromDomain(domain)))
        return NULL;

    if (virDomainSnapshotCurrentEnsureACL(domain->conn, vm->def) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjGetCurrent(vm->snapshots);
    if (!snap)
        goto cleanup;

    snapshot = virGetDomainSnapshot(domain, snap->def->name);

cleanup:
    virDomainObjEndAPI(&vm);
    return snapshot;
}

/* Delete a snapshot */
static int
macosvfDomainSnapshotDelete(virDomainSnapshotPtr snapshot,
                            unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    int ret = -1;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_DELETE_CHILDREN |
                  VIR_DOMAIN_SNAPSHOT_DELETE_CHILDREN_ONLY, -1);

    if (!(vm = macosvfDomObjFromSnapshot(snapshot)))
        return -1;

    if (virDomainSnapshotDeleteEnsureACL(snapshot->domain->conn, vm->def) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjFindByName(vm->snapshots, snapshot->name);
    if (!snap) {
        virReportError(VIR_ERR_NO_DOMAIN_SNAPSHOT,
                       _("no snapshot with matching name '%1$s'"),
                       snapshot->name);
        goto cleanup;
    }

    /* Delete snapshot metadata from disk */
    if (macosvfSnapshotDeleteMetadata(vm, snap) < 0)
        goto cleanup;

    /* Remove snapshot from list */
    virDomainMomentObjListRemove(vm->snapshots, snap);

    VIR_INFO("Deleted snapshot '%s' for domain '%s'", snapshot->name, vm->def->name);
    ret = 0;

cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

/* Get all snapshots */
static int
macosvfDomainListAllSnapshots(virDomainPtr domain,
                              virDomainSnapshotPtr **snaps,
                              unsigned int flags)
{
    virDomainObj *vm = NULL;
    int n = -1;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_LIST_ROOTS |
                  VIR_DOMAIN_SNAPSHOT_LIST_DESCENDANTS |
                  VIR_DOMAIN_SNAPSHOT_LIST_LEAVES |
                  VIR_DOMAIN_SNAPSHOT_LIST_NO_METADATA |
                  VIR_DOMAIN_SNAPSHOT_LIST_EXTERNAL, -1);

    if (!(vm = macosvfDomObjFromDomain(domain)))
        return -1;

    if (virDomainListAllSnapshotsEnsureACL(domain->conn, vm->def) < 0)
        goto cleanup;

    n = virDomainListSnapshots(vm->snapshots, NULL, domain, snaps, flags);
    if (n < 0)
        goto cleanup;

cleanup:
    virDomainObjEndAPI(&vm);
    return n;
}

/* Get number of snapshot children */
static int
macosvfDomainSnapshotNumChildren(virDomainSnapshotPtr snapshot,
                                 unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    int n = -1;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_LIST_DESCENDANTS |
                  VIR_DOMAIN_SNAPSHOT_LIST_LEAVES, -1);

    if (!(vm = macosvfDomObjFromSnapshot(snapshot)))
        return -1;

    if (virDomainSnapshotNumChildrenEnsureACL(snapshot->domain->conn, vm->def) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjFindByName(vm->snapshots, snapshot->name);
    if (!snap) {
        virReportError(VIR_ERR_NO_DOMAIN_SNAPSHOT,
                       _("no snapshot with matching name '%1$s'"),
                       snapshot->name);
        goto cleanup;
    }

    n = virDomainSnapshotObjListNum(vm->snapshots, snap, flags);

cleanup:
    virDomainObjEndAPI(&vm);
    return n;
}

/* List snapshot children names */
static int
macosvfDomainSnapshotListChildrenNames(virDomainSnapshotPtr snapshot,
                                       char **names,
                                       int nameslen,
                                       unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    int n = -1;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_LIST_DESCENDANTS |
                  VIR_DOMAIN_SNAPSHOT_LIST_LEAVES |
                  VIR_DOMAIN_SNAPSHOT_LIST_NO_METADATA, -1);

    if (!(vm = macosvfDomObjFromSnapshot(snapshot)))
        return -1;

    if (virDomainSnapshotListChildrenNamesEnsureACL(snapshot->domain->conn, vm->def) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjFindByName(vm->snapshots, snapshot->name);
    if (!snap) {
        virReportError(VIR_ERR_NO_DOMAIN_SNAPSHOT,
                       _("no snapshot with matching name '%1$s'"),
                       snapshot->name);
        goto cleanup;
    }

    n = virDomainSnapshotObjListGetNames(vm->snapshots, snap, names, nameslen,
                                         flags);
    if (n < 0)
        goto cleanup;

cleanup:
    virDomainObjEndAPI(&vm);
    return n;
}

/* Check if snapshot is current */
static int
macosvfDomainSnapshotIsCurrent(virDomainSnapshotPtr snapshot,
                               unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    int current = 0;

    virCheckFlags(0, -1);

    if (!(vm = macosvfDomObjFromSnapshot(snapshot)))
        return -1;

    if (virDomainSnapshotIsCurrentEnsureACL(snapshot->domain->conn, vm->def) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjGetCurrent(vm->snapshots);
    if (snap && STREQ(snap->def->name, snapshot->name))
        current = 1;

cleanup:
    virDomainObjEndAPI(&vm);
    return current;
}

/* Get snapshot parent */
static virDomainSnapshotPtr
macosvfDomainSnapshotGetParent(virDomainSnapshotPtr snapshot,
                               unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    virDomainMomentObj *parent = NULL;
    virDomainSnapshotPtr parentSnapshot = NULL;

    virCheckFlags(0, NULL);

    if (!(vm = macosvfDomObjFromSnapshot(snapshot)))
        return NULL;

    if (virDomainSnapshotGetParentEnsureACL(snapshot->domain->conn, vm->def) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjFindByName(vm->snapshots, snapshot->name);
    if (!snap) {
        virReportError(VIR_ERR_NO_DOMAIN_SNAPSHOT,
                       _("no snapshot with matching name '%1$s'"),
                       snapshot->name);
        goto cleanup;
    }

    parent = virDomainSnapshotObjGetParent(snap);
    if (parent)
        parentSnapshot = virGetDomainSnapshot(snapshot->domain, parent->def->name);

cleanup:
    virDomainObjEndAPI(&vm);
    return parentSnapshot;
}

/* Get all snapshot children */
static int
macosvfDomainSnapshotListAllChildren(virDomainSnapshotPtr snapshot,
                                     virDomainSnapshotPtr **snaps,
                                     unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    int n = -1;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_LIST_DESCENDANTS |
                  VIR_DOMAIN_SNAPSHOT_LIST_LEAVES |
                  VIR_DOMAIN_SNAPSHOT_LIST_NO_METADATA, -1);

    if (!(vm = macosvfDomObjFromSnapshot(snapshot)))
        return -1;

    if (virDomainSnapshotListAllChildrenEnsureACL(snapshot->domain->conn, vm->def) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjFindByName(vm->snapshots, snapshot->name);
    if (!snap) {
        virReportError(VIR_ERR_NO_DOMAIN_SNAPSHOT,
                       _("no snapshot with matching name '%1$s'"),
                       snapshot->name);
        goto cleanup;
    }

    n = virDomainListSnapshots(vm->snapshots, snap, snapshot->domain, snaps, flags);
    if (n < 0)
        goto cleanup;

cleanup:
    virDomainObjEndAPI(&vm);
    return n;
}

/* Revert to snapshot */
static int
macosvfDomainRevertToSnapshot(virDomainSnapshotPtr snapshot,
                              unsigned int flags)
{
    virDomainObj *vm = NULL;
    virDomainMomentObj *snap = NULL;
    int ret = -1;

    virCheckFlags(VIR_DOMAIN_SNAPSHOT_REVERT_RUNNING |
                  VIR_DOMAIN_SNAPSHOT_REVERT_PAUSED |
                  VIR_DOMAIN_SNAPSHOT_REVERT_FORCE, -1);

    if (!(vm = macosvfDomObjFromSnapshot(snapshot)))
        return -1;

    if (virDomainRevertToSnapshotEnsureACL(snapshot->domain->conn, vm->def) < 0)
        goto cleanup;

    snap = virDomainSnapshotObjFindByName(vm->snapshots, snapshot->name);
    if (!snap) {
        virReportError(VIR_ERR_NO_DOMAIN_SNAPSHOT,
                       _("no snapshot with matching name '%1$s'"),
                       snapshot->name);
        goto cleanup;
    }

    /* For macOSVF, snapshots are metadata-only (domain configuration)
     * We can restore the domain configuration but not VM state
     * This is similar to undefining and redefining with snapshot XML */

    /* Check if VM is running */
    if (virDomainObjIsActive(vm)) {
        if (!(flags & VIR_DOMAIN_SNAPSHOT_REVERT_FORCE)) {
            virReportError(VIR_ERR_OPERATION_INVALID,
                           _("Cannot revert to snapshot while domain is running"));
            goto cleanup;
        }
        /* Force revert: destroy VM first */
        /* Note: This would require VM destruction and recreation */
        /* For now, return an error indicating this isn't fully supported */
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED,
                       _("Reverting to snapshot while VM is running is not supported"));
        goto cleanup;
    }

    /* Restore domain configuration from snapshot
     * This would replace the current domain definition with the snapshot's definition
     * For now, we report this as a limitation */
    virReportError(VIR_ERR_OPERATION_UNSUPPORTED,
                   _("Snapshot revert is not yet fully implemented. "
                     "Snapshots store metadata only, not VM state."));

cleanup:
    virDomainObjEndAPI(&vm);
    return ret;
}

/* Check if snapshot has metadata */
static int
macosvfDomainSnapshotHasMetadata(virDomainSnapshotPtr snapshot G_GNUC_UNUSED,
                                 unsigned int flags G_GNUC_UNUSED)
{
    /* All macOSVF snapshots have metadata */
    return 1;
}
