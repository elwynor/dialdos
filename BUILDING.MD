# Building DS-DOS Shell

DS-DOS Shell is a Win32 DLL module for The Major BBS v10. It is built with
Microsoft Visual Studio against the MBBS v10 Module SDK.

## Prerequisites

- **Microsoft Visual Studio 2022** (or Build Tools 2022) with the C++
  desktop workload. Platform toolset **v143**.
- **Windows 10 SDK** (`WindowsTargetPlatformVersion` 10.0).
- **The Major BBS v10 Module SDK**
  (https://github.com/TheMajorBBS/MBBS-v10-module-SDK). It provides the
  headers (`inc/`) and import libraries (`lib/importlibs`, `lib/v10`).
- **Python 3** for the message-header and packaging scripts in `TOOLS/`.

> **Build for x86 (Win32) only. Never x64.** The Major BBS v10 is a 32-bit
> program and cannot load a 64-bit module.

## Setup

Copy `MBBSSDK.props.example` to `MBBSSDK.props` and set `MbbsSdkDir` to your
SDK folder (trailing backslash required). `MBBSSDK.props` is not committed.

## Message file

The module's text is in `SRC/DIALDOS/DIALDOS.MSG`, and
`SRC/DIALDOS/DIALDOS.H` holds the matching message numbers. `DIALDOS.MSG`
contains raw ESC bytes and CP437 characters and must keep CRLF line
endings. Edit it with an editor that preserves both, or with a script that
reads and writes bytes.

Message numbers come from each entry's position in the file, so adding,
removing or reordering an entry changes the numbers after it. After any
such change, regenerate the header and rebuild:

```
python TOOLS\MKMSG.PY
```

`MKMSG.PY` also restores CRLF line endings and rejects names over 8
characters, duplicate names and a repeated `LEVELn` marker. Deploy the DLL
and `.MSG` together. The BBS compiles `DIALDOS.MCV` from the `.MSG` itself.

## Build

In Visual Studio, open `DIALDOS.sln`, choose **Release** / **Win32**, and
build. Or from a command prompt:

```
MSBuild.exe DIALDOS.sln -p:Configuration=Release -p:Platform=Win32
```

The DLL is written to `BUILD/Release/DIALDOS.dll`. The build should finish
with no warnings.

## Tests

```
TESTS\RUN_TESTS.CMD
```

This compiles and runs `TESTS/TEST_FNSPLIT.C`, which checks the
Borland-compatible `fnsplit()` and the Borland path sizes that the data
file layout depends on. Exit code 0 means every test passed. The rest of
the module needs a running BBS to test.

## Packaging a release

1. Set the new version in `SRC/DIALDOS/DIALDOSH.H` (`DIALDOS_VERSION`) and
   `SRC/DIALDOS/DIALDOS.rc` (`FILEVERSION`, `PRODUCTVERSION` and the
   version strings). Update `CHANGELOG.MD` and the revision history in
   `SRC/DIALDOS/DIALDOS.RLN`.
2. Run `python TOOLS\MKDIST.PY`. It regenerates the message header, runs
   the tests, and rebuilds the Release configuration with warnings treated
   as errors. It checks that the DLL's version matches `DIALDOS_VERSION`,
   that it is a 32-bit DLL exporting `_init__dialdos`, and that the text
   files have CRLF line endings. Then it copies the deployable files into
   `DIST/<version>/` and zips them as `DIST/DIALDOS-<version>.zip`.
3. Attach the zip to the GitHub release.

The deployable files are `DIALDOS.DLL`, `DIALDOS.MDF`, `DIALDOS.MSG`,
`DIALDOS.RLN` and `GCVIRDAT\DIALDOS.VIR`. The engine's `dfaVirgin()`
only looks for virgin files in the BBS's `GCVIRDAT` folder.

`MKDIST.PY` refuses to write into a `DIST/<version>/` folder that already
exists. Never rebuild under a version number that has already been
released. Bump the version instead.

## Release checklist

- Project builds with no errors or warnings, and the tests pass.
- Version numbers updated (`DIALDOSH.H`, `DIALDOS.rc`, `CHANGELOG.MD`,
  `DIALDOS.RLN`).
- `README.MD` updated.
- `DIST/<version>/` holds only the five deployable files.
- DLL properties show the right product, version, company and copyright.
- Installation tested on a test board.
- No credentials, internal paths or internal notes in published files.
