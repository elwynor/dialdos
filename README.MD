# DS-DOS Shell for The Major BBS v10

**DS-DOS Shell** (`DIALDOS`) gives the sysop, and any users the sysop sets
up, an MS-DOS style command prompt on the BBS host. You can list, copy,
move, edit and transfer files without leaving the BBS.

DS-DOS was written by Richard Iglar for DialSoft in 1996-1997, for The
Major BBS 6.25 and Worldgroup 1.x to 3.x. Version 3.9.0 converts the
Worldgroup 3 release for The Major BBS v10. The commands, the screens and
the data file format are unchanged.

## Features

- **Familiar MS-DOS commands:** `DIR`, `CD`, `MD`, `RD`, `COPY`, `XCOPY`,
  `MOVE`, `DEL`, `REN`, `TYPE`/`MORE`, `ATTRIB`, `CHKDSK`, `DELTREE`,
  `EDIT`, `ECHO`, `PROMPT`, `SET`, `VOL`, `TIME`, `DATE` and more. Type
  `HELP` or `<command> /?` online.
- **DOSKEY-style history:** cursor up and down recalls earlier commands, and
  `HISTORY` lists them.
- **Batch files:** run `.BAT` files online, with optional protection so only
  files marked for DS-DOS will run.
- **File transfer:** `GET`/`DOWNLOAD` and `PUT`/`UPLOAD` use the BBS's
  normal transfer protocols.
- **Online editor:** `EDIT` opens text files of up to 400 lines in the
  BBS's full-screen editor.
- **GREP**, `| MORE` and `| GREP` pipes, and `DIR /S /P /W /B /L /A`.
- **Per-user accounts:** give users a home directory and choose which
  commands each user may use (`EDITUSER`, `DELUSER`, `USERS`).
- **Colors:** each user can color directories and up to 20 file extensions
  (`COLORS`).
- **Unix aliases:** `ls`, `cp`, `xcp`, `mv`, `rm`, `rmdir`, `mkdir`, `vi`,
  `cat`.

## What changed for The Major BBS v10

- Built for The Major BBS v10.
- **No activation code is needed.** The 14-day demo, the activation check
  and the activation code field on the `CONFIG` screen are removed.
- The install-day e-mail to DialSoft, which sent your BBS's name, address
  and phone numbers, is removed.
- A few long-standing faults are fixed. `ATTRIB` changes attributes again,
  `TIME` shows the real time, and DIR and CHKDSK free-space figures are
  correct. Long Windows file names can no longer overrun internal buffers.
  See [CHANGELOG.MD](CHANGELOG.MD).
- The file names (`DIALDOS.*`) and the `DIALDOS.DAT` record layout are the
  same as the Worldgroup version.

## Installation

1. Bring the BBS down.
2. Unzip the release into your BBS directory. `DIALDOS.DLL`, `DIALDOS.MDF`,
   `DIALDOS.MSG` and `DIALDOS.RLN` go in the BBS directory, and
   `DIALDOS.VIR` goes in its `GCVIRDAT` folder.
3. Start the BBS. The first time it starts, DS-DOS creates `DIALDOS.DAT`
   from `GCVIRDAT\DIALDOS.VIR`. A `DIALDOS.VIR` in the BBS directory itself
   is used if it isn't in `GCVIRDAT`. An existing `DIALDOS.DAT` is never
   overwritten.
4. Add **DS-DOS Shell** to your menu with the Menu Tree editor. Protect the
   menu entry with a key only trusted users have. DS-DOS can reach every
   file the BBS can.

## Configuration

DS-DOS has no CNF options. A user with master access types `CONFIG` inside
DS-DOS. Changes apply at once.

| Setting | New install | Meaning |
|---|---|---|
| Key required for "sysop" access | `SYSOP` | Key needed for `EDITUSER`, `DELUSER` and `USERS`. A user with no DS-DOS account also needs it to get in, plus master access or the master password. |
| Enable master password | NO | Ask key holders who have no DS-DOS account for the master password |
| Master password | `SYSOP` | The password asked for above |
| Record uploads/downloads to audit trail | YES | Log every `GET` and `PUT` in the Audit Trail |
| Enable batch file protection | YES | Only run `.BAT` files whose first line contains `DIALDOS` |

Change the default master password if you enable it.

## Usage

Enter DS-DOS from its menu entry and type `HELP`. Type `EXIT` or `X` to
leave; your current directory and command history are saved. To give
another user access, type `EDITUSER <userid>` (or `EDITUSER NEW`). Then set
their password, home directory and allowed commands. `DIALDOS.RLN` has the
full command list and revision history.

## Version Information

Current version: **3.9.0**. See [CHANGELOG.MD](CHANGELOG.MD).

## License

Licensed under the GNU Affero General Public License v3.0. See
[LICENSE](LICENSE).

Copyright (C) 1996-2026 DialSoft Corp.

## Support

Report problems through the GitHub issues of this repository.

---

DS-DOS Shell converted for The Major BBS v10 by Mark Laudenbach - https://github.com/laudenbachm for Elwynor Technologies.
