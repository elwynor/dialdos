# DS-DOS Shell - Technical Notes

## Source layout

| File | Contents |
|---|---|
| `SRC/DIALDOS/DIALDOS.C` | The whole module: init, input handling, every command, the step-by-step job engine, the editor and form callbacks |
| `SRC/DIALDOS/DIALDOSH.H` | Data records, per-user session data (VDA), flag bits and the full-screen form field specs |
| `SRC/DIALDOS/COMMON.H` | Platform layer: SDK includes, the Borland run-time helpers Visual Studio lacks, `dspr()` |
| `SRC/DIALDOS/FNSPLIT.H` | Borland path sizes and a Borland-compatible `fnsplit()` (needs no SDK, so it can be unit tested) |
| `SRC/DIALDOS/DIALDOS.H` | Message numbers, generated from `DIALDOS.MSG` by `TOOLS/MKMSG.PY` |
| `SRC/DIALDOS/DIALDOS.MSG` | All screen text, help text and the three full-screen forms |
| `SRC/DIALDOS/DIALDOS.VIR` | Empty `DIALDOS.DAT`, copied on first start |
| `SRC/DIALDOS/DIALDOS.BCR`, `GALCAPS.ALT` | The Btrieve file definition `DIALDOS.VIR` was built from (reference) |

## How a session runs

- **Entry (`uinput`, substate 0).** A user with a record in `DIALDOS.DAT`
  gets in, after their own password if they have one. A user without a
  record needs the configured sysop key, and then either the master
  password (if enabled) or master access. `init_dos()` loads or creates
  the record. It then installs the character-input hook and shows the
  prompt.
- **Command line (substate 100).** Each line is added to the DOSKEY
  history and passed to `exec_cmd()`. The pipes `| MORE` and `| GREP` and
  the `> nul` redirection are handled first. Then the command is matched,
  with its access bit checked against `dosrec.security`.
- **Character hook (`chrprc`).** It turns cursor up and down into history
  recall, handles Ctrl-C and ESC, and filters control characters. It runs
  per keystroke, before the line is complete.
- **Long jobs.** DIR, DEL, ATTRIB, TYPE, REN, COPY, XCOPY, MOVE, CHKDSK,
  HISTORY, DELTREE, USERS and GREP never run in one go. All users share one
  thread, so a long loop would freeze the board. `begin_cycle(n)` sets
  substate *n* and injects a `CYCLE` event. The BBS then calls `usrsts()`,
  which does up to five steps (one file, or one block of a copy). It
  re-injects `CYCLE` while work remains, and holds off while the user's
  output buffer is full or a `-- More --` pause is pending. `end_cycle()`
  closes files and shows the prompt.
- **Recursion (`/S`).** Directories still to visit are kept as a
  comma-separated list in `dosvda->text`. Substate 11 pops the next one.
  `dirlist_add()` stops the job with "Out of memory" rather than overrun
  the buffer.
- **Batch files.** They run through `begin_polling(batch_poll)`, one line
  per poll, each fed to `exec_cmd()`.
- **Exit (`X` / `EXIT`).** The user's record is written back to
  `DIALDOS.DAT`. Hanging up without `EXIT` keeps the record as it was at
  entry (unchanged from the original).

## Data

### `DIALDOS.DAT` - one record per user (`struct dosrec`, 4100 bytes)

Opened with a maximum record length of 4101. Variable-length Btrieve file,
1024-byte pages. Key 0 is the userid (30 bytes, case-insensitive through
`GALCAPS.ALT`).

| Field | Size | Purpose |
|---|---|---|
| `userid` | 30 | Key |
| `prompt` | 66 | `PROMPT` string (`$p$g` style) |
| `curdir` | 80 | Current drive and directory |
| `history` | 2001 | DOSKEY history, NUL-separated, newest first |
| `doshigh` | 2 | Number of history entries |
| `dirs` | 26 x 66 | Remembered current directory for each drive letter |
| `speed` | 2 | Blocks per step for COPY and GREP (1-200, default 50) |
| `security` | 4 | Allowed-command bits (`CMD_*`, `CANMOVE`) |
| `homedir` | 50 | Home directory (users without `CANMOVE` are confined to it) |
| `password` | 9 | Optional personal password |
| `ext`, `extclr`, `dirclr` | 80+20+1 | `COLORS` settings |

