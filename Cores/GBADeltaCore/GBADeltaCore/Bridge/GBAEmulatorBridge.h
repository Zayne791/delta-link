//
//  GBAEmulatorBridge.h
//  GBADeltaCore
//
//  Created by Riley Testut on 6/3/16.
//  Copyright © 2016 Riley Testut. All rights reserved.
//

#import <Foundation/Foundation.h>

@protocol DLTAEmulatorBridging;

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, GBALinkState)
{
    GBALinkStateOff = 0,      // no game running, or the port could not be opened
    GBALinkStateWaiting = 1,  // listening for the Kindle
    GBALinkStateLinked = 2,   // the Kindle is connected and running the same gpSP
};

FOUNDATION_EXPORT NSInteger const GBALinkPort;   // 5739 (GLINK_PORT)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything" // Silence "Cannot find protocol definition" warning due to forward declaration.
@interface GBAEmulatorBridge : NSObject <DLTAEmulatorBridging>
#pragma clang diagnostic pop

@property (class, nonatomic, readonly) GBAEmulatorBridge *sharedBridge;

// Delta Link: while a GBA game runs, the bridge hosts the link cable (player 0) on TCP port 5739 so a Kindle
// running the same gpSP can join (Wireless Club / Union Room trades). Changes post GBALinkStateDidChangeNotification.
@property (nonatomic, readonly) GBALinkState linkState;
@property (nonatomic, copy, readonly, nullable) NSString *linkError;   // why the last link dropped, if it did
@property (nonatomic, getter=isLinkEnabled) BOOL linkEnabled;          // default YES; read when a game starts

// When non-nil, Delta looks here for patches with same name as ROM.
@property (nonatomic, copy, nullable) NSURL *patchesDirectoryURL;

@end

NS_ASSUME_NONNULL_END
