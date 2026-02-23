/*
 * VZManager.m - Virtualization Framework Manager Implementation
 */

#import "VZManager.h"
#import <errno.h>
#import <fcntl.h>
#import <string.h>
#import <unistd.h>
#import <Virtualization/VZGenericPlatformConfiguration.h>
#import <Virtualization/VZGenericMachineIdentifier.h>

@interface VZManager () {
  NSMutableDictionary<NSString *, VZVirtualMachine *> *_virtualMachines;
  NSMutableDictionary<NSString *, VZVirtualMachineConfiguration *>
      *_configurations;
  NSMutableDictionary<NSString *, NSString *> *_consolePaths;
}
@end

@implementation VZManager

+ (instancetype)sharedManager {
  static VZManager *sharedInstance = nil;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    sharedInstance = [[VZManager alloc] init];
  });
  return sharedInstance;
}

- (instancetype)init {
  self = [super init];
  if (self) {
    _virtualMachines = [NSMutableDictionary dictionary];
    _configurations = [NSMutableDictionary dictionary];
    _consolePaths = [NSMutableDictionary dictionary];
  }
  return self;
}

- (NSDictionary<NSString *, VZVirtualMachine *> *)virtualMachines {
  return [_virtualMachines copy];
}

#pragma mark - VM Lifecycle Methods

