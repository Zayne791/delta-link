//
//  GBAEmulatorBridge.mm
//  GBADeltaCore
//
//  Created by Riley Testut on 6/3/16.
//  Copyright © 2016 Riley Testut. All rights reserved.
//
//  Delta Link: the core is gpSP (libretro API, interpreter) instead of VBA-M, and while a game runs the bridge
//  hosts the glink link cable so a Kindle running the same gpSP can trade. All of the emulator and link work
//  is in DLGBACore.c (plain C, tested on Linux); this file adapts it to DeltaCore.
//

#import "GBAEmulatorBridge.h"

#include "DLGBACore.h"

// DeltaCore
#import <GBADeltaCore/GBADeltaCore.h>
#import <DeltaCore/DeltaCore.h>
#import <DeltaCore/DeltaCore-Swift.h>

#if STATIC_LIBRARY
#import "GBADeltaCore-Swift.h"
#import "GBATypes.h"
#else
#import <GBADeltaCore/GBADeltaCore-Swift.h>
#endif

NSInteger const GBALinkPort = 5739; // GLINK_PORT in glink.h

// NSNetService is deprecated but still the simplest way to publish a Bonjour record (see -advertiseLink).
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static NSString *const GBALinkBonjourType = @"_deltalink._tcp.";

@interface GBAEmulatorBridge () <NSNetServiceDelegate>

@property (nonatomic, copy, nullable, readwrite) NSURL *gameURL;

@property (nonatomic, readwrite) GBALinkState linkState;
@property (nonatomic, copy, nullable, readwrite) NSString *linkError;

@property (nonatomic) uint32_t activatedInputs;
@property (nonatomic) NSTimeInterval cachedFrameDuration;

@property (nonatomic, strong, nullable) NSNetService *bonjourService;

@end

#pragma mark - DLGBACore callbacks -

static void GBADLAudio(const int16_t *frames, size_t count, void *context)
{
    GBAEmulatorBridge *bridge = (__bridge GBAEmulatorBridge *)context;
    [bridge.audioRenderer.audioBuffer writeBuffer:(const void *)frames size:(NSInteger)(count * 2 * sizeof(int16_t))];
}

static void GBADLLog(const char *message, void *context)
{
    NSLog(@"[GBADeltaCore] %s", message);
}

static void GBADLLink(int state, const char *error, void *context)
{
    GBAEmulatorBridge *bridge = (__bridge GBAEmulatorBridge *)context;
    GBALinkState linkState = (GBALinkState)state;
    NSString *linkError = @(error ?: "");

    // Called on the emulation thread; Delta's UI listens on the main thread.
    dispatch_async(dispatch_get_main_queue(), ^{
        bridge.linkState = linkState;
        bridge.linkError = linkError.length > 0 ? linkError : nil;

        NSDictionary *userInfo = @{GBALinkStateKey: @(linkState), GBALinkErrorKey: linkError};
        [[NSNotificationCenter defaultCenter] postNotificationName:GBALinkStateDidChangeNotification object:bridge userInfo:userInfo];
    });
}

@implementation GBAEmulatorBridge
@synthesize audioRenderer = _audioRenderer;
@synthesize videoRenderer = _videoRenderer;
@synthesize saveUpdateHandler = _saveUpdateHandler;

+ (instancetype)sharedBridge
{
    static GBAEmulatorBridge *_emulatorBridge = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        _emulatorBridge = [[self alloc] init];
    });

    return _emulatorBridge;
}

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        _linkEnabled = YES;
        _cachedFrameDuration = 1.0 / dl_frames_per_second(); // 59.7275 fps, the GBA's real rate
    }

    return self;
}

#pragma mark - Emulation -

- (void)startWithGameURL:(NSURL *)gameURL
{
    self.gameURL = gameURL;

    if (self.patchesDirectoryURL != nil)
    {
        // VBA-M applied .ips/.ups/.ppf patches here (Delta's DLC events). gpSP reads the ROM file itself; not supported.
        NSLog(@"[GBADeltaCore] ROM patches are not supported by the gpSP core; starting %@ unpatched.", gameURL.lastPathComponent);
    }

    dl_callbacks callbacks = {};
    callbacks.audio = GBADLAudio;
    callbacks.link = GBADLLink;
    callbacks.log = GBADLLog;
    callbacks.ctx = (__bridge void *)self;

    if (!dl_start(gameURL.fileSystemRepresentation, &callbacks, self.isLinkEnabled ? (int)GBALinkPort : 0))
    {
        NSLog(@"Failed to load GBA game at %@.", gameURL);
        return;
    }

    self.cachedFrameDuration = 1.0 / dl_frames_per_second();

    if (self.isLinkEnabled)
    {
        [self advertiseLink];
    }
}

