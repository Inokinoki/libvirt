/*
 * VZXPCService.m - XPC Service Implementation
 */

#import "VZXPCService.h"
#import "VZManager.h"
#include <bootstrap.h>

@interface VZXPCService () <VZXPCProtocol, NSXPCListenerDelegate>
@end

@implementation VZXPCService

- (instancetype)init {
    self = [super init];
    if (self) {
        _vzManager = [VZManager sharedManager];
    }
    return self;
}

- (void)startWithMachPortName:(NSString *)machPortName {
    NSLog(@"VZXPCService: Starting with Mach port %@", machPortName);

    /* When running via launchd with MachServices, the Mach port is already
     * registered by launchd. We just need to create the NSXPCListener.
     */

    self.listener = [[NSXPCListener alloc] initWithMachServiceName:machPortName];
    self.listener.delegate = self;

    NSLog(@"VZXPCService: Created listener, resuming...");
    [self.listener resume];

    NSLog(@"VZXPCService: Listening on Mach port %@", machPortName);
}

- (void)stop {
    [self.listener suspend];
}

#pragma mark - VZXPCProtocol Implementation

- (void)createVMWithId:(NSString *)vmId
                config:(NSDictionary *)config
                 reply:(void (^)(BOOL success, NSError * _Nullable error))reply {

    NSLog(@"VZXPCService: createVMWithId %@", vmId);

    __block BOOL result = NO;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    [self.vzManager createVMWithId:vmId config:config completion:^(BOOL success, NSError * _Nullable err) {
        result = success;
        error = err;
        dispatch_semaphore_signal(sem);
    }];

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

    reply(result, error);
}

- (void)startVMWithId:(NSString *)vmId
                reply:(void (^)(BOOL success, NSError * _Nullable error))reply {

    NSLog(@"VZXPCService: startVMWithId %@", vmId);

    __block BOOL result = NO;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    /* VZVirtualMachine start must be called from main queue */
    if ([NSThread isMainThread]) {
        /* Already on main thread - call directly */
        [self.vzManager startVMWithId:vmId completion:^(BOOL success, NSError * _Nullable err) {
            result = success;
            error = err;
            dispatch_semaphore_signal(sem);
        }];
    } else {
        /* Dispatch to main queue */
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.vzManager startVMWithId:vmId completion:^(BOOL success, NSError * _Nullable err) {
                result = success;
                error = err;
                dispatch_semaphore_signal(sem);
            }];
        });
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    reply(result, error);
}

- (void)stopVMWithId:(NSString *)vmId
               reply:(void (^)(BOOL success, NSError * _Nullable error))reply {

    NSLog(@"VZXPCService: stopVMWithId %@", vmId);

    __block BOOL result = NO;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    /* VZVirtualMachine stop must be called from main queue */
    if ([NSThread isMainThread]) {
        [self.vzManager stopVMWithId:vmId completion:^(BOOL success, NSError * _Nullable err) {
            result = success;
            error = err;
            dispatch_semaphore_signal(sem);
        }];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.vzManager stopVMWithId:vmId completion:^(BOOL success, NSError * _Nullable err) {
                result = success;
                error = err;
                dispatch_semaphore_signal(sem);
            }];
        });
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    reply(result, error);
}

- (void)pauseVMWithId:(NSString *)vmId
                reply:(void (^)(BOOL success, NSError * _Nullable error))reply {

    NSLog(@"VZXPCService: pauseVMWithId %@", vmId);

    __block BOOL result = NO;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    /* VZVirtualMachine pause must be called from main queue */
    if ([NSThread isMainThread]) {
        [self.vzManager pauseVMWithId:vmId completion:^(BOOL success, NSError * _Nullable err) {
            result = success;
            error = err;
            dispatch_semaphore_signal(sem);
        }];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.vzManager pauseVMWithId:vmId completion:^(BOOL success, NSError * _Nullable err) {
                result = success;
                error = err;
                dispatch_semaphore_signal(sem);
            }];
        });
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    reply(result, error);
}

- (void)resumeVMWithId:(NSString *)vmId
                 reply:(void (^)(BOOL success, NSError * _Nullable error))reply {

    NSLog(@"VZXPCService: resumeVMWithId %@", vmId);

    __block BOOL result = NO;
    __block NSError *error = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    /* VZVirtualMachine resume must be called from main queue */
    if ([NSThread isMainThread]) {
        [self.vzManager resumeVMWithId:vmId completion:^(BOOL success, NSError * _Nullable err) {
            result = success;
            error = err;
            dispatch_semaphore_signal(sem);
        }];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.vzManager resumeVMWithId:vmId completion:^(BOOL success, NSError * _Nullable err) {
                result = success;
                error = err;
                dispatch_semaphore_signal(sem);
            }];
        });
    }

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    reply(result, error);
}

- (void)getStateForVMWithId:(NSString *)vmId
                      reply:(void (^)(NSInteger state, NSError * _Nullable error))reply {

    [self.vzManager getStateForVMWithId:vmId completion:^(VZManagerVMState state, NSError * _Nullable error) {
        reply((NSInteger)state, error);
    }];
}

- (void)getConsolePathForVMWithId:(NSString *)vmId
                            reply:(void (^)(NSString * _Nullable path, NSError * _Nullable error))reply {

    [self.vzManager getConsolePathForVMWithId:vmId completion:reply];
}

- (void)listVMsWithReply:(void (^)(NSArray *vmIds, NSError * _Nullable error))reply {

    NSArray *vmIds = [self.vzManager.virtualMachines allKeys];
    reply(vmIds, nil);
}

- (void)destroyVMWithId:(NSString *)vmId
                  reply:(void (^)(BOOL success, NSError * _Nullable error))reply {

    NSLog(@"VZXPCService: destroyVMWithId %@", vmId);

    /* Get the mutable copy of virtualMachines and remove the key */
    NSMutableDictionary *vms = [self.vzManager valueForKey:@"_virtualMachines"];
    if (vms) {
        [vms removeObjectForKey:vmId];
    }

    /* Also clear console path */
    NSMutableDictionary *paths = [self.vzManager valueForKey:@"_consolePaths"];
    if (paths) {
        [paths removeObjectForKey:vmId];
    }

    reply(YES, nil);
}

- (void)pingWithReply:(void (^)(NSString *pong))reply {

    reply(@"pong");
}

@end

#pragma mark - NSXPCListenerDelegate

@implementation VZXPCService (NSXPCListenerDelegate)

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection {

    NSLog(@"VZXPCService: Accepting new XPC connection");

    newConnection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(VZXPCProtocol)];
    newConnection.exportedObject = self;
    [newConnection resume];

    return YES;
}

@end