- (void)createVMWithId:(NSString *)vmId
                config:(NSDictionary *)config
            completion:(nullable void (^)(BOOL success,
                                          NSError *_Nullable error))completion {

  NSLog(@"VZManager: Creating VM %@", vmId);

  /* Write creation start to log file (append) */
  NSString *logFile = @"/tmp/virtmacosvf-helper.log";
  NSString *createLog = [NSString
      stringWithFormat:@"\n===== VZManager: Creating VM %@ =====\n", vmId];
  if ([[NSFileManager defaultManager] fileExistsAtPath:logFile]) {
    NSString *existing = [NSString stringWithContentsOfFile:logFile
                                                   encoding:NSUTF8StringEncoding
                                                      error:nil];
    createLog = [existing stringByAppendingString:createLog];
  }
  [createLog writeToFile:logFile
              atomically:YES
                encoding:NSUTF8StringEncoding
                   error:nil];

  @autoreleasepool {
    NSError *error = nil;

    /* Create VM configuration */
    VZVirtualMachineConfiguration *vzConfig =
        [[VZVirtualMachineConfiguration alloc] init];

    /* Set CPU count */
    NSNumber *cpuCount = config[@"cpuCount"];
    vzConfig.CPUCount = (cpuCount) ? [cpuCount integerValue] : 2;

    /* Set memory size */
    NSNumber *memorySize = config[@"memorySize"];
    vzConfig.memorySize = (memorySize) ? [memorySize longLongValue]
                                       : (1024 * 1024 * 1024); /* 1GB default */

    /* Configure platform - use GenericPlatformConfiguration for ARM64 Linux
     * boot */
#if defined(__arm64__)
    VZGenericPlatformConfiguration *platformConfig =
        [[VZGenericPlatformConfiguration alloc] init];
    /* Set a unique machine identifier for ARM64 */
    if (@available(macOS 13.0, *)) {
      platformConfig.machineIdentifier = [[VZGenericMachineIdentifier alloc] init];
      NSLog(@"VZManager: Using GenericPlatformConfiguration with machine identifier for ARM64");
    } else {
      NSLog(@"VZManager: Using GenericPlatformConfiguration for ARM64 (no machine identifier on macOS < 13.0)");
    }
    vzConfig.platform = platformConfig;
#else
    VZGenericPlatformConfiguration *platformConfig =
        [[VZGenericPlatformConfiguration alloc] init];
    vzConfig.platform = platformConfig;
#endif

    /* Configure boot loader */
    NSString *kernelPath = config[@"kernelPath"];
    if (kernelPath) {
      /* Use Linux boot configuration */
      NSURL *kernelURL = [NSURL fileURLWithPath:kernelPath];
      VZLinuxBootLoader *bootLoader =
          [[VZLinuxBootLoader alloc] initWithKernelURL:kernelURL];

      NSString *cmdline = config[@"cmdline"];
      if (cmdline) {
        bootLoader.commandLine = cmdline;
      }

      NSString *initrdPath = config[@"initrdPath"];
      if (initrdPath) {
        NSURL *initrdURL = [NSURL fileURLWithPath:initrdPath];
        bootLoader.initialRamdiskURL = initrdURL;
      }

      vzConfig.bootLoader = bootLoader;
      NSLog(@"VZManager: Using Linux boot loader with kernel %@", kernelPath);

      /* Log detailed boot info */
      NSMutableString *bootLog = [NSMutableString
          stringWithFormat:@"\nVZManager: Linux boot configuration\n"];
      [bootLog appendFormat:@"VZManager: Kernel path: %@\n", kernelPath];
      [bootLog appendFormat:@"VZManager: Initrd path: %@\n",
                            initrdPath ?: @"(none)"];
      [bootLog
          appendFormat:@"VZManager: Command line: %@\n", cmdline ?: @"(none)"];

      /* Check if kernel file exists */
      BOOL kernelExists =
          [[NSFileManager defaultManager] fileExistsAtPath:kernelPath];
      [bootLog appendFormat:@"VZManager: Kernel file exists: %@\n",
                            kernelExists ? @"YES" : @"NO"];

      if (initrdPath) {
        BOOL initrdExists =
            [[NSFileManager defaultManager] fileExistsAtPath:initrdPath];
        [bootLog appendFormat:@"VZManager: Initrd file exists: %@\n",
                              initrdExists ? @"YES" : @"NO"];
      }

      /* Log to system log and file */
      NSLog(@"%@", bootLog);
    } else {
      /* Use EFI boot loader for disk boot */
      NSLog(@"VZManager: No kernel specified, using EFI boot loader");
      VZEFIBootLoader *bootLoader = [[VZEFIBootLoader alloc] init];

      /* Create EFI variable store for NVRAM */
      NSError *storeError = nil;
      NSURL *storeURL = [NSURL
          fileURLWithPath:[NSTemporaryDirectory()
                              stringByAppendingPathComponent:
                                  [NSString stringWithFormat:@"%@_nvram.plist",
                                                             vmId]]];
      VZEFIVariableStore *variableStore = nil;

      /* Check if file already exists */
      if ([[NSFileManager defaultManager] fileExistsAtPath:storeURL.path]) {
        /* Use existing variable store */
        variableStore = [[VZEFIVariableStore alloc] initWithURL:storeURL];
        NSLog(@"VZManager: Using existing EFI variable store at %@",
              storeURL.path);
      }

      /* If that failed or file didn't exist, create new one */
      if (!variableStore) {
        variableStore = [[VZEFIVariableStore alloc]
            initCreatingVariableStoreAtURL:storeURL
                                   options:0
                                     error:&storeError];
      }

      if (variableStore) {
        bootLoader.variableStore = variableStore;
        NSLog(@"VZManager: EFI variable store ready at %@", storeURL.path);
      } else {
        NSLog(@"VZManager: Failed to create EFI variable store: %@",
              storeError);
        if (completion) {
          NSError *error = [NSError
              errorWithDomain:@"VZManager"
                         code:4
                     userInfo:@{
                       NSLocalizedDescriptionKey : [NSString
                           stringWithFormat:
                               @"Failed to create EFI variable store: %@",
                               storeError.localizedDescription]
                     }];
          completion(NO, error);
        }
        return;
      }

      vzConfig.bootLoader = bootLoader;
    }

    /* Configure console/serial port */
    NSString *ptyPath = config[@"ptyPath"];
    NSLog(@"VZManager: ptyPath = %@", ptyPath ?: @"(nil)");
    NSMutableArray *serialPorts = [NSMutableArray array];

    if (ptyPath) {
      /* Create file handles for the PTY using POSIX open
       * Open with O_RDWR first, then dup() to create separate FDs
       * for reading and writing. This ensures proper PTY handling.
       * Note: Do NOT use O_NONBLOCK - VZ framework expects blocking I/O
       * for the serial port attachment to work correctly.
       */
      int rwFd = open([ptyPath UTF8String], O_RDWR | O_NOCTTY);
      NSLog(@"VZManager: rwFd = %d", rwFd);

      NSFileHandle *readHandle = nil;
      NSFileHandle *writeHandle = nil;

      if (rwFd >= 0) {
        /* Duplicate the file descriptor for separate read/write handles */
        int readFd = dup(rwFd);
        int writeFd = dup(rwFd);
        NSLog(@"VZManager: readFd = %d, writeFd = %d (from rwFd %d)", readFd, writeFd, rwFd);

        if (readFd >= 0 && writeFd >= 0) {
          /* Create file handles - readHandle takes ownership of readFd,
           * writeHandle takes ownership of writeFd.
           * We keep rwFd open separately to ensure the PTY stays connected.
           */
          readHandle = [[NSFileHandle alloc] initWithFileDescriptor:readFd
                                                     closeOnDealloc:YES];
          writeHandle = [[NSFileHandle alloc] initWithFileDescriptor:writeFd
                                                      closeOnDealloc:YES];
          /* Disable closeOnDealloc for rwFd - we'll close it manually */
          NSLog(@"VZManager: readHandle = %@, writeHandle = %@",
                readHandle ?: @"(nil)", writeHandle ?: @"(nil)");
        } else {
          if (readFd < 0) {
            NSLog(@"VZManager: Failed to dup FD for reading: %s",
                  strerror(errno));
          }
          if (writeFd < 0) {
            NSLog(@"VZManager: Failed to dup FD for writing: %s",
                  strerror(errno));
          }
          if (readFd >= 0)
            close(readFd);
          if (writeFd >= 0)
            close(writeFd);
        }
        close(rwFd);
      } else {
        NSLog(@"VZManager: Failed to open PTY: %s", strerror(errno));
      }

      if (readHandle && writeHandle) {
        VZFileHandleSerialPortAttachment *attachment =
            [[VZFileHandleSerialPortAttachment alloc]
                initWithFileHandleForReading:readHandle
                        fileHandleForWriting:writeHandle];
        NSLog(@"VZManager: attachment = %@", attachment ?: @"(nil)");

        if (attachment) {
          VZVirtioConsoleDeviceSerialPortConfiguration *consoleConfig =
              [[VZVirtioConsoleDeviceSerialPortConfiguration alloc] init];
          consoleConfig.attachment = attachment;
          [serialPorts addObject:consoleConfig];
          _consolePaths[vmId] = ptyPath;
          NSLog(@"VZManager: Serial port configured for %@", ptyPath);
        } else {
          NSLog(@"VZManager: Failed to create serial port attachment");
        }
      } else {
        NSLog(@"VZManager: Failed to create PTY file handles");
      }
    } else {
      /* No PTY path provided - create a null console for headless Linux VMs
       * This provides the hvc0 device that Linux kernels often expect
       */
#if defined(__arm64__)
      VZVirtioConsoleDeviceSerialPortConfiguration *consoleConfig =
          [[VZVirtioConsoleDeviceSerialPortConfiguration alloc] init];
      /* Use a null attachment - data written is discarded */
      VZFileHandleSerialPortAttachment *nullAttachment =
          [[VZFileHandleSerialPortAttachment alloc]
              initWithFileHandleForReading:[NSFileHandle fileHandleWithStandardInput]
                      fileHandleForWriting:[NSFileHandle fileHandleWithStandardOutput]];
      consoleConfig.attachment = nullAttachment;
      [serialPorts addObject:consoleConfig];
      NSLog(@"VZManager: Null console configured for headless VM");
#endif
    }

    if (serialPorts.count > 0) {
      vzConfig.serialPorts = serialPorts;
    }

    /* Configure network with NAT */
    NSNumber *useNetwork = config[@"useNetwork"];
    if (!useNetwork || [useNetwork boolValue]) {
      /* Default to NAT networking */
      VZNATNetworkDeviceAttachment *natAttachment =
          [[VZNATNetworkDeviceAttachment alloc] init];

      VZVirtioNetworkDeviceConfiguration *networkConfig =
          [[VZVirtioNetworkDeviceConfiguration alloc] init];
      networkConfig.attachment = natAttachment;
      vzConfig.networkDevices = @[ networkConfig ];
      NSLog(@"VZManager: Network configured with NAT");
    }

    /* Configure graphics, keyboard, and pointing devices for ARM64 Linux */
#if defined(__arm64__)
    /* Configure graphics device with scanout */
    VZVirtioGraphicsDeviceConfiguration *graphicsConfig =
        [[VZVirtioGraphicsDeviceConfiguration alloc] init];

    /* Add a scanout (display) configuration */
    VZVirtioGraphicsScanoutConfiguration *scanoutConfig =
        [[VZVirtioGraphicsScanoutConfiguration alloc]
            initWithWidthInPixels:1024 heightInPixels:768];
    graphicsConfig.scanouts = @[ scanoutConfig ];

    vzConfig.graphicsDevices = @[ graphicsConfig ];
    NSLog(@"VZManager: Graphics device with scanout configured");

    /* Configure keyboard */
    VZUSBKeyboardConfiguration *keyboardConfig =
        [[VZUSBKeyboardConfiguration alloc] init];
    vzConfig.keyboards = @[ keyboardConfig ];
    NSLog(@"VZManager: Keyboard configured");

    /* Configure pointing device (mouse) */
    VZUSBScreenCoordinatePointingDeviceConfiguration *pointingConfig =
        [[VZUSBScreenCoordinatePointingDeviceConfiguration alloc] init];
    vzConfig.pointingDevices = @[ pointingConfig ];
    NSLog(@"VZManager: Pointing device configured");

    /* Configure entropy device for Linux VMs */
    VZVirtioEntropyDeviceConfiguration *entropyConfig =
        [[VZVirtioEntropyDeviceConfiguration alloc] init];
    vzConfig.entropyDevices = @[ entropyConfig ];
    NSLog(@"VZManager: Entropy device configured");
#endif

    /* Configure disk storage */
    NSArray *diskPaths = config[@"diskPaths"];
    if (diskPaths && diskPaths.count > 0) {
      NSMutableArray *storageDevices = [NSMutableArray array];

      for (NSUInteger i = 0; i < diskPaths.count; i++) {
        NSString *diskPath = diskPaths[i];
        NSURL *diskURL = [NSURL fileURLWithPath:diskPath];

        /* Create disk image attachment */
        NSError *attachmentError = nil;
        VZDiskImageStorageDeviceAttachment *diskAttachment =
            [[VZDiskImageStorageDeviceAttachment alloc]
                initWithURL:diskURL
                   readOnly:NO
                      error:&attachmentError];

        if (diskAttachment) {
          VZVirtioBlockDeviceConfiguration *diskConfig =
              [[VZVirtioBlockDeviceConfiguration alloc]
                  initWithAttachment:diskAttachment];
          [storageDevices addObject:diskConfig];
          NSLog(@"VZManager: Successfully created disk attachment for %@",
                diskPath);
        } else {
          NSLog(@"VZManager: Failed to create disk attachment for %@: %@",
                diskPath, attachmentError);
          /* Fail the VM creation if we can't attach the disk */
          if (completion) {
            NSError *error = [NSError
                errorWithDomain:@"VZManager"
                           code:3
                       userInfo:@{
                         NSLocalizedDescriptionKey : [NSString
                             stringWithFormat:@"Failed to attach disk: %@",
                                              attachmentError
                                                  .localizedDescription]
                       }];
            completion(NO, error);
          }
          return;
        }
      }

      if (storageDevices.count > 0) {
        vzConfig.storageDevices = storageDevices;
        NSLog(@"VZManager: Added %lu storage devices",
              (unsigned long)storageDevices.count);
      }
    } else {
      NSLog(@"VZManager: No disk paths provided in configuration");
    }

    /* Validate configuration */
    NSError *validationError = nil;
    BOOL isValid = [vzConfig validateWithError:&validationError];
    NSLog(@"VZManager: Configuration validation result: %@",
          isValid ? @"valid" : @"invalid");

    /* Write validation result to log file (append) */
    NSString *logFile = @"/tmp/virtmacosvf-helper.log";
    NSMutableString *validationLog = [NSMutableString
        stringWithFormat:@"\nVZManager: === Configuration validation ===\n"];
    [validationLog appendFormat:@"VZManager: Configuration validation: %@\n",
                                isValid ? @"valid" : @"invalid"];

    if (!isValid) {
      NSLog(@"VZManager: Configuration validation failed: %@", validationError);
      NSLog(@"VZManager: Validation error details: domain=%@ code=%ld "
            @"userInfo=%@",
            validationError.domain, (long)validationError.code,
            validationError.userInfo);

      [validationLog
          appendFormat:@"VZManager: Validation FAILED: domain=%@ code=%ld\n",
                       validationError.domain, (long)validationError.code];
      [validationLog appendFormat:@"VZManager: Validation error: %@\n",
                                  validationError.localizedDescription];
      [validationLog
          appendFormat:@"VZManager: Validation failureReason: %@\n",
                       validationError.localizedFailureReason ?: @"(none)"];
      if ([[NSFileManager defaultManager] fileExistsAtPath:logFile]) {
        NSString *existing =
            [NSString stringWithContentsOfFile:logFile
                                      encoding:NSUTF8StringEncoding
                                         error:nil];
        validationLog =
            [NSMutableString stringWithFormat:@"%@%@", existing, validationLog];
      }
      [validationLog writeToFile:logFile
                      atomically:YES
                        encoding:NSUTF8StringEncoding
                           error:nil];

      if (completion) {
        completion(NO, validationError);
      }
      return;
    }
    [validationLog
        appendFormat:@"VZManager: Configuration validated successfully\n"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:logFile]) {
      NSString *existing =
          [NSString stringWithContentsOfFile:logFile
                                    encoding:NSUTF8StringEncoding
                                       error:nil];
      validationLog =
          [NSMutableString stringWithFormat:@"%@%@", existing, validationLog];
    }
    [validationLog writeToFile:logFile
                    atomically:YES
                      encoding:NSUTF8StringEncoding
                         error:nil];
    NSLog(@"VZManager: Configuration validated successfully");

    /* Create the VM */
    VZVirtualMachine *vm =
        [[VZVirtualMachine alloc] initWithConfiguration:vzConfig];
    _virtualMachines[vmId] = vm;

    /* Log detailed VM configuration to both NSLog and file */
    NSMutableString *configLog = [NSMutableString
        stringWithFormat:@"\n===== VZManager: VM %@ configuration details =====\n", vmId];
    [configLog appendFormat:@"VZManager: VM CPU count: %lu\n", (unsigned long)vzConfig.CPUCount];
    [configLog appendFormat:@"VZManager: VM memory size: %llu bytes\n", vzConfig.memorySize];
    [configLog appendFormat:@"VZManager: VM platform type: %@\n", NSStringFromClass([vzConfig.platform class])];
    [configLog appendFormat:@"VZManager: VM boot loader type: %@\n", NSStringFromClass([vzConfig.bootLoader class])];
    [configLog appendFormat:@"VZManager: VM graphics devices: %lu\n", (unsigned long)vzConfig.graphicsDevices.count];
    [configLog appendFormat:@"VZManager: VM network devices: %lu\n", (unsigned long)vzConfig.networkDevices.count];
    [configLog appendFormat:@"VZManager: VM storage devices: %lu\n", (unsigned long)vzConfig.storageDevices.count];
    [configLog appendFormat:@"VZManager: VM serial ports: %lu\n", (unsigned long)vzConfig.serialPorts.count];
    [configLog appendFormat:@"VZManager: VM keyboards: %lu\n", (unsigned long)vzConfig.keyboards.count];
    [configLog appendFormat:@"VZManager: VM pointing devices: %lu\n", (unsigned long)vzConfig.pointingDevices.count];
    [configLog appendFormat:@"VZManager: VM entropy devices: %lu\n", (unsigned long)vzConfig.entropyDevices.count];

    NSLog(@"%@", configLog);

    /* Append to log file */
    if ([[NSFileManager defaultManager] fileExistsAtPath:logFile]) {
      NSString *existing = [NSString stringWithContentsOfFile:logFile
                                                     encoding:NSUTF8StringEncoding
                                                        error:nil];
      configLog = [NSMutableString stringWithFormat:@"%@%@", existing, configLog];
    }
    [configLog writeToFile:logFile
                atomically:YES
                  encoding:NSUTF8StringEncoding
                     error:nil];

    if (self.delegate) {
      [self.delegate vzManager:self didCreateVM:vmId error:nil];
    }

    if (completion) {
      completion(YES, nil);
    }
  }
}