### Settings (`struct configrec`, 512 bytes)

One record in the BBS's general data file (`BBSGEN.DAT`, `genbb`), keyed
by userid `Sysop` and module `DIALDOS`. It holds the sysop key, the master
password and the option flags (`ENPASSWD`, `AUDTXFER`, `BATCHPRT`), and is
edited with the `CONFIG` full-screen form (`CNF10`). The old demo and
activation fields (`installdate`, `lastrun`, `daycount`, `actcode`) are
still in the record, unused, so later fields do not move.

### Record layout and the Borland sizes

Both records use `#pragma pack(1)` to reproduce Borland's byte packing.
The `dosrec` fields are sized with Borland's `<dir.h>` limits (`MAXPATH`
80, `MAXDIR` 66). The SDK defines `MAXPATH` as 771 under Visual Studio, so
`FNSPLIT.H` redefines the Borland values after the SDK headers. A build-time
check fails if either record is not exactly 512 or 4100 bytes.

## Platform layer

| Borland / DOS | Replacement |
|---|---|
| `fnsplit()` | `FNSPLIT.H`, same algorithm and results (unit tested) |
| `getdisk()`, `getcurdir()` | `_getdrive()`, `_getdcwd()` |
| `getfat()` | `GetDiskFreeSpaceA()` (CHKDSK geometry) |
| `dskfre()` (kilobytes, 32-bit) | `dskfrebytes()` using `GetDiskFreeSpaceExA()` |
| `_dos_gettime()` | `GetLocalTime()` |
| `_dos_setfileattr()` | `SetFileAttributesA()` (the FAM bits equal the Windows attribute bits) |
| `_dos_setdate()` / `_dos_settime()` | Not used. As in the Worldgroup NT build, DATE and TIME check the input but never set the clock |

`dspr()` replaces the engine's `spr()` in this module. It keeps the same
rotating-buffer behaviour, but uses eight 1024-byte buffers with bounded
formatting. The engine's version formats into 120 bytes before checking the
length, which long Windows paths can exceed.

## Messages and forms

`DIALDOS.MSG` has one `LEVEL6` block of text, then `LEVEL99` holding the
three full-screen forms: `CNF10` (`CONFIG`), `CNF20` (`COLORS`) and
`CNF30` (`EDITUSER`). For each form, the `???` marker runs in the template,
the field-spec string (`cfsp`, `clrfsp`, `dosfsp`) and the answer format
(`cfmt`, `clrfmt`, `dosfmt`) must match in count and order. Message text
uses positional `printf`-style arguments, as in the original.

## Shared state to be aware of

- `editorbuffer` (32 KB) and the `INEDITOR` flag are module-wide, so only
  one user can use `EDIT` at a time. `editowner` records which channel holds
  the editor. The editor drops its text without calling `save_file()` when
  a caller hangs up, so the hangup routine (`doshup`) releases it.
- The `DEBUG` command toggles a module-wide flag that prints path parsing
  for every DS-DOS user, and clears `INEDITOR`. It needs the sysop key.
- `dosactive[]` marks, per channel, that `init_dos()` has set up the
  session. The hangup routine only closes the VDA's file handles when it
  is set, because the VDA is shared with every other module.

## Path limits

Paths are held in the Borland-sized fields (`MAXPATH` 80, `MAXDIR` 66,
8.3 names). `fnsplit()` cuts each part to size, and a cut path names a
different file, so DS-DOS refuses any path that does not survive the split
whole (`fns_whole()`). Directories too long for a recursive job's buffers
are skipped with "Path not found", and `file_names()` refuses a COPY, MOVE
or REN target that would not fit. Users without `CANMOVE` are kept inside
their home directory by `fns_under()`, which matches whole path elements,
so `C:\USERS\BOB` does not admit `C:\USERS\BOBBY`.
