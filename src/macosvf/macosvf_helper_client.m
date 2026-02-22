/*
 * macosvf_helper_client.c - VZ Helper Client Implementation
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <spawn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>

#include <Foundation/Foundation.h>
#include <Cocoa/Cocoa.h>
#include <ServiceManagement/ServiceManagement.h>
#include <Security/Security.h>

#include "macosvf_helper_client.h"
#include "virlog.h"
#include "virerror.h"

#define VIR_FROM_THIS VIR_FROM_MACOSVF

VIR_LOG_INIT("macosvf.macosvf_helper_client");

/* Helper macro for dispatch_release - not needed on macOS 10.12+ with ARC */
#define DISPATCH_RELEASE(obj) /* No-op - dispatch_release handled by system */

/* XPC Mach port name */
#define XPC_MACH_PORT_NAME @"org.libvirt.virtmacosvf-helper"

/* Helper label for SMJobBless */
#define HELPER_LABEL "org.libvirt.virtmacosvf-helper"

/* Helper client structure */
struct _macosvfHelperClient {
    NSXPCConnection *connection;
    pid_t helperPid;
    int refCount;
};

/* Singleton instance */
static macosvfHelperClient *singletonClient = NULL;

/* Global run loop thread for XPC callbacks */
static pthread_t xpcRunLoopThread;
static CFRunLoopRef xpcRunLoop = NULL;
static BOOL runLoopReady = NO;

/* Forward declaration of XPC protocol */
@protocol VZXPCProtocol <NSObject>
- (void)createVMWithId:(NSString *)vmId
                config:(NSDictionary *)config
                 reply:(void (^)(BOOL success, NSError *error))reply;
- (void)startVMWithId:(NSString *)vmId
                reply:(void (^)(BOOL success, NSError *error))reply;
- (void)stopVMWithId:(NSString *)vmId
               reply:(void (^)(BOOL success, NSError *error))reply;
- (void)pauseVMWithId:(NSString *)vmId
                reply:(void (^)(BOOL success, NSError *error))reply;
- (void)resumeVMWithId:(NSString *)vmId
                 reply:(void (^)(BOOL success, NSError *error))reply;
- (void)getStateForVMWithId:(NSString *)vmId
                      reply:(void (^)(NSInteger state, NSError *error))reply;
- (void)getConsolePathForVMWithId:(NSString *)vmId
                            reply:(void (^)(NSString *path, NSError *error))reply;
- (void)listVMsWithReply:(void (^)(NSArray *vmIds, NSError *error))reply;
- (void)destroyVMWithId:(NSString *)vmId
                  reply:(void (^)(BOOL success, NSError *error))reply;
- (void)pingWithReply:(void (^)(NSString *pong))reply;
@end

/* Run loop thread function for XPC callbacks */
static void *xpcRunLoopFunc(void *arg) {
    @autoreleasepool {
        xpcRunLoop = CFRunLoopGetCurrent();

        runLoopReady = YES;

        VIR_INFO("XPC run loop thread started");

        /* Run the run loop indefinitely */
        CFRunLoopRun();

        VIR_INFO("XPC run loop thread stopping");
    }
    return NULL;
}

/* Initialize the XPC run loop thread */
static int xpcRunLoopInit(void) {
    if (pthread_create(&xpcRunLoopThread, NULL, xpcRunLoopFunc, NULL) != 0) {
        VIR_ERROR("Failed to create XPC run loop thread");
        return -1;
    }

    /* Wait for run loop to be ready */
    int timeout = 50; /* 5 seconds */
    while (!runLoopReady && timeout > 0) {
        usleep(100000); /* 100ms */
        timeout--;
    }

    if (!runLoopReady) {
        VIR_ERROR("XPC run loop thread failed to start");
        return -1;
    }

    VIR_INFO("XPC run loop thread initialized");
    return 0;
}