- (void)startVMWithId:(NSString *)vmId
           completion:(nullable void (^)(BOOL success,
                                         NSError *_Nullable error))completion {

  NSLog(@"VZManager: Starting VM %@", vmId);

  VZVirtualMachine *vm = _virtualMachines[vmId];
  if (!vm) {
    NSError *error = [NSError
        errorWithDomain:@"VZManager"
                   code:1
               userInfo:@{NSLocalizedDescriptionKey : @"VM not found"}];
    if (completion) {
      completion(NO, error);
    }
    return;
  }

  /* Start the VM - this is async and calls completion when done */
  NSLog(@"VZManager: Calling startWithCompletionHandler for VM %@", vmId);
  NSLog(@"VZManager: VM state before start: %ld", (long)vm.state);

  /* Write detailed log to file for debugging (append) */
  NSString *logFile = @"/tmp/virtmacosvf-helper.log";
  NSString *startLog =
      [NSString stringWithFormat:@"\nVZManager: Starting VM %@ - state=%ld\n",
                                 vmId, (long)vm.state];
  if ([[NSFileManager defaultManager] fileExistsAtPath:logFile]) {
    NSString *existing = [NSString stringWithContentsOfFile:logFile
                                                   encoding:NSUTF8StringEncoding
                                                      error:nil];
    startLog = [existing stringByAppendingString:startLog];
  }
  [startLog writeToFile:logFile
             atomically:YES
               encoding:NSUTF8StringEncoding
                  error:nil];

  [vm startWithCompletionHandler:^(NSError *_Nullable error) {
    NSLog(@"VZManager: VM %@ start completed: %@", vmId, error ?: @"success");

    /* Always write detailed error info to file (append) */
    NSMutableString *errorLog = [NSMutableString
        stringWithFormat:@"\nVZManager: VM %@ start completed: %@\n", vmId,
                         error ?: @"success"];
    if (error) {
      [errorLog appendFormat:@"VZManager: Start error domain=%@ code=%ld\n",
                             error.domain, (long)error.code];
      [errorLog appendFormat:@"VZManager: Start error description=%@\n",
                             error.localizedDescription];
      [errorLog appendFormat:@"VZManager: Start error failureReason=%@\n",
                             error.localizedFailureReason ?: @"(none)"];
      [errorLog appendFormat:@"VZManager: Start error recoverySuggestion=%@\n",
                             error.localizedRecoverySuggestion ?: @"(none)"];
      [errorLog
          appendFormat:@"VZManager: Start error userInfo=%@\n", error.userInfo];

      /* Try to get underlying error from VZ framework */
      NSError *underlyingError = error.userInfo[NSUnderlyingErrorKey];
      if (underlyingError) {
        [errorLog
            appendFormat:
                @"VZManager: Underlying error: domain=%@ code=%ld desc=%@\n",
                underlyingError.domain, (long)underlyingError.code,
                underlyingError.localizedDescription];
      }
    }
    if ([[NSFileManager defaultManager] fileExistsAtPath:logFile]) {
      NSString *existing =
          [NSString stringWithContentsOfFile:logFile
                                    encoding:NSUTF8StringEncoding
                                       error:nil];
      errorLog = [NSMutableString stringWithFormat:@"%@%@", existing, errorLog];
    }
    [errorLog writeToFile:logFile
               atomically:YES
                 encoding:NSUTF8StringEncoding
                    error:nil];

    if (self.delegate) {
      [self.delegate vzManager:self didStartVM:vmId error:error];
    }

    if (completion) {
      completion(error == nil, error);
    }
  }];
}

