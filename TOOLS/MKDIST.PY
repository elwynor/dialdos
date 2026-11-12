# Build 26.11.12.4 05:15PM
"""
MKDIST.PY - package a release of DS-DOS Shell into DIST/<version>/.

DS-DOS Shell for The Major BBS v10
Copyright (C) 1996-2026 DialSoft Corp.

Steps:
  1. Check DIALDOS.MSG and regenerate DIALDOS.H (TOOLS/MKMSG.PY).
  2. Run the unit tests (TESTS/RUN_TESTS.CMD).
  3. Rebuild the Release|Win32 configuration with MSBuild.
  4. Check the DLL: its product version matches DIALDOS_VERSION in
     DIALDOSH.H, it is a 32-bit (x86) DLL, and it exports _init__dialdos.
     Check the text files kept their CRLF line endings.
  5. Copy the deployable files, with upper-case names, into DIST/<version>/
     and zip them as DIST/DIALDOS-<version>.zip.

A version number must name exactly one build, so this refuses to write
into a DIST/<version>/ folder that already exists - bump the version
instead of rebuilding under an old number.

Usage:  python TOOLS/MKDIST.PY [path-to-MSBuild.exe]   (from the project root)
"""

import glob
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "SRC", "DIALDOS")
VSROOT = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
MSBUILD_DEFAULT = os.path.join(VSROOT, r"MSBuild\Current\Bin\MSBuild.exe")

# (source file, name in the release) - deployable files only.
DEPLOY = [
    (os.path.join(ROOT, "BUILD", "Release", "DIALDOS.dll"), "DIALDOS.DLL"),
    (os.path.join(SRC, "DIALDOS.MDF"), "DIALDOS.MDF"),
    (os.path.join(SRC, "DIALDOS.MSG"), "DIALDOS.MSG"),
    (os.path.join(SRC, "DIALDOS.RLN"), "DIALDOS.RLN"),
    # The engine's dfaVirgin() looks for virgin files in GCVIRDAT\, so the
    # release puts DIALDOS.VIR there; unzipping into the BBS folder lands it
    # in the right place.
    (os.path.join(SRC, "DIALDOS.VIR"), "GCVIRDAT/DIALDOS.VIR"),
]

CRLF_FILES = ("DIALDOS.MDF", "DIALDOS.MSG", "DIALDOS.RLN")


def fail(text):
    sys.exit("MKDIST: " + text)


def version():
    with open(os.path.join(SRC, "DIALDOSH.H"), "rb") as f:
        m = re.search(rb'#define\s+DIALDOS_VERSION\s+"([^"]+)"', f.read())
    if not m:
        fail("DIALDOS_VERSION not found in DIALDOSH.H")
    return m.group(1).decode("ascii")


def check_crlf(path):
    with open(path, "rb") as f:
        data = f.read()
    if data.count(b"\n") != data.count(b"\r\n"):
        fail("%s has bare LF line endings; the BBS needs CRLF" % path)
    if b"\0" in data:
        fail("%s contains a NUL byte" % path)


def dumpbin(*args):
    tools = sorted(glob.glob(os.path.join(
        VSROOT, r"VC\Tools\MSVC\*\bin\Hostx86\x86\dumpbin.exe")))
    if not tools:
        fail("dumpbin.exe not found")
    return subprocess.check_output([tools[-1], "/nologo"] + list(args), text=True)


def main():
    ver = version()
    out = os.path.join(ROOT, "DIST", ver)
    if os.path.exists(out):
        fail("DIST/%s already exists - bump the version, do not rebuild "
             "under a number that may already be shared" % ver)

    subprocess.check_call([sys.executable, os.path.join(ROOT, "TOOLS", "MKMSG.PY")])
    subprocess.check_call(["cmd", "/c", os.path.join("TESTS", "RUN_TESTS.CMD")], cwd=ROOT)
    msbuild = sys.argv[1] if len(sys.argv) > 1 else MSBUILD_DEFAULT
    subprocess.check_call([msbuild, os.path.join(ROOT, "DIALDOS.sln"), "-t:Rebuild",
                           "-p:Configuration=Release", "-p:Platform=Win32",
                           "-nologo", "-v:minimal", "-warnaserror"])

    dll = DEPLOY[0][0]
    pv = subprocess.check_output(
        ["powershell", "-NoProfile", "-Command",
         "(Get-Item '%s').VersionInfo.ProductVersion" % dll], text=True).strip()
    if pv != ver:
        fail("DLL product version %r does not match DIALDOS_VERSION %r - "
             "update DIALDOS.rc" % (pv, ver))
    if "14C machine (x86)" not in dumpbin("/headers", dll):
        fail("DIALDOS.DLL is not a 32-bit (x86) DLL")
    if "_init__dialdos" not in dumpbin("/exports", dll):
        fail("DIALDOS.DLL does not export _init__dialdos")
    for name in CRLF_FILES:
        check_crlf(os.path.join(SRC, name))

    os.makedirs(out)
    for src, name in DEPLOY:
        dst = os.path.join(out, name)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
    zpath = os.path.join(ROOT, "DIST", "DIALDOS-%s.zip" % ver)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for _, name in DEPLOY:
            z.write(os.path.join(out, name), name)
    print("MKDIST: packaged v%s -> DIST/%s/ and %s" % (ver, ver,
                                                       os.path.basename(zpath)))


if __name__ == "__main__":
    main()
