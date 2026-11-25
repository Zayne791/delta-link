#!/usr/bin/env python3
# make_gpsp_project.py: turns the upstream GBADeltaCore.xcodeproj (VBA-M) into the Delta Link one (gpSP).
# Run once on the upstream project file (it was; the result is committed). Needs `pip install pbxproj`
# (mod-pbxproj, round-trips project files byte for byte).
#   python3 Tools/make_gpsp_project.py GBADeltaCore.xcodeproj/project.pbxproj
#
# - static library target "libVBA-M" becomes "libgpSP": compiles gpSP (interpreter only, no HAVE_DYNAREC),
#   its libretro-common bits, the built-in BIOS as C (Bridge/gpsp_bios.c) and the C bridge (Bridge/DLGBACore.c)
# - framework target GBADeltaCore: drops SFML + GBASoundDriver + vba-over.ini, keeps GBAEmulatorBridge.mm etc.
# - every VBA-M / SFML file reference and group is removed
import re, sys
from pbxproj import XcodeProject
from pbxproj.pbxextensions import FileOptions
from pbxproj.pbxextensions.ProjectFiles import ProjectFiles
ProjectFiles._FILE_TYPES.setdefault('.cc', ('sourcecode.cpp.cpp', 'PBXSourcesBuildPhase'))   # gpSP's C++ files

path = sys.argv[1]
p = XcodeProject.load(path)
O = p.objects

LIB_TARGET = 'BF287FF11EB590F90027FC53'
LIB_SOURCES = 'BF287FEE1EB590F90027FC53'
LIB_PRODUCT = 'BF287FF21EB590F90027FC53'
LIB_GROUP = 'BF287FF31EB590F90027FC53'          # "libVBA-M" group (VBA-M + SFML subgroups)
FW_TARGET = 'BFE8E9C91D010AF7009D623D'
MAIN_GROUP = 'BFE8E9C01D010AF7009D623D'
BRIDGE_GROUP = 'BF04172F1D01D99900E85BCF'
EXTRA_REMOVE = ['BF28805D1EB592A70027FC53',     # vba-over.ini
                'BF7A908B1D02708700A3617D',     # GBASoundDriver.h
                'BF7A908A1D02708700A3617D']     # GBASoundDriver.mm

assert O[LIB_TARGET].name == 'libVBA-M', 'already converted?'

# ---- collect everything under the libVBA-M group ----
groups, files = [], list(EXTRA_REMOVE)
def walk(gid):
    groups.append(gid)
    for c in O[gid].children:
        if O[c].isa in ('PBXGroup', 'PBXVariantGroup'): walk(c)
        else: files.append(c)
walk(LIB_GROUP)
files = set(files)

# ---- drop their build files from every phase, then the build files, file refs and groups ----
build_files = {bid for bid in O.get_keys() if O[bid].isa == 'PBXBuildFile' and O[bid].fileRef in files}
for oid in list(O.get_keys()):
    o = O[oid]
    if o.isa.endswith('BuildPhase') and hasattr(o, 'files'):
        for bid in list(o.files):
            if bid in build_files: o.files.remove(bid)
for oid in list(O.get_keys()):
    o = O[oid]
    if o.isa in ('PBXGroup', 'PBXVariantGroup'):
        for c in list(o.children):
            if c in files or c in groups: o.children.remove(c)
for oid in build_files | files | set(groups): del O[oid]

# ---- the libgpSP sources ----
G = 'gpsp'; LC = G + '/libretro/libretro-common'
gpsp_sources = [f'{G}/{s}' for s in ('video.cc', 'cpu.cc', 'main.c', 'gba_memory.c', 'savestate.c', 'input.c', 'sound.c',
                'cheats.c', 'memmap.c', 'serial.c', 'gbp.c', 'rfu.c', 'serial_proto.c', 'libretro/libretro.c', 'gba_cc_lut.c')]
gpsp_sources += [f'{LC}/{s}' for s in ('compat/compat_posix_string.c', 'compat/compat_strl.c', 'compat/fopen_utf8.c',
                 'encodings/encoding_utf.c', 'file/file_path.c', 'file/file_path_io.c', 'streams/file_stream.c',
                 'string/stdstring.c', 'time/rtime.c', 'vfs/vfs_implementation.c')]

O[LIB_TARGET].name = 'libgpSP'
O[LIB_TARGET].productName = 'libgpSP'
O[LIB_PRODUCT].path = 'libgpSP.a'
O['BF2881011EB5962B0027FC53'].remoteInfo = 'libgpSP'   # container proxy behind the framework's dependency on it