- (void)stopVMWithId:(NSString *)vmId
          completion:(nullable void (^)(BOOL success,
                                        NSError *_Nullable error))completion {

  NSLog(@"VZManager: Stopping VM %@", vmId);

  VZVirtualMachine *vm = _virtualMachines[vmId];
  if (!vm) {
    NSError *error = [NSError
        errorWithDomain:@"VZManager"
                   code:1
               userInfo:@{NSLocalizedDescriptionKey : @"VM not found"}];
    if (completion) {
      completion(NO, error);
    }
    return;
  }

  /* Stop the VM */
  [vm stopWithCompletionHandler:^(NSError *_Nullable error) {
    NSLog(@"VZManager: VM %@ stop completed: %@", vmId, error ?: @"success");

    if (self.delegate) {
      [self.delegate vzManager:self didStopVM:vmId error:error];
    }

    if (completion) {
      completion(error == nil, error);
    }
  }];
}

- (void)pauseVMWithId:(NSString *)vmId
           completion:(nullable void (^)(BOOL success,
                                         NSError *_Nullable error))completion {

  NSLog(@"VZManager: Pausing VM %@", vmId);

  VZVirtualMachine *vm = _virtualMachines[vmId];
  if (!vm) {
    NSError *error = [NSError
        errorWithDomain:@"VZManager"
                   code:1
               userInfo:@{NSLocalizedDescriptionKey : @"VM not found"}];
    if (completion) {
      completion(NO, error);
    }
    return;
  }

  if (@available(macOS 13.0, *)) {
    [vm pauseWithCompletionHandler:^(NSError *_Nullable error) {
      NSLog(@"VZManager: VM %@ pause completed: %@", vmId, error ?: @"success");
      if (completion) {
        completion(error == nil, error);
      }
    }];
  } else {
    NSError *error =
        [NSError errorWithDomain:@"VZManager"
                            code:2
                        userInfo:@{
                          NSLocalizedDescriptionKey :
                              @"Pause not supported on this macOS version"
                        }];
    if (completion) {
      completion(NO, error);
    }
  }
}

