/*
 * VZManager.h - Virtualization Framework Manager for libvirt macOS VF helper
 *
 * This class manages all VZ framework operations on the main thread
 * where the NSRunLoop is properly running.
 */

#import <Foundation/Foundation.h>
#import <Virtualization/Virtualization.h>

NS_ASSUME_NONNULL_BEGIN

@class VZManager;

/* Delegate protocol for VZ manager events */
@protocol VZManagerDelegate <NSObject>
@optional
- (void)vzManager:(VZManager *)manager didCreateVM:(NSString *)vmId error:(nullable NSError *)error;
- (void)vzManager:(VZManager *)manager didStartVM:(NSString *)vmId error:(nullable NSError *)error;
- (void)vzManager:(VZManager *)manager didStopVM:(NSString *)vmId error:(nullable NSError *)error;
- (void)vzManager:(VZManager *)manager didGetState:(NSString *)vmId state:(NSString *)state;
- (void)vzManager:(VZManager *)manager didGetConsolePath:(NSString *)vmId path:(NSString *)path;
@end

/* VM state enumeration */
typedef NS_ENUM(NSInteger, VZManagerVMState) {
    VZManagerVMStateUnknown = 0,
    VZManagerVMStateStopped,
    VZManagerVMStateRunning,
    VZManagerVMStatePaused,
    VZManagerVMStateError,
    VZManagerVMStateStarting,
    VZManagerVMStateStopping,
};

/* Main VZ Manager class */
@interface VZManager : NSObject

@property (nonatomic, weak) id<VZManagerDelegate> delegate;
@property (nonatomic, readonly) NSDictionary<NSString *, VZVirtualMachine *> *virtualMachines;

/* Shared singleton instance */
+ (instancetype)sharedManager;

/* VM Lifecycle Methods */
- (void)createVMWithId:(NSString *)vmId
                config:(NSDictionary *)config
           completion:(nullable void (^)(BOOL success, NSError * _Nullable error))completion;

- (void)startVMWithId:(NSString *)vmId
           completion:(nullable void (^)(BOOL success, NSError * _Nullable error))completion;

- (void)stopVMWithId:(NSString *)vmId
          completion:(nullable void (^)(BOOL success, NSError * _Nullable error))completion;

- (void)pauseVMWithId:(NSString *)vmId
           completion:(nullable void (^)(BOOL success, NSError * _Nullable error))completion;

- (void)resumeVMWithId:(NSString *)vmId
            completion:(nullable void (^)(BOOL success, NSError * _Nullable error))completion;

/* VM Query Methods */
- (void)getStateForVMWithId:(NSString *)vmId
                 completion:(nullable void (^)(VZManagerVMState state, NSError * _Nullable error))completion;

- (void)getConsolePathForVMWithId:(NSString *)vmId
                       completion:(nullable void (^)(NSString * _Nullable path, NSError * _Nullable error))completion;

/* Utility Methods */
- (NSString *)stateToString:(VZManagerVMState)state;
- (VZManagerVMState)stateFromVZState:(VZVirtualMachineState)vzState;

@end

NS_ASSUME_NONNULL_END