gpsp_group = p.add_group('gpSP', path=None, parent=O[MAIN_GROUP], source_tree='<group>')
opts = FileOptions(create_build_files=True)
def add(path, parent, target):
    refs = p.add_file(path, parent=parent, tree='SOURCE_ROOT', target_name=target, force=False, file_options=opts)
    assert refs, path
for s in gpsp_sources: add(s, gpsp_group, 'libgpSP')

bridge = O[BRIDGE_GROUP]
for name, target in (('DLGBACore.c', 'libgpSP'), ('gpsp_bios.c', 'libgpSP')):
    add('GBADeltaCore/Bridge/' + name, bridge, target)
for name in ('DLGBACore.h', 'glink.h'):
    p.add_file('GBADeltaCore/Bridge/' + name, parent=bridge, tree='SOURCE_ROOT', target_name=None, force=False,
               file_options=FileOptions(create_build_files=False))

# ---- build settings ----
GPSP_DEFS = ['-DHAVE_STRINGS_H', '-DHAVE_STDINT_H', '-DHAVE_INTTYPES_H', '-D__LIBRETRO__', '-DINLINE=inline',
             '-DFRONTEND_SUPPORTS_RGB565', '-DIOS', '-DHAVE_POSIX_MEMALIGN', '-fno-strict-aliasing']
for cid in O[O[LIB_TARGET].buildConfigurationList].buildConfigurations:
    bs = O[cid].buildSettings
    bs['PRODUCT_NAME'] = 'gpSP'
    bs['OTHER_CFLAGS'] = GPSP_DEFS                      # no $(inherited): the project-level flags were VBA-M's
    bs['OTHER_CPLUSPLUSFLAGS'] = ['$(OTHER_CFLAGS)', '-fno-exceptions', '-fno-rtti']
    bs['CLANG_CXX_LANGUAGE_STANDARD'] = 'gnu++11'
    bs['GCC_C_LANGUAGE_STANDARD'] = 'gnu99'
    bs['GCC_OPTIMIZATION_LEVEL'] = '3'                  # an interpreter: speed matters
    bs['CLANG_ENABLE_MODULES'] = 'NO'                   # plain C/C++ like gpSP's own Makefile build
    bs['HEADER_SEARCH_PATHS'] = ['"$(SRCROOT)/gpsp/libretro"', '"$(SRCROOT)/gpsp/libretro/libretro-common/include"',
                                 '"$(SRCROOT)/gpsp"', '"$(SRCROOT)/GBADeltaCore/Bridge"']
    bs['USER_HEADER_SEARCH_PATHS'] = ''
    bs['ALWAYS_SEARCH_USER_PATHS'] = 'NO'
    if 'OTHER_LDFLAGS' in bs: del bs['OTHER_LDFLAGS']
    bs['GCC_TREAT_WARNINGS_AS_ERRORS'] = 'NO'
    bs['GCC_WARN_ABOUT_RETURN_TYPE'] = 'YES'            # project sets YES_ERROR; third-party C stays warnings
    bs['CLANG_WARN_STRICT_PROTOTYPES'] = 'NO'
    bs['CLANG_WARN_COMMA'] = 'NO'

# Project level: drop VBA-M's defines and header paths (the framework target only builds ObjC/Swift now)
proj = O[p.rootObject]
for cid in O[proj.buildConfigurationList].buildConfigurations:
    bs = O[cid].buildSettings
    for k in ('OTHER_CFLAGS', 'HEADER_SEARCH_PATHS', 'USER_HEADER_SEARCH_PATHS'):
        if k in bs: del bs[k]

p.save(path)

# ---- sanity: every referenced object id exists, and nothing VBA-M/SFML is left ----
text = open(path).read()
defined = set(re.findall(r'^\t\t([0-9A-F]{24}) ', text, re.M))
used = set(re.findall(r'\b([0-9A-F]{24})\b', re.sub(r'remoteGlobalIDString = \w+;', '', text)))   # remote ids live in DeltaCore.xcodeproj
missing = used - defined
assert not missing, f'dangling ids: {sorted(missing)[:10]}'
for bad in ('visualboyadvance-m', 'SFML', 'GBASoundDriver', 'vba-over'):
    assert bad not in text, bad
print(f'ok: removed {len(files)} files, {len(groups)} groups, {len(build_files)} build files; added {len(gpsp_sources) + 2} sources')