- (void)stop
{
    dl_stop();
    [self stopAdvertisingLink];

    dispatch_async(dispatch_get_main_queue(), ^{
        self.linkState = GBALinkStateOff;
    });
}

- (void)pause
{
}

- (void)resume
{
    // While Delta was paused (pause menu, app in the background) iOS may have torn down our sockets.
    // Unless the Kindle is still linked, open a fresh listener.
    if (dl_is_running() && dl_link_state() != DL_LINK_LINKED)
    {
        dl_link_restart();
    }
}

- (void)runFrameAndProcessVideo:(BOOL)processVideo
{
    uint8_t *videoBuffer = processVideo ? self.videoRenderer.videoBuffer : NULL;

    BOOL saveSettled = dl_run_frame(videoBuffer);

    if (videoBuffer != NULL)
    {
        [self.videoRenderer processFrame];
    }

    if (saveSettled && self.saveUpdateHandler)
    {
        // The game finished writing its save chip: let Delta write the .sav now (VBA-M's systemSaveUpdateCounter).
        self.saveUpdateHandler();
    }
}

- (nullable NSData *)readMemoryAtAddress:(NSInteger)address size:(NSInteger)size
{
    const uint8_t *bytes = dl_memory((long)address, (long)size);
    if (bytes == NULL)
    {
        return nil;
    }

    NSData *data = [NSData dataWithBytesNoCopy:(void *)bytes length:size freeWhenDone:NO];
    return data;
}

#pragma mark - Inputs -

- (void)activateInput:(NSInteger)gameInput value:(double)value playerIndex:(NSInteger)playerIndex
{
    self.activatedInputs |= (uint32_t)gameInput;
    dl_set_keys(self.activatedInputs);
}

- (void)deactivateInput:(NSInteger)gameInput playerIndex:(NSInteger)playerIndex
{
    self.activatedInputs &= ~((uint32_t)gameInput);
    dl_set_keys(self.activatedInputs);
}

- (void)resetInputs
{
    self.activatedInputs = 0;
    dl_set_keys(0);
}

#pragma mark - Game Saves -

- (void)saveGameSaveToURL:(NSURL *)URL
{
    // Raw save chip dump (FireRed: 128 KB flash), byte-compatible with the .sav files VBA-M wrote.
    dl_save_battery(URL.fileSystemRepresentation);
}

- (void)loadGameSaveFromURL:(NSURL *)URL
{
    dl_load_battery(URL.fileSystemRepresentation);
}

#pragma mark - Save States -

- (void)saveSaveStateToURL:(NSURL *)URL
{
    dl_save_state(URL.fileSystemRepresentation);
}

- (void)loadSaveStateFromURL:(NSURL *)URL
{
    dl_load_state(URL.fileSystemRepresentation);
}

#pragma mark - Cheats -

- (BOOL)addCheatCode:(NSString *)cheatCode type:(NSString *)type
{
    // gpSP runs unencrypted Code Breaker codes only.
    if (![type isEqualToString:CheatTypeCodeBreaker])
    {
        return NO;
    }

    NSMutableCharacterSet *legalCharactersSet = [NSMutableCharacterSet hexadecimalCharacterSet];
    [legalCharactersSet addCharactersInString:@" "];

    NSArray<NSString *> *codes = [cheatCode componentsSeparatedByString:@"\n"];
    for (NSString *code in codes)
    {
        if ([code rangeOfCharacterFromSet:[legalCharactersSet invertedSet]].location != NSNotFound || code.length != 13)
        {
            return NO;
        }
    }

    return dl_cheat_add(cheatCode.UTF8String);
}

- (void)resetCheats
{
    dl_cheats_reset();
}

- (void)updateCheats
{
}

#pragma mark - Link -

- (void)advertiseLink
{
    // Publishing a Bonjour service makes iOS ask for Local Network access on the first run (without that
    // permission the Kindle's connection is silently dropped). It also lets a client find the phone by name.
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.bonjourService != nil)
        {
            return;
        }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSNetService *service = [[NSNetService alloc] initWithDomain:@"local." type:GBALinkBonjourType name:@"Delta Link" port:(int)GBALinkPort];
#pragma clang diagnostic pop
        service.delegate = self;
        [service publish];
        self.bonjourService = service;
    });
}

- (void)stopAdvertisingLink
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.bonjourService stop];
        self.bonjourService.delegate = nil;
        self.bonjourService = nil;
    });
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-implementations"
- (void)netService:(NSNetService *)sender didNotPublish:(NSDictionary<NSString *, NSNumber *> *)errorDict
{
    NSLog(@"[GBADeltaCore] Could not advertise Delta Link over Bonjour: %@", errorDict);
}
#pragma clang diagnostic pop

#pragma mark - Getters/Setters -

- (NSTimeInterval)frameDuration
{
    return self.cachedFrameDuration;
}

@end