/* Cleanup the XPC run loop thread */
static void xpcRunLoopCleanup(void) {
    if (!runLoopReady) {
        return;
    }

    runLoopReady = NO;

    /* Give pending XPC operations time to complete */
    usleep(100000); /* 100ms */

    /* Stop the run loop - must be called from the run loop thread */
    if (xpcRunLoop) {
        /* Use CFRunLoopPerformBlock to ensure CFRunLoopStop is called
         * from the same thread that owns the run loop */
        CFRunLoopPerformBlock(xpcRunLoop, kCFRunLoopDefaultMode, ^{
            CFRunLoopStop(xpcRunLoop);
        });
        CFRunLoopWakeUp(xpcRunLoop);
        xpcRunLoop = NULL;
    }

    /* Wait for thread to exit - use pthread_join on macOS */
    pthread_join(xpcRunLoopThread, NULL);
    VIR_INFO("XPC run loop thread cleaned up");
}

/* Install and launch the helper using SMJobBless */
static int macosvfHelperLaunch(macosvfHelperClient *client) {

    if (client->helperPid > 0) {
        /* Already running */
        return 0;
    }

    VIR_DEBUG("Helper is managed by launchd, no need to launch");

    /* The helper is expected to be running via launchd.
     * We don't launch it directly anymore.
     */
    return 0;
}

/* Stop the helper application - with SMJobBless, we don't directly control it */
static void macosvfHelperStop(macosvfHelperClient *client) {

    if (client->helperPid <= 0) {
        return;
    }

    VIR_DEBUG("Stopping helper application (PID %d)", client->helperPid);

    /* Send SIGTERM to the helper process */
    kill(client->helperPid, SIGTERM);

    /* Wait a bit for termination using polling */
    for (int i = 0; i < 50; i++) {
        if (kill(client->helperPid, 0) != 0) {
            /* Process no longer exists */
            break;
        }
        usleep(100000); /* 100ms */
    }

    client->helperPid = 0;
}

/* Get or create the singleton helper client */
macosvfHelperClient *macosvfHelperClientGet(void) {
    if (!singletonClient) {
        singletonClient = g_new0(macosvfHelperClient, 1);
        singletonClient->refCount = 1;
        singletonClient->helperPid = 0;
        singletonClient->connection = nil;
    } else {
        singletonClient->refCount++;
    }
    return singletonClient;
}

/* Initialize the helper client */
int macosvfHelperClientInit(macosvfHelperClient *client) {

    if (!client) {
        return -1;
    }

    /* Initialize the XPC run loop thread if not already done */
    if (xpcRunLoopInit() < 0) {
        return -1;
    }

    @autoreleasepool {
        /* Launch the helper if not running */
        if (macosvfHelperLaunch(client) < 0) {
            return -1;
        }

        VIR_INFO("Creating XPC connection to Mach service: %s", [XPC_MACH_PORT_NAME UTF8String]);

        /* Create XPC connection - try privileged option first */
        client->connection = [[NSXPCConnection alloc] initWithMachServiceName:XPC_MACH_PORT_NAME
                                                                      options:NSXPCConnectionPrivileged];

        if (!client->connection) {
            VIR_ERROR("Failed to create XPC connection with privileged option");
            /* Try without privileged option */
            client->connection = [[NSXPCConnection alloc] initWithMachServiceName:XPC_MACH_PORT_NAME
                                                                          options:0];
        }

        if (!client->connection) {
            VIR_ERROR("Failed to create XPC connection");
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s", "Failed to create XPC connection");
            return -1;
        }

        /* Set up error handler for connection */
        [client->connection setInvalidationHandler:^{
            VIR_ERROR("XPC connection invalidated");
        }];

        /* Set up the XPC interface */
        [client->connection setRemoteObjectInterface:[NSXPCInterface interfaceWithProtocol:@protocol(VZXPCProtocol)]];

        [client->connection resume];

        VIR_INFO("XPC connection established and resumed");
    }

    return 0;
}

