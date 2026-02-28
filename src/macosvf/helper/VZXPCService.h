/*
 * VZXPCService.h - XPC Service for VZ Manager
 *
 * This class handles XPC communication between the libvirt daemon
 * and the VZ Manager running on the main thread.
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@class VZManager;

/* XPC Service handler */
@interface VZXPCService : NSObject

@property (nonatomic, strong) VZManager *vzManager;
@property (nonatomic, strong) NSXPCListener *listener;

/* Start the XPC service */
- (void)startWithMachPortName:(NSString *)machPortName;

/* Stop the XPC service */
- (void)stop;

@end

/* XPC Protocol for communication */
@protocol VZXPCProtocol <NSObject>

/* Create a VM */
- (void)createVMWithId:(NSString *)vmId
                config:(NSDictionary *)config
           reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

/* Start a VM */
- (void)startVMWithId:(NSString *)vmId
            reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

/* Stop a VM */
- (void)stopVMWithId:(NSString *)vmId
           reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

/* Pause a VM */
- (void)pauseVMWithId:(NSString *)vmId
            reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

/* Resume a VM */
- (void)resumeVMWithId:(NSString *)vmId
             reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

/* Get VM state */
- (void)getStateForVMWithId:(NSString *)vmId
                  reply:(void (^)(NSInteger state, NSError * _Nullable error))reply;

/* Get console path */
- (void)getConsolePathForVMWithId:(NSString *)vmId
                        reply:(void (^)(NSString * _Nullable path, NSError * _Nullable error))reply;

/* List all VMs */
- (void)listVMsWithReply:(void (^)(NSArray *vmIds, NSError * _Nullable error))reply;

/* Destroy a VM */
- (void)destroyVMWithId:(NSString *)vmId
              reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

/* Ping for health check */
- (void)pingWithReply:(void (^)(NSString *pong))reply;

@end

NS_ASSUME_NONNULL_END
