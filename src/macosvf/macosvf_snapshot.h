/*
 * macosvf_snapshot.h: macOS Virtualization.Framework domain snapshot operations
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

#include "internal.h"

virDomainSnapshotPtr macosvfDomainSnapshotCreateXML(virDomainPtr domain,
                                                     const char *xmlDesc,
                                                     unsigned int flags);

char *macosvfDomainSnapshotGetXMLDesc(virDomainSnapshotPtr snapshot,
                                      unsigned int flags);

int macosvfDomainSnapshotListNames(virDomainPtr domain,
                                   char **names,
                                   int nameslen,
                                   unsigned int flags);

int macosvfDomainSnapshotNum(virDomainPtr domain,
                             unsigned int flags);

virDomainSnapshotPtr macosvfDomainSnapshotLookupByName(virDomainPtr domain,
                                                       const char *name,
                                                       unsigned int flags);

int macosvfDomainHasCurrentSnapshot(virDomainPtr domain,
                                    unsigned int flags);

virDomainSnapshotPtr macosvfDomainSnapshotCurrent(virDomainPtr domain,
                                                  unsigned int flags);

int macosvfDomainSnapshotDelete(virDomainSnapshotPtr snapshot,
                                unsigned int flags);

int macosvfDomainListAllSnapshots(virDomainPtr domain,
                                  virDomainSnapshotPtr **snaps,
                                  unsigned int flags);

int macosvfDomainSnapshotNumChildren(virDomainSnapshotPtr snapshot,
                                     unsigned int flags);

int macosvfDomainSnapshotListChildrenNames(virDomainSnapshotPtr snapshot,
                                           char **names,
                                           int nameslen,
                                           unsigned int flags);

int macosvfDomainSnapshotIsCurrent(virDomainSnapshotPtr snapshot,
                                   unsigned int flags);

virDomainSnapshotPtr macosvfDomainSnapshotGetParent(virDomainSnapshotPtr snapshot,
                                                    unsigned int flags);

int macosvfDomainSnapshotListAllChildren(virDomainSnapshotPtr snapshot,
                                         virDomainSnapshotPtr **snaps,
                                         unsigned int flags);

int macosvfDomainRevertToSnapshot(virDomainSnapshotPtr snapshot,
                                  unsigned int flags);

int macosvfDomainSnapshotHasMetadata(virDomainSnapshotPtr snapshot,
                                     unsigned int flags);