/* Cleanup the helper client */
void macosvfHelperClientFree(macosvfHelperClient *client) {

    if (!client) {
        return;
    }

    client->refCount--;

    if (client->refCount > 0) {
        return;
    }

    @autoreleasepool {
        /* Invalidate XPC connection */
        if (client->connection) {
            [client->connection invalidate];
            client->connection = nil;
        }

        /* Stop the helper */
        macosvfHelperStop(client);
    }

    if (client == singletonClient) {
        singletonClient = NULL;
    }

    /* Cleanup the XPC run loop thread when the last client is freed */
    xpcRunLoopCleanup();

    g_free(client);
}

/* VM Lifecycle Operations */

int macosvfHelperCreateVM(macosvfHelperClient *client,
                          const char *vmId,
                          const macosvfHelperVMConfig *config) {

    if (!client || !client->connection || !vmId || !config) {
        return -1;
    }

    __block int result = -1;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    @autoreleasepool {
        id<VZXPCProtocol> remote = [client->connection remoteObjectProxyWithErrorHandler:^(NSError *err) {
            VIR_ERROR("XPC connection error: %s", [[err localizedDescription] UTF8String]);
            error = err;
            dispatch_semaphore_signal(sem);
        }];

        /* Build configuration dictionary */
        NSMutableDictionary *configDict = [NSMutableDictionary dictionary];
        [configDict setObject:@(config->cpuCount) forKey:@"cpuCount"];
        [configDict setObject:@(config->memorySize) forKey:@"memorySize"];

        if (config->kernelPath) {
            [configDict setObject:[NSString stringWithUTF8String:config->kernelPath] forKey:@"kernelPath"];
        }
        if (config->initrdPath) {
            [configDict setObject:[NSString stringWithUTF8String:config->initrdPath] forKey:@"initrdPath"];
        }
        if (config->cmdline) {
            [configDict setObject:[NSString stringWithUTF8String:config->cmdline] forKey:@"cmdline"];
        }
        if (config->ptyPath) {
            [configDict setObject:[NSString stringWithUTF8String:config->ptyPath] forKey:@"ptyPath"];
        }

        /* Add network configuration */
        [configDict setObject:@(config->useNetwork ? 1 : 0) forKey:@"useNetwork"];

        /* Add port forwarding rules */
        if (config->numPortForwards > 0) {
            NSMutableArray *fwArray = [NSMutableArray array];
            for (int i = 0; i < config->numPortForwards && i < MACOSVF_MAX_PORT_FORWARDS; i++) {
                NSMutableDictionary *fwDict = [NSMutableDictionary dictionary];
                [fwDict setObject:@(config->portForwards[i].hostPort) forKey:@"hostPort"];
                [fwDict setObject:@(config->portForwards[i].guestPort) forKey:@"guestPort"];
                [fwDict setObject:@(config->portForwards[i].protocol ? YES : NO) forKey:@"isUDP"];
                [fwArray addObject:fwDict];
            }
            [configDict setObject:fwArray forKey:@"portForwards"];
            NSLog(@"macosvf_helper_client: Adding %d port forwards", config->numPortForwards);
        }

        /* Add disk paths as an array */
        if (config->numDisks > 0) {
            NSMutableArray *diskArray = [NSMutableArray array];
            for (int i = 0; i < config->numDisks && i < MACOSVF_MAX_DISKS; i++) {
                if (config->diskPaths[i]) {
                    [diskArray addObject:[NSString stringWithUTF8String:config->diskPaths[i]]];
                }
            }
            [configDict setObject:diskArray forKey:@"diskPaths"];
        }

        /* Call remote method */
        [remote createVMWithId:[NSString stringWithUTF8String:vmId]
                        config:configDict
                         reply:^(BOOL success, NSError *err) {
            result = success ? 0 : -1;
            if (err) {
                error = err;
                VIR_ERROR("CreateVM failed: %s", [[err localizedDescription] UTF8String]);
            }
            dispatch_semaphore_signal(sem);
        }];
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    DISPATCH_RELEASE(sem);

    if (error) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "Failed to create VM: %s",
                       [[error localizedDescription] UTF8String]);
        return -1;
    }

    return result;
}