- (void)resumeVMWithId:(NSString *)vmId
            completion:(nullable void (^)(BOOL success,
                                          NSError *_Nullable error))completion {

  NSLog(@"VZManager: Resuming VM %@", vmId);

  VZVirtualMachine *vm = _virtualMachines[vmId];
  if (!vm) {
    NSError *error = [NSError
        errorWithDomain:@"VZManager"
                   code:1
               userInfo:@{NSLocalizedDescriptionKey : @"VM not found"}];
    if (completion) {
      completion(NO, error);
    }
    return;
  }

  if (@available(macOS 13.0, *)) {
    [vm resumeWithCompletionHandler:^(NSError *_Nullable error) {
      NSLog(@"VZManager: VM %@ resume completed: %@", vmId,
            error ?: @"success");
      if (completion) {
        completion(error == nil, error);
      }
    }];
  } else {
    NSError *error =
        [NSError errorWithDomain:@"VZManager"
                            code:2
                        userInfo:@{
                          NSLocalizedDescriptionKey :
                              @"Resume not supported on this macOS version"
                        }];
    if (completion) {
      completion(NO, error);
    }
  }
}

#pragma mark - VM Query Methods

- (void)getStateForVMWithId:(NSString *)vmId
                 completion:
                     (nullable void (^)(VZManagerVMState state,
                                        NSError *_Nullable error))completion {

  VZVirtualMachine *vm = _virtualMachines[vmId];
  if (!vm) {
    NSError *error = [NSError
        errorWithDomain:@"VZManager"
                   code:1
               userInfo:@{NSLocalizedDescriptionKey : @"VM not found"}];
    if (completion) {
      completion(VZManagerVMStateError, error);
    }
    return;
  }

  VZManagerVMState state = [self stateFromVZState:vm.state];
  if (completion) {
    completion(state, nil);
  }
}

