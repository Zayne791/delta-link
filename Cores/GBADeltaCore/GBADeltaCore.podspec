Pod::Spec.new do |spec|
  spec.name         = "GBADeltaCore"
  spec.version      = "0.1"
  spec.summary      = "Game Boy Advance plug-in for Delta emulator."
  spec.description  = "iOS framework that wraps gpSP (interpreter) to play Game Boy Advance games in Delta, with the Delta Link Wi-Fi link cable. (Delta builds this core from GBADeltaCore.xcodeproj; this podspec is not used.)"
  spec.homepage     = "https://github.com/rileytestut/GBADeltaCore"
  spec.platform     = :ios, "14.0"
  spec.source       = { :git => "https://github.com/rileytestut/GBADeltaCore.git" }

  spec.author             = { "Riley Testut" => "riley@rileytestut.com" }
  spec.social_media_url   = "https://twitter.com/rileytestut"
  
  spec.source_files  = "GBADeltaCore/**/*.{h,m,mm,c,swift}",
                       "gpsp/{video,cpu}.cc", "gpsp/{main,gba_memory,savestate,input,sound,cheats,memmap,serial,gbp,rfu,serial_proto,gba_cc_lut}.c", "gpsp/*.h", "gpsp/libretro/libretro.c", "gpsp/libretro/*.h",
                       "gpsp/libretro/libretro-common/{compat/compat_posix_string,compat/compat_strl,compat/fopen_utf8,encodings/encoding_utf,file/file_path,file/file_path_io,streams/file_stream,string/stdstring,time/rtime,vfs/vfs_implementation}.c",
                       "gpsp/libretro/libretro-common/include/**/*.h"
  spec.public_header_files = "GBADeltaCore/Types/GBATypes.h", "GBADeltaCore/Bridge/GBAEmulatorBridge.h", "GBADeltaCore/GBADeltaCore.h"
  spec.header_mappings_dir = ""
  spec.resource_bundles = {
    "GBADeltaCore" => ["GBADeltaCore/**/*.deltamapping", "GBADeltaCore/**/*.deltaskin"]
  }
  
  spec.dependency 'DeltaCore'
  
  spec.xcconfig = {
    "HEADER_SEARCH_PATHS" => '"${PODS_CONFIGURATION_BUILD_DIR}" "$(PODS_TARGET_SRCROOT)/gpsp" "$(PODS_TARGET_SRCROOT)/gpsp/libretro" "$(PODS_TARGET_SRCROOT)/gpsp/libretro/libretro-common/include"',
    "GCC_PREPROCESSOR_DEFINITIONS" => "STATIC_LIBRARY=1 HAVE_STRINGS_H HAVE_STDINT_H HAVE_INTTYPES_H __LIBRETRO__ INLINE=inline FRONTEND_SUPPORTS_RGB565 IOS HAVE_POSIX_MEMALIGN"
  }
  
end