int macosvfHelperStartVM(macosvfHelperClient *client,
                         const char *vmId) {

    if (!client || !client->connection || !vmId) {
        return -1;
    }

    __block int result = -1;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    @autoreleasepool {
        id<VZXPCProtocol> remote = [client->connection remoteObjectProxyWithErrorHandler:^(NSError *err) {
            error = err;
            dispatch_semaphore_signal(sem);
        }];

        [remote startVMWithId:[NSString stringWithUTF8String:vmId]
                        reply:^(BOOL success, NSError *err) {
            result = success ? 0 : -1;
            if (err) {
                error = err;
                VIR_ERROR("StartVM failed: %s", [[err localizedDescription] UTF8String]);
            }
            dispatch_semaphore_signal(sem);
        }];
    }

    /* Start is async - wait with timeout */
    dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC);
    int waitResult = dispatch_semaphore_wait(sem, timeout);
    DISPATCH_RELEASE(sem);

    if (waitResult != 0) {
        VIR_ERROR("StartVM timed out");
        virReportError(VIR_ERR_OPERATION_TIMEOUT, "%s", "Start VM timed out");
        return -1;
    }

    if (error) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "Failed to start VM: %s",
                       [[error localizedDescription] UTF8String]);
        return -1;
    }

    return result;
}

int macosvfHelperStopVM(macosvfHelperClient *client,
                        const char *vmId) {

    if (!client || !client->connection || !vmId) {
        return -1;
    }

    __block int result = -1;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    @autoreleasepool {
        id<VZXPCProtocol> remote = [client->connection remoteObjectProxyWithErrorHandler:^(NSError *err) {
            error = err;
            dispatch_semaphore_signal(sem);
        }];

        [remote stopVMWithId:[NSString stringWithUTF8String:vmId]
                       reply:^(BOOL success, NSError *err) {
            result = success ? 0 : -1;
            if (err) {
                error = err;
            }
            dispatch_semaphore_signal(sem);
        }];
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    DISPATCH_RELEASE(sem);

    if (error) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "Failed to stop VM: %s",
                       [[error localizedDescription] UTF8String]);
        return -1;
    }

    return result;
}

int macosvfHelperPauseVM(macosvfHelperClient *client,
                         const char *vmId) {

    if (!client || !client->connection || !vmId) {
        return -1;
    }

    __block int result = -1;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    @autoreleasepool {
        id<VZXPCProtocol> remote = [client->connection remoteObjectProxyWithErrorHandler:^(NSError *err) {
            error = err;
            dispatch_semaphore_signal(sem);
        }];

        [remote pauseVMWithId:[NSString stringWithUTF8String:vmId]
                        reply:^(BOOL success, NSError *err) {
            result = success ? 0 : -1;
            if (err) {
                error = err;
                VIR_ERROR("PauseVM failed: %s", [[err localizedDescription] UTF8String]);
            }
            dispatch_semaphore_signal(sem);
        }];
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    DISPATCH_RELEASE(sem);

    if (error) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "Failed to pause VM: %s",
                       [[error localizedDescription] UTF8String]);
        return -1;
    }

    return result;
}

int macosvfHelperResumeVM(macosvfHelperClient *client,
                          const char *vmId) {

    if (!client || !client->connection || !vmId) {
        return -1;
    }

    __block int result = -1;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    @autoreleasepool {
        id<VZXPCProtocol> remote = [client->connection remoteObjectProxyWithErrorHandler:^(NSError *err) {
            error = err;
            dispatch_semaphore_signal(sem);
        }];

        [remote resumeVMWithId:[NSString stringWithUTF8String:vmId]
                         reply:^(BOOL success, NSError *err) {
            result = success ? 0 : -1;
            if (err) {
                error = err;
                VIR_ERROR("ResumeVM failed: %s", [[err localizedDescription] UTF8String]);
            }
            dispatch_semaphore_signal(sem);
        }];
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    DISPATCH_RELEASE(sem);

    if (error) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "Failed to resume VM: %s",
                       [[error localizedDescription] UTF8String]);
        return -1;
    }

    return result;
}

