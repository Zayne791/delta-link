//
//  GBA.swift
//  GBADeltaCore
//
//  Created by Riley Testut on 6/3/16.
//  Copyright © 2016 Riley Testut. All rights reserved.
//

import Foundation
import AVFoundation

import DeltaCore

public extension GBA
{
    static let didActivateGyroNotification = NSNotification.Name.__GBADidActivateGyro
    static let didDeactivateGyroNotification = NSNotification.Name.__GBADidDeactivateGyro
    static let didEnableSavingNotification = NSNotification.Name.__GBADidEnableSaving
    static let didDisableSavingNotification = NSNotification.Name.__GBADidDisableSaving
    
    /// Delta Link: posted (on the main thread) when the Kindle links or the link drops. Object: GBAEmulatorBridge.
    static let linkStateDidChangeNotification = NSNotification.Name.__GBALinkStateDidChange
}

@objc public enum GBAGameInput: Int, Input
{
    case up = 64
    case down = 128
    case left = 32
    case right = 16
    case a = 1
    case b = 2
    case l = 512
    case r = 256
    case start = 8
    case select = 4
    
    public var type: InputType {
        return .game(.gba)
    }
}

public struct GBA: DeltaCoreProtocol
{
    public static let core = GBA()
    
    public var name: String { "GBADeltaCore" }
    public var identifier: String { "com.rileytestut.GBADeltaCore" }
    
    public var gameType: GameType { GameType.gba }
    public var gameInputType: Input.Type { GBAGameInput.self }
    public var gameSaveFileExtension: String { "sav" }
    
    public let audioFormat = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: 32768, channels: 2, interleaved: true)!
    public let videoFormat = VideoFormat(format: .bitmap(.bgra8), dimensions: CGSize(width: 240, height: 160))
    
    public var supportedCheatFormats: Set<CheatFormat> {
        // Delta Link: gpSP only runs (unencrypted) Code Breaker codes; VBA-M also took Action Replay and GameShark.
        let codeBreakerFormat = CheatFormat(name: NSLocalizedString("Code Breaker", comment: ""), format: "XXXXXXXX YYYY", type: .codeBreaker)
        return [codeBreakerFormat]
    }
    
    public let emulatorBridge: EmulatorBridging = GBAEmulatorBridge.shared
    
    private init()
    {
    }
}

// Expose DeltaCore properties to Objective-C.
public extension GBAEmulatorBridge
{
    @objc(gbaResources) class var __gbaResources: Bundle {
        return GBA.core.resourceBundle
    }
    
    @objc(coreDirectoryURL) class var __coreDirectoryURL: URL {
        return _coreDirectoryURL
    }
}

private let _coreDirectoryURL = GBA.core.directoryURL
