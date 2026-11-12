/* Build 26.11.12.4 05:15PM */
/****************************************************************************/
/*     File: FNSPLIT.H                                                      */
/*  Purpose: Borland <dir.h> path sizes and a Borland-compatible fnsplit() */
/*           for DS-DOS.  Needs only <string.h>, so the tests in TESTS\    */
/*           can compile it without the BBS SDK.                          */
/*                                                                          */
/* Copyright (C) 1996-2026 DialSoft Corp.                                   */
/****************************************************************************/

#ifndef __FNSPLIT_H
#define __FNSPLIT_H

#include <string.h>

/****************************************************************************/
/* Borland <dir.h> path-size limits                                         */
/*                                                                          */
/* IMPORTANT: these sizes are part of the DIALDOS.DAT record layout.  Each  */
/* user record stores the prompt (MAXDIR), the current directory (MAXPATH)  */
/* and a remembered directory for each of the 26 drives (26 x MAXDIR).  The */
/* SDK defines MAXPATH as 771 under Visual Studio, which would change the   */
/* record size and break every existing DIALDOS.DAT.  COMMON.H includes this */
/* after all the SDK headers, so it is safe to put the Borland values back. */
/****************************************************************************/

#undef  MAXPATH
#undef  MAXDRIVE
#undef  MAXDIR
#undef  MAXFILE
#undef  MAXEXT
#define MAXPATH     80             /* full path, e.g. C:\DIR\FILE.EXT      */
#define MAXDRIVE    3              /* "C:"                                 */
#define MAXDIR      66             /* "\DIR\SUBDIR\"                       */
#define MAXFILE     9              /* 8-character file name                */
#define MAXEXT      5              /* ".EXT"                               */

/* fnsplit() result flags.  The SDK already defines DRIVE, DIRECTORY,      */
/* FILENAME and EXTENSION for Visual Studio builds; this adds the last one. */
#ifndef DRIVE
#define DRIVE       0x01
#define DIRECTORY   0x02
#define FILENAME    0x04
#define EXTENSION   0x08
#endif
#define FNS_WILDCARDS 0x10         /* name or extension holds * or ?       */

/****************************************************************************/
/* fnsplit() - split a path into drive, directory, name and extension.     */
/*                                                                          */
/* Same results as Borland's fnsplit(): the drive keeps its colon ("C:"),  */
/* the directory keeps its leading and trailing backslash ("\DIR\"), the   */
/* extension keeps its dot (".TXT"), and "." / ".." are treated as         */
/* directories.  Each part is cut to its <dir.h> size.  Returns a mask of  */
/* the parts that were present.                                             */
/****************************************************************************/

static void
fns_copy(char *dst,const char *src,unsigned maxlen)
{
     if (dst != NULL) {
          strncpy(dst,src,maxlen);
          dst[maxlen]='\0';
     }
}

static int
fns_dotfound(char *p)              /* is this "." or ".." a directory name?*/
{
     if (*(p-1) == '.') {
          p--;
     }
     switch (*--p) {
     case ':':
          if (*(p-2) != '\0') {
               break;
          }
          /* fall through */
     case '/':
     case '\\':
     case '\0':
          return(1);
     }
     return(0);
}

static int
fnsplit(const char *path,char *drive,char *dir,char *name,char *ext)
{
     char buf[MAXPATH+2];
     char *p;
     int len,seenname=0,ret=0;

     if (drive != NULL) *drive='\0';
     if (dir != NULL) *dir='\0';
     if (name != NULL) *name='\0';
     if (ext != NULL) *ext='\0';

     /* buf[0] is a NUL sentinel so the backwards scan below stops there */
     while (*path == ' ') {
          path++;
     }
     if ((len=(int)strlen(path)) > MAXPATH) {
          len=MAXPATH;
     }
     p=buf;
     *p++='\0';
     strncpy(p,path,len);
     *(p+=len)='\0';

     /* Walk backwards from the end, peeling off ext, name, dir, drive */
     for (;;) {
          switch (*--p) {
          case '.':
               if (!seenname && *(p+1) == '\0') {
                    seenname=fns_dotfound(p);
               }
               if (!seenname && !(ret&EXTENSION)) {
                    ret|=EXTENSION;
                    fns_copy(ext,p,MAXEXT-1);
                    *p='\0';
               }
               continue;
          case ':':
               if (p != &buf[2]) {
                    continue;
               }
               /* fall through */
          case '\0':
               if (seenname) {
                    if (*++p) {
                         ret|=DIRECTORY;
                    }
                    fns_copy(dir,p,MAXDIR-1);
                    *p--='\0';
                    break;
               }
               /* fall through */
          case '/':
          case '\\':
               if (!seenname) {
                    seenname=1;
                    if (*++p) {
                         ret|=FILENAME;
                    }
                    fns_copy(name,p,MAXFILE-1);
                    *p--='\0';
                    if (*p == '\0' || (*p == ':' && p == &buf[2])) {
                         break;
                    }
               }
               continue;
          case '*':
          case '?':
               if (!seenname) {
                    ret|=FNS_WILDCARDS;
               }
               /* fall through */
          default:
               continue;
          }
          break;
     }
     if (*p == ':') {
          if (buf[1]) {
               ret|=DRIVE;
          }
          fns_copy(drive,&buf[1],MAXDRIVE-1);
     }
     return(ret);
}

/****************************************************************************/
/* fns_whole() - did fnsplit() keep every character of the path?           */
/*                                                                          */
/* fnsplit() cuts each part to its 8.3-era size, so a long Windows name    */
/* such as REPORT_2024_OLD.DOC comes back as REPORT_2.DOC - a different    */
/* file that may exist.  DS-DOS refuses a path that does not survive the   */
/* split whole, rather than act on the wrong file.                         */
/****************************************************************************/

static int
fns_whole(const char *path,const char *drive,const char *dir,
          const char *name,const char *ext)
{
     while (*path == ' ') {        /* fnsplit() skips these too            */
          path++;
     }
     return(strlen(drive)+strlen(dir)+strlen(name)+strlen(ext) == strlen(path));
}

/****************************************************************************/
/* fns_under() - is path inside directory home?                            */
/*                                                                          */
/* A plain prefix test is not enough: home C:\USERS\BOB would also admit   */
/* C:\USERS\BOBBY\.  The text after the prefix must start a new path       */
/* element (or home must already end with a backslash).  Case-insensitive. */
/****************************************************************************/

static int
fns_under(const char *home,const char *path)
{
     size_t n=strlen(home);

     if (n == 0 || strlen(path) < n || _strnicmp(home,path,n) != 0) {
          return(0);
     }
     return(home[n-1] == '\\' || path[n] == '\0' || path[n] == '\\');
}

#endif  /* __FNSPLIT_H */
