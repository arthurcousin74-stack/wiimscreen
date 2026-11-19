# PlatformIO extra script of the `windows` env (wiimscreen.exe). See platformio.ini and docs/DESKTOP.md.
#
# - On Windows (CI: MSYS2 UCRT64), the compiler in PATH is MinGW-w64 gcc: nothing to replace.
# - From Linux, cross-compile with a MinGW-w64 toolchain (llvm-mingw, or gcc-mingw-w64):
#     WINDOWS_CROSS=<dir>/bin/x86_64-w64-mingw32-   prefix of the tools (gcc, g++, ar, ranlib, windres)
#     WINDOWS_SYSROOT=<dir>/ucrt64                  SDL2 and OpenSSL for Windows (include/, lib/): the MSYS2
#                                                   packages mingw-w64-ucrt-x86_64-SDL2 and -openssl, extracted
# The program is a GUI program (-mwindows: no console window) linked statically (SDL2, OpenSSL, the C++ runtime):
# the zip holds wiimscreen.exe and cacert.pem only. The resource file adds the manifest (UTF-8 code page) and the
# version information.
import os
import re

Import("env")  # noqa: F821 (SCons)

PROJECT = env.subst("$PROJECT_DIR")  # noqa: F821
prefix = os.environ.get("WINDOWS_CROSS", "")
sysroot = os.environ.get("WINDOWS_SYSROOT")
if sysroot:
    env.Append(CPPPATH=[os.path.join(sysroot, "include")], LIBPATH=[os.path.join(sysroot, "lib")])  # noqa: F821

env.Replace(PROGNAME="wiimscreen")  # noqa: F821

# M5GFX headers use std::abort, fopen_s and fread without including <cstdlib>/<cstdio> (libc++ does not include
# them on the way; libstdc++ does)
env.Append(CXXFLAGS=["-include", "cstdlib", "-include", "cstdio"])  # noqa: F821
if prefix and os.path.exists(prefix + "clang"):
    # llvm-mingw: M5GFX headers, included by the project's files, warn with clang only (not with gcc). After the
    # project's -Wall -Wextra (build_src_flags), so that CHECK_WERROR=1 builds
    flags = env.get("SRC_BUILD_FLAGS", "")  # noqa: F821
    extra = ["-Wno-vla-cxx-extension", "-Wno-deprecated-copy"]
    env["SRC_BUILD_FLAGS"] = flags + extra if isinstance(flags, list) else flags + " " + " ".join(extra)  # noqa: F821

# Static SDL2 and OpenSSL, then the Windows libraries they need (sdl2.pc and libcrypto.pc, Libs.private)
env.Append(  # noqa: F821
    LINKFLAGS=["-static", "-mwindows", "-Wl,--gc-sections"],
    CCFLAGS=["-ffunction-sections", "-fdata-sections"],  # unused M5GFX fonts (about 40 MB) are not linked
    LIBS=["SDL2", "ssl", "crypto", "m", "kernel32", "user32", "gdi32", "winmm", "imm32", "ole32", "oleaut32",
          "version", "uuid", "advapi32", "setupapi", "shell32", "dinput8", "ws2_32", "crypt32"])

# Version shown in the file's properties: tools/version.py (local) or FW_VERSION in PLATFORMIO_BUILD_FLAGS (CI)
version = "dev"
m = re.search(r'FW_VERSION=\\*"?([\w.+-]+)', os.environ.get("PLATFORMIO_BUILD_FLAGS", ""))
if m:
    version = m.group(1)
for d in env.get("CPPDEFINES", []):  # noqa: F821
    if isinstance(d, (tuple, list)) and d[0] == "FW_VERSION":
        version = str(d[1]).strip('\\"')
build_dir = env.subst("$BUILD_DIR")  # noqa: F821
os.makedirs(build_dir, exist_ok=True)
version_h = os.path.join(build_dir, "wiimscreen_version.h")
text = '#define WIIMSCREEN_VERSION "%s"\n' % version
if not os.path.exists(version_h) or open(version_h).read() != text:
    with open(version_h, "w") as f:
        f.write(text)

# Resources: manifest and version (platform/windows/wiimscreen.rc), linked as one more object
rc_dir = os.path.join(PROJECT, "platform", "windows")
res = env.Command(  # noqa: F821
    os.path.join(build_dir, "wiimscreen_res.o"), os.path.join(rc_dir, "wiimscreen.rc"),
    '"%swindres" -I "%s" -I "%s" -O coff -i "$SOURCE" -o "$TARGET"' % (prefix, rc_dir, build_dir))
env.Depends(res, [version_h, os.path.join(rc_dir, "wiimscreen.manifest")])  # noqa: F821
env.Append(LINKFLAGS=[res[0].get_abspath()])  # noqa: F821

# The native platform looks for gcc again after the pre: scripts (its builder/main.py), then calls BuildProgram:
# the cross tools are set just before, and the program waits for the resources
build_program = env.BuildProgram  # noqa: F821


def windows_build_program(e):
    if prefix:
        e.Replace(CC=prefix + "gcc", CXX=prefix + "g++", AS=prefix + "as", AR=prefix + "ar",
                  RANLIB=prefix + "ranlib", LINK=prefix + "g++", PROGSUFFIX=".exe")
    program = build_program()
    e.Depends(program, res)
    return program


env.AddMethod(windows_build_program, "BuildProgram")  # noqa: F821
