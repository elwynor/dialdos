# Changelog

All notable changes to DS-DOS Shell are recorded here. The full history
back to 1996 is in `SRC/DIALDOS/DIALDOS.RLN`.

## [3.9.0] - 2026-11-12

First release for The Major BBS v10, converted from the Worldgroup 3
release (3.8) by Mark Laudenbach.

### Changed
- Built for The Major BBS v10 with Visual Studio 2022 (Win32), from the
  Borland C++ Worldgroup source.
- Replaced the Borland run-time helpers that Visual Studio lacks (`fnsplit`,
  `getdisk`, `getcurdir`, `getfat`, `_dos_gettime`) with versions that give
  the same results.
- Kept the Borland path sizes (`MAXPATH` 80, `MAXDIR` 66, ...), so the
  `DIALDOS.DAT` record layout matches the Worldgroup releases. A build-time
  check fails if either record changes size.
- On first start, `DIALDOS.DAT` is created from `GCVIRDAT\DIALDOS.VIR` (or
  from `DIALDOS.VIR` in the BBS directory). This replaces
  the old `DIALDOS.BAT` install step.
- The version is shown as `DS-DOS Shell 3.9.0` by `VER` and `$V`, written to
  the Audit Trail at startup, and stored in the DLL's version information.
- Lock checks compare `haskey()` with 1. On The Major BBS v10, `haskey()`
  returns -1 for a lock that does not exist, and the old `!haskey()` test
  let users through where Worldgroup kept them out.
- The release notes and the module description file are updated for The
  Major BBS v10.

### Removed
- The activation code check, the 14-day demo and the "demo expired"
  screens (`REGNOW`, `ISDEMO`, `NOTREG`).
- The activation code field and the "call DialSoft" line from the `CONFIG`
  screen.
- The install-day e-mail that sent the BBS's name, address and phone
  numbers to DialSoft.
- The DOS and MajorBBS 6.25 build branches and their DLL-switching install
  batch file.

### Fixed
- `ATTRIB +/-` changes file attributes again. The Windows NT build ignored
  it.
- `TIME` shows the current time. The Windows NT build showed uninitialized
  values.
- DIR and CHKDSK free space is shown in bytes as labelled (it was
  kilobytes), and is correct on drives over 2 TB.
- Large numbers get their commas in the right places (`1,234,567`, not
  `123,4567`).
- The COPY percentage meter works on files over 21 MB.
- Long Windows file names no longer overrun fixed buffers. This covered
  COPY/MOVE destination names, source and destination paths, and the
  directory list used by `DIR /S`, `XCOPY /S` and `DELTREE`. It also
  covered the engine's file-transfer name buffers and the 120-byte `spr()`
  buffer.
- A BBS with no `PROMPT` environment variable gives new users the `$p$g`
  prompt. Before, setting up the user's record copied from a null pointer.
- `CHKDSK` no longer divides by zero if the drive geometry cannot be read.
- `GREP` no longer tests an uninitialized variable.
- `$H` in `PROMPT` is a backspace (it printed `8`).
- The current directory and version in the prompt are printed as text, so a
  `%` in a directory name is not treated as a format code.
- A long Windows name typed as an argument is refused with "Invalid path"
  or "Path not found". Before, it was cut to 8.3, so `DEL
  REPORT_2024_OLD.DOC` could delete `REPORT_2.DOC`. Directories, COPY/MOVE
  targets and DELTREE paths too long for DS-DOS's buffers are skipped or
  refused instead of being cut short. DELTREE could delete files in
  another directory this way.
- MOVE keeps the source file if the copy fails (for example, a full disk),
  and COPY reports the error and stops.
- `EDIT` refuses a file too large for the editor. Before, it opened the
  first 32,000 bytes without a warning, and saving lost the rest.
- A user restricted to a home directory can no longer reach a sibling
  directory whose name starts the same way (`C:\USERS\BOB` no longer
  admits `C:\USERS\BOBBY`).
- `DEBUG` needs the sysop key. Any DS-DOS user could turn it on for the
  whole board and clear the editor lock.
- A caller who drops while in DS-DOS no longer leaves files open, or the
  editor locked for everyone until the BBS restarts.
- The password prompt for a sysop-key holder with no DS-DOS account
  accepts only the master password.
- DIR and CHKDSK totals are correct past 2 GB, and files from 2 GB to 4 GB
  show their real size.
- CHKDSK no longer counts directories left over from an interrupted
  `DIR /S` or `XCOPY /S`.
- Deleting a BBS account no longer leaves the BBS's account-deletion code
  working on the wrong data file afterwards.