- (void)getConsolePathForVMWithId:(NSString *)vmId
                       completion:(nullable void (^)(NSString *_Nullable path,
                                                     NSError *_Nullable error))
                                      completion {

  NSString *path = _consolePaths[vmId];
  if (completion) {
    completion(path, nil);
  }
}

#pragma mark - Utility Methods

- (NSString *)stateToString:(VZManagerVMState)state {
  switch (state) {
  case VZManagerVMStateStopped:
    return @"stopped";
  case VZManagerVMStateRunning:
    return @"running";
  case VZManagerVMStatePaused:
    return @"paused";
  case VZManagerVMStateError:
    return @"error";
  case VZManagerVMStateStarting:
    return @"starting";
  case VZManagerVMStateStopping:
    return @"stopping";
  default:
    return @"unknown";
  }
}

- (VZManagerVMState)stateFromVZState:(VZVirtualMachineState)vzState {
  switch (vzState) {
  case VZVirtualMachineStateStopped:
    return VZManagerVMStateStopped;
  case VZVirtualMachineStateRunning:
    return VZManagerVMStateRunning;
  case VZVirtualMachineStatePaused:
    return VZManagerVMStatePaused;
  case VZVirtualMachineStateError:
    return VZManagerVMStateError;
  case VZVirtualMachineStateStarting:
  case VZVirtualMachineStatePausing:
  case VZVirtualMachineStateResuming:
    return VZManagerVMStateStarting;
  case VZVirtualMachineStateStopping:
  case VZVirtualMachineStateSaving:
  case VZVirtualMachineStateRestoring:
    return VZManagerVMStateStopping;
  default:
    return VZManagerVMStateUnknown;
  }
}

@end
