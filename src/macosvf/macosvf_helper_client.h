/*
 * macosvf_helper_client.h - VZ Helper Client for macosvf driver
 *
 * This module handles communication with the virtmacosvf-helper process
 * via XPC. It launches the helper if needed and forwards VZ operations.
 */

#ifndef __MACOSVF_HELPER_CLIENT_H__
#define __MACOSVF_HELPER_CLIENT_H__

#include "macosvf_conf.h"

/* Opaque helper client structure */
typedef struct _macosvfHelperClient macosvfHelperClient;

/* Helper VM state (must match VZManager.h) */
typedef enum {
  MACOSVF_HELPER_VM_STATE_UNKNOWN = 0,
  MACOSVF_HELPER_VM_STATE_STOPPED,
  MACOSVF_HELPER_VM_STATE_RUNNING,
  MACOSVF_HELPER_VM_STATE_PAUSED,
  MACOSVF_HELPER_VM_STATE_ERROR,
  MACOSVF_HELPER_VM_STATE_STARTING,
  MACOSVF_HELPER_VM_STATE_STOPPING,
} macosvfHelperVMState;

/* Maximum number of disks supported */
#define MACOSVF_MAX_DISKS 8

/* Maximum number of port forwards supported */
#define MACOSVF_MAX_PORT_FORWARDS 16

/* Port forward configuration */
typedef struct {
  int hostPort;
  int guestPort;
  int protocol; /* 0 = TCP, 1 = UDP */
} macosvfPortForward;

/* VM configuration structure */
typedef struct {
  int cpuCount;
  unsigned long long memorySize;
  char *kernelPath;
  char *initrdPath;
  char *cmdline;
  char *ptyPath;
  int numDisks;
  char *diskPaths[MACOSVF_MAX_DISKS];
  bool useNetwork;
  int numPortForwards;
  macosvfPortForward portForwards[MACOSVF_MAX_PORT_FORWARDS];
  bool enableDisplay;
} macosvfHelperVMConfig;

/* Create/get the singleton helper client */
macosvfHelperClient *macosvfHelperClientGet(void);

/* Initialize the helper client */
int macosvfHelperClientInit(macosvfHelperClient *client);

/* Cleanup the helper client */
void macosvfHelperClientFree(macosvfHelperClient *client);

/* VM Lifecycle Operations */
int macosvfHelperCreateVM(macosvfHelperClient *client, const char *vmId,
                          const macosvfHelperVMConfig *config);

int macosvfHelperStartVM(macosvfHelperClient *client, const char *vmId);

int macosvfHelperStopVM(macosvfHelperClient *client, const char *vmId);

int macosvfHelperPauseVM(macosvfHelperClient *client, const char *vmId);

int macosvfHelperResumeVM(macosvfHelperClient *client, const char *vmId);

int macosvfHelperDestroyVM(macosvfHelperClient *client, const char *vmId);

/* VM Query Operations */
int macosvfHelperGetVMState(macosvfHelperClient *client, const char *vmId,
                            macosvfHelperVMState *state);

int macosvfHelperGetConsolePath(macosvfHelperClient *client, const char *vmId,
                                char **path);

/* Utility Operations */
int macosvfHelperPing(macosvfHelperClient *client);
int macosvfHelperListVMs(macosvfHelperClient *client, char ***vmIds,
                         size_t *count);

#endif /* __MACOSVF_HELPER_CLIENT_H__ */
