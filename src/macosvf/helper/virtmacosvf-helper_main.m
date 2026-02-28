/*
 * virtmacosvf-helper_main.m - Main entry point for VZ Helper
 *
 * This is a minimal Cocoa application that runs an NSRunLoop on the main
 * thread and handles XPC requests from the libvirt macosvf driver.
 *
 * All VZ framework operations are executed on this main thread where
 * the NSRunLoop is properly running, satisfying VZ's threading requirements.
 */

#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#import "VZXPCService.h"
#import "VZManager.h"

/* The Mach port name for XPC communication */
#define XPC_MACH_PORT_NAME @"org.libvirt.virtmacosvf-helper"

@interface HelperAppDelegate : NSObject <NSApplicationDelegate>

@property (nonatomic, strong) VZXPCService *xpcService;
@property (nonatomic, strong) NSWindow *window;

@end

@implementation HelperAppDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        _xpcService = [[VZXPCService alloc] init];
    }
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {

    NSLog(@"virtmacosvf-helper: Application did finish launching");
    NSLog(@"virtmacosvf-helper: Starting XPC service on port %@", XPC_MACH_PORT_NAME);

    /* Start the XPC service */
    [self.xpcService startWithMachPortName:XPC_MACH_PORT_NAME];

    NSLog(@"virtmacosvf-helper: XPC service started, running NSRunLoop");

    /* The NSRunLoop is already running because we're an NSApplication.
     * VZ framework operations will be executed on this main thread.
     */
}

- (void)applicationWillTerminate:(NSNotification *)notification {

    NSLog(@"virtmacosvf-helper: Application will terminate");

    /* Stop the XPC service */
    [self.xpcService stop];

    NSLog(@"virtmacosvf-helper: Goodbye!");
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    /* Keep running even without windows */
    return NO;
}

@end

int main(int argc, const char * argv[]) {

    /* Set up logging */
    NSLog(@"virtmacosvf-helper: Starting (PID: %d)", getpid());

    @autoreleasepool {
        /* Create the NSApplication - this sets up the main thread runloop */
        NSApplication *app = [NSApplication sharedApplication];

        /* Create and set the delegate */
        HelperAppDelegate *delegate = [[HelperAppDelegate alloc] init];
        [app setDelegate:delegate];

        /* Create a minimal window (hidden) to satisfy NSApplication requirements */
        NSRect frame = NSMakeRect(0, 0, 1, 1);
        NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        [window setLevel:NSFloatingWindowLevel];
        [window setOpaque:NO];
        [window setAlphaValue:0.0];
        [window setCanHide:YES];

        /* Set activation policy to accessory (no dock icon) */
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];

        /* Run the application - this starts the NSRunLoop */
        [app run];
    }

    return 0;
}