int macosvfHelperDestroyVM(macosvfHelperClient *client,
                           const char *vmId) {

    if (!client || !client->connection || !vmId) {
        return -1;
    }

    __block int result = -1;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    @autoreleasepool {
        id<VZXPCProtocol> remote = [client->connection remoteObjectProxyWithErrorHandler:^(NSError *err) {
            error = err;
            dispatch_semaphore_signal(sem);
        }];

        [remote destroyVMWithId:[NSString stringWithUTF8String:vmId]
                          reply:^(BOOL success, NSError *err) {
            result = success ? 0 : -1;
            if (err) {
                error = err;
            }
            dispatch_semaphore_signal(sem);
        }];
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    DISPATCH_RELEASE(sem);

    if (error) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "Failed to destroy VM: %s",
                       [[error localizedDescription] UTF8String]);
        return -1;
    }

    return result;
}

/* VM Query Operations */

int macosvfHelperGetVMState(macosvfHelperClient *client,
                            const char *vmId,
                            macosvfHelperVMState *state) {

    if (!client || !client->connection || !vmId || !state) {
        return -1;
    }

    __block int result = -1;
    __block NSInteger vmState = 0;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    @autoreleasepool {
        id<VZXPCProtocol> remote = [client->connection remoteObjectProxyWithErrorHandler:^(NSError *err) {
            error = err;
            dispatch_semaphore_signal(sem);
        }];

        [remote getStateForVMWithId:[NSString stringWithUTF8String:vmId]
                              reply:^(NSInteger s, NSError *err) {
            vmState = s;
            result = 0;
            if (err) {
                error = err;
            }
            dispatch_semaphore_signal(sem);
        }];
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    DISPATCH_RELEASE(sem);

    if (error) {
        *state = MACOSVF_HELPER_VM_STATE_ERROR;
        return -1;
    }

    *state = (macosvfHelperVMState)vmState;
    return result;
}

int macosvfHelperGetConsolePath(macosvfHelperClient *client,
                                const char *vmId,
                                char **path) {

    if (!client || !client->connection || !vmId || !path) {
        return -1;
    }

    __block int result = -1;
    __block NSString *consolePath = nil;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    @autoreleasepool {
        id<VZXPCProtocol> remote = [client->connection remoteObjectProxyWithErrorHandler:^(NSError *err) {
            error = err;
            dispatch_semaphore_signal(sem);
        }];

        [remote getConsolePathForVMWithId:[NSString stringWithUTF8String:vmId]
                                    reply:^(NSString *p, NSError *err) {
            consolePath = p;
            result = 0;
            if (err) {
                error = err;
            }
            dispatch_semaphore_signal(sem);
        }];
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    DISPATCH_RELEASE(sem);

    if (error) {
        return -1;
    }

    if (consolePath) {
        *path = strdup([consolePath UTF8String]);
    } else {
        *path = NULL;
    }

    return result;
}

/* Utility Operations */

int macosvfHelperPing(macosvfHelperClient *client) {

    if (!client || !client->connection) {
        return -1;
    }

    __block int result = -1;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    @autoreleasepool {
        id<VZXPCProtocol> remote = [client->connection remoteObjectProxy];
        [remote pingWithReply:^(NSString *pong) {
            result = 0;
            dispatch_semaphore_signal(sem);
        }];
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    DISPATCH_RELEASE(sem);

    return result;
}

int macosvfHelperListVMs(macosvfHelperClient *client,
                         char ***vmIds,
                         size_t *count) {
    /* Implement as needed */
    return 0;
}
