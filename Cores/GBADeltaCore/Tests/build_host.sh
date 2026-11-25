#!/bin/sh
# build_host.sh: compile GBADeltaCore's C side (gpSP interpreter + DLGBACore.c + glink.h) for this host and run
# the bridge test. Same sources and defines as the Xcode target, minus Objective-C.
#   Tests/build_host.sh ROM SAV PORT        (SAN=1 adds AddressSanitizer + UBSan)
set -e
cd "$(dirname "$0")/.."
B=${BUILD:-Tests/build}; mkdir -p "$B/obj"
G=gpsp; LC=$G/libretro/libretro-common
DEFS="-DHAVE_STRINGS_H -DHAVE_STDINT_H -DHAVE_INTTYPES_H -D__LIBRETRO__ -DINLINE=inline -DFRONTEND_SUPPORTS_RGB565 -DHAVE_POSIX_MEMALIGN"
INC="-I$G/libretro -I$LC/include -I$G -IGBADeltaCore/Bridge"
OPT="-O2 -g -fno-strict-aliasing"
[ -n "$SAN" ] && OPT="$OPT -fsanitize=address,undefined -fno-omit-frame-pointer"
SRC="$G/video.cc $G/cpu.cc $G/main.c $G/gba_memory.c $G/savestate.c $G/input.c $G/sound.c $G/cheats.c $G/memmap.c
 $G/serial.c $G/gbp.c $G/rfu.c $G/serial_proto.c $G/libretro/libretro.c $G/gba_cc_lut.c
 $LC/compat/compat_posix_string.c $LC/compat/compat_strl.c $LC/compat/fopen_utf8.c $LC/encodings/encoding_utf.c
 $LC/file/file_path.c $LC/file/file_path_io.c $LC/streams/file_stream.c $LC/string/stdstring.c $LC/time/rtime.c
 $LC/vfs/vfs_implementation.c GBADeltaCore/Bridge/gpsp_bios.c GBADeltaCore/Bridge/DLGBACore.c Tests/dl_host_test.c"
OBJS=""
for s in $SRC; do
  o=$B/obj/$(echo $s | tr / _).o; OBJS="$OBJS $o"
  case $s in
    *.cc) g++ -std=c++11 -fno-exceptions -fno-rtti $OPT $DEFS $INC -w -c $s -o $o & ;;
    GBADeltaCore/*|Tests/*) gcc -std=gnu99 $OPT $DEFS $INC -Wall -Wextra -Wno-unused-parameter -Werror -c $s -o $o & ;;
    *) gcc -std=gnu99 $OPT $DEFS $INC -w -c $s -o $o & ;;
  esac
done
wait
g++ $OPT -o $B/dl_host_test $OBJS -lm
[ $# -ge 3 ] && exec $B/dl_host_test "$@" $B || echo "built $B/dl_host_test"
