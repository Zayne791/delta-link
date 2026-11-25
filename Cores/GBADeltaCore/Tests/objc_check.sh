#!/bin/sh
# objc_check.sh: type-check GBADeltaCore's Objective-C(++) on Linux with clang -fsyntax-only, against minimal
# stand-ins for Foundation/UIKit and the Swift-generated DeltaCore header (Tests/objc-stubs). Not a build: it
# catches typos, wrong selectors and type errors before a macOS CI run. Needs clang and Cores/DeltaCore.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cp -r Tests/objc-stubs/. "$T/"
mkdir -p "$T/GBADeltaCore"
for h in GBADeltaCore/GBADeltaCore.h GBADeltaCore/Bridge/GBAEmulatorBridge.h GBADeltaCore/Types/GBATypes.h; do ln -s "$PWD/$h" "$T/GBADeltaCore/"; done
echo '#pragma once' > "$T/GBADeltaCore/GBADeltaCore-Swift.h"
ln -s "$PWD/../DeltaCore/DeltaCore/include/DeltaTypes.h" "$T/DeltaCore/DeltaTypes.h"
F="-fsyntax-only -fobjc-arc -fblocks -fobjc-runtime=macosx-14.0 -I$T -Wall -Wextra -Wno-unused-parameter -Wno-nullability-completeness -Werror"
clang++ -x objective-c++ -std=gnu++11 $F -IGBADeltaCore/Bridge GBADeltaCore/Bridge/GBAEmulatorBridge.mm
clang -x objective-c $F -IGBADeltaCore/Types GBADeltaCore/Types/GBATypes.m
echo "objc ok"
