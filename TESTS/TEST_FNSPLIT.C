/* Build 26.11.12.4 05:15PM */
/****************************************************************************/
/*     File: TEST_FNSPLIT.C                                                 */
/*  Purpose: Checks that FNSPLIT.H's fnsplit() splits paths the way the     */
/*           Borland run-time library did, since every DS-DOS path command  */
/*           (CD, DIR, COPY, DEL, ...) depends on it.  Also checks the     */
/*           Borland path sizes the DIALDOS.DAT record layout relies on.   */
/*                                                                          */
/* Copyright (C) 1996-2026 DialSoft Corp.                                   */
/****************************************************************************/
/* Build and run:  TESTS\RUN_TESTS.CMD   (exit code 0 = all passed)         */

#include <stdio.h>
#include "..\SRC\DIALDOS\FNSPLIT.H"

static int failures = 0;

static void
check(const char *path, int flags,
      const char *drive, const char *dir, const char *name, const char *ext)
{
     char d[MAXDRIVE], p[MAXDIR], n[MAXFILE], e[MAXEXT];
     char buf[MAXPATH + 1];
     int got;

     strcpy(buf, path);
     got = fnsplit(buf, d, p, n, e);
     if (got != flags || strcmp(d, drive) || strcmp(p, dir)
      || strcmp(n, name) || strcmp(e, ext)) {
          failures++;
          printf("FAIL %-22s got  %02X [%s][%s][%s][%s]\n", path, got, d, p, n, e);
          printf("     %-22s want %02X [%s][%s][%s][%s]\n", "", flags, drive, dir, name, ext);
     }
}

static void
whole(const char *path, int want)
{
     char d[MAXDRIVE], p[MAXDIR], n[MAXFILE], e[MAXEXT];
     int got;

     fnsplit(path, d, p, n, e);
     got = fns_whole(path, d, p, n, e);
     if (got != want) {
          failures++;
          printf("FAIL fns_whole(%s) got %d want %d\n", path, got, want);
     }
}

static void
under(const char *home, const char *path, int want)
{
     int got = fns_under(home, path);

     if (got != want) {
          failures++;
          printf("FAIL fns_under(%s, %s) got %d want %d\n", home, path, got, want);
     }
}

int
main(void)
{
     /* The record layout in DIALDOSH.H was built on these sizes. */
     if (MAXPATH != 80 || MAXDRIVE != 3 || MAXDIR != 66 || MAXFILE != 9 || MAXEXT != 5) {
          failures++;
          printf("FAIL Borland path sizes changed\n");
     }

     check("C:\\FOO\\BAR.TXT",  DRIVE|DIRECTORY|FILENAME|EXTENSION, "C:", "\\FOO\\", "BAR", ".TXT");
     check("BAR.TXT",           FILENAME|EXTENSION,                 "",   "",        "BAR", ".TXT");
     check("BAR",               FILENAME,                           "",   "",        "BAR", "");
     check("C:",                DRIVE,                              "C:", "",        "",    "");
     check("C:\\",              DRIVE|DIRECTORY,                    "C:", "\\",      "",    "");
     check("C:\\DIR\\",         DRIVE|DIRECTORY,                    "C:", "\\DIR\\", "",    "");
     check("C:FOO",             DRIVE|FILENAME,                     "C:", "",        "FOO", "");
     check("\\WGSERV",          DIRECTORY|FILENAME,                 "",   "\\",      "WGSERV", "");
     check("SUB\\FILE.DAT",     DIRECTORY|FILENAME|EXTENSION,       "",   "SUB\\",   "FILE", ".DAT");

     /* "." and ".." are directories, never a name plus an extension */
     check(".",                 DIRECTORY,                          "",   ".",       "",    "");
     check("..",                DIRECTORY,                          "",   "..",      "",    "");
     check("C:..",              DRIVE|DIRECTORY,                    "C:", "..",      "",    "");
     check("\\WGSERV\\..",      DIRECTORY,                          "",   "\\WGSERV\\..", "", "");
     check("..\\DOC.TXT",       DIRECTORY|FILENAME|EXTENSION,       "",   "..\\",    "DOC", ".TXT");

     /* wildcards */
     check("*.*",               FILENAME|EXTENSION|FNS_WILDCARDS,   "",   "",        "*",   ".*");
     check("C:\\DIR\\A?C.T*",   DRIVE|DIRECTORY|FILENAME|EXTENSION|FNS_WILDCARDS,
                                                                    "C:", "\\DIR\\", "A?C", ".T*");

     /* only the last dot starts the extension */
     check("A.B.C",             FILENAME|EXTENSION,                 "",   "",        "A.B", ".C");

     /* parts are cut to the 8.3 sizes */
     check("LONGFILENAME.HTML", FILENAME|EXTENSION,                 "",   "",        "LONGFILE", ".HTM");

     /* leading spaces are ignored */
     check("  C:\\X.Y",         DRIVE|DIRECTORY|FILENAME|EXTENSION, "C:", "\\",      "X",   ".Y");

     /* fns_whole(): a path that fnsplit() had to cut must be refused, or */
     /* DEL REPORT_2024_OLD.DOC would delete REPORT_2.DOC.                */
     whole("C:\\FOO\\BAR.TXT",     1);
     whole("  C:\\X.Y",            1);
     whole("*.*",                  1);
     whole("C:..",                 1);
     whole("BAR",                  1);
     whole("REPORT_2024_OLD.DOC",  0);
     whole("NOTES.HTML",           0);
     whole("C:\\DIR\\LONGDIRECTORYNAME",  0);
     /* directory of 66 characters (one over MAXDIR-1) and of exactly 65 */
     whole("C:\\A_DIRECTORY_NAME_LONG_ENOUGH\\TO_RUN_PAST_SIXTY_FIVE_CHARACTERS_X\\X.TXT", 0);
     whole("C:\\A_DIRECTORY_NAME_JUST_SHORT\\ENOUGH_TO_STAY_UNDER_THE_LIMIT_OK12\\X.TXT",  1);

     /* fns_under(): the home-directory fence must not be a bare prefix */
     under("C:\\USERS\\BOB",   "C:\\USERS\\BOB\\FILE.TXT",   1);
     under("C:\\USERS\\BOB",   "C:\\USERS\\BOB",             1);
     under("C:\\USERS\\BOB",   "c:\\users\\bob\\x",          1);
     under("C:\\USERS\\BOB",   "C:\\USERS\\BOBBY\\SECRET",   0);
     under("C:\\USERS\\BOB",   "C:\\USERS\\BOB2\\*.*",       0);
     under("C:\\USERS\\BOB\\", "C:\\USERS\\BOB\\FILE.TXT",   1);
     under("C:\\USERS\\BOB\\", "C:\\USERS\\BOBBY\\SECRET",   0);
     under("C:\\USERS\\BOB\\", "C:\\USERS\\BOB",             0);
     under("C:\\",             "C:\\ANYTHING\\X",            1);
     under("C:\\",             "D:\\X",                      0);
     under("",                 "C:\\X",                      0);

     if (failures) {
          printf("%d test(s) FAILED\n", failures);
          return 1;
     }
     printf("fnsplit, fns_whole, fns_under: all tests passed\n");
     return 0;
}
