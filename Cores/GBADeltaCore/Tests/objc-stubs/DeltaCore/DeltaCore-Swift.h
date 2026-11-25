// What Swift generates for DeltaCore's @objc protocols (EmulatorBridging.swift, AudioRendering.swift, VideoRendering.swift, RingBuffer.swift)
#pragma once
#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface DLTARingBuffer : NSObject
- (NSInteger)writeBuffer:(void const *)buffer size:(NSInteger)size;
@end
@protocol DLTAAudioRendering <NSObject>
@property (nonatomic, readonly, strong) DLTARingBuffer *audioBuffer;
@end
@protocol DLTAVideoRendering <NSObject>
@property (nonatomic, readonly) uint8_t * _Nullable videoBuffer;
- (void)prepare;
- (void)processFrame;
@end
@protocol DLTAEmulatorBridging <NSObject>
@property (nonatomic, readonly, copy) NSURL * _Nullable gameURL;
@property (nonatomic, readonly) NSTimeInterval frameDuration;
@property (nonatomic, strong) id <DLTAAudioRendering> _Nullable audioRenderer;
@property (nonatomic, strong) id <DLTAVideoRendering> _Nullable videoRenderer;
@property (nonatomic, copy) void (^ _Nullable saveUpdateHandler)(void);
- (void)startWithGameURL:(NSURL *)gameURL;
- (void)stop; - (void)pause; - (void)resume;
- (void)runFrameAndProcessVideo:(BOOL)processVideo;
- (void)activateInput:(NSInteger)input value:(double)value playerIndex:(NSInteger)playerIndex;
- (void)deactivateInput:(NSInteger)input playerIndex:(NSInteger)playerIndex;
- (void)resetInputs;
- (void)saveSaveStateToURL:(NSURL *)url;
- (void)loadSaveStateFromURL:(NSURL *)url;
- (void)saveGameSaveToURL:(NSURL *)url;
- (void)loadGameSaveFromURL:(NSURL *)url;
- (BOOL)addCheatCode:(NSString *)cheatCode type:(NSString *)type;
- (void)resetCheats;
- (void)updateCheats;
@optional
- (NSData * _Nullable)readMemoryAtAddress:(NSInteger)address size:(NSInteger)size;
@end
NS_ASSUME_NONNULL_END
