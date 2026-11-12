/* Build 26.11.12.4 05:15PM */
/****************************************************************************/
/*     File: COMMON.H                                                       */
/*   Author: Richard Iglar                                                  */
/*  Started: July 9, 1997                                                   */
/* Modified: January 11, 2026 - WG 3.2 NT update - R. Hadsall               */
/*           November 12, 2026 - ported to The Major BBS v10                */
/*           by Mark Laudenbach                                             */
/*  Purpose: Platform glue for DS-DOS.                                      */
/*                                                                          */
/* Copyright (C) 1996-2026 DialSoft Corp.                                   */
/****************************************************************************/
/*                                                                          */
/* DS-DOS was written for Borland C, which supplied a handful of DOS-style  */
/* helpers (fnsplit, getdisk, getcurdir, getfat, _dos_gettime) and the      */
/* <dir.h> path-size limits.  Visual Studio has none of these, so this      */
/* header provides small replacements that behave the way the Borland ones  */
/* did.  The rest of the module is written against them unchanged.         */
/*                                                                          */
/****************************************************************************/

#ifndef __COMMON_H
#define __COMMON_H

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <direct.h>                /* _getdrive, _getdcwd, mkdir, rmdir    */
#include <io.h>                    /* unlink                               */

#include "gcomm.h"
#include "majorbbs.h"
#include "dntapi.h"                /* date/time conversion helpers         */
#include "filexfer.h"              /* file transfer (GET / PUT commands)   */
#include "fsdbbs.h"                /* full-screen data entry forms         */

/****************************************************************************/
/* Type names the original code used                                        */
/****************************************************************************/

#define RETVAL      GBOOL          /* return type of the input routine     */
#define FINRET      SHORT          /* return type of editor/form callbacks */

/* File-search block (fnd1st/fndnxt) and accessors for its fields */
#define FNDBLK      ffblk
#define FNAME(a)    a.ff_name
#define FDATE(a)    a.ff_fdate
#define FTIME(a)    a.ff_ftime
#define FATTR(a)    a.ff_attrib
#define FSIZE(a)    a.ff_fsize
/* The engine stores a file's size in a signed 32-bit field, so files of  */
/* 2 GB or more read as negative.  Read it unsigned: right up to 4 GB.    */
#define FSIZEU(a)   ((WULONG)(unsigned long)(a).ff_fsize)

/* 64-bit integers for disk-size arithmetic (drives are far bigger now) */
typedef __int64          WLONG;
typedef unsigned __int64 WULONG;

#include "FNSPLIT.H"               /* Borland path sizes + fnsplit()       */

/****************************************************************************/
/* getdisk() / getcurdir() - the BBS's current drive and directory         */
/****************************************************************************/

static INT
getdisk(VOID)                      /* current drive: 0=A, 1=B, 2=C ...     */
{
     return(_getdrive()-1);
}

static INT                         /* 0 = ok, -1 = error                   */
getcurdir(INT drive,CHAR *directory) /* drive 0=current, 1=A ...           */
{                                  /* result has no drive or leading "\"  */
     CHAR tmp[_MAX_PATH];
     CHAR *p;

     *directory='\0';
     if (_getdcwd(drive,tmp,sizeof(tmp)) == NULL) {
          return(-1);
     }
     p=tmp;
     if (p[0] != '\0' && p[1] == ':') {
          p+=2;
     }
     if (*p == '\\') {
          p++;
     }
     stzcpy(directory,p,MAXDIR);
     return(0);
}

/****************************************************************************/
/* getfat() - disk geometry for the CHKDSK command                          */
/****************************************************************************/

struct fatinfo {
     UINT fi_sclus;                /* sectors per cluster                  */
     UINT fi_fatid;                /* FAT id byte (kept for CHKDSK only)   */
     UINT fi_nclus;                /* total clusters                       */
     UINT fi_bysec;                /* bytes per sector                     */
};

static VOID
getfat(UINT drive,struct fatinfo *f) /* drive 0=current, 1=A ...           */
{
     CHAR root[4]="C:\\";
     DWORD spc,bps,freec,totalc;

     root[0]=(CHAR)(drive == 0 ? 'A'+getdisk() : 'A'+drive-1);
     if (GetDiskFreeSpaceA(root,&spc,&bps,&freec,&totalc)) {
          f->fi_sclus=spc;
          f->fi_bysec=bps;
          f->fi_nclus=totalc;
          f->fi_fatid=0xF8;
     }
     else {
          memset(f,0,sizeof(struct fatinfo));
     }
}

/****************************************************************************/
/* dskfrebytes() - free bytes on a drive, for DIR and CHKDSK                */
/*                                                                          */
/* The SDK's dskfre() returns kilobytes in a 32-bit value, which the old    */
/* code printed as "bytes" and which overflows on drives over 2 TB.         */
/****************************************************************************/

static WULONG
dskfrebytes(CHAR drive)            /* drive letter, e.g. 'C'               */
{
     CHAR root[4]="C:\\";
     ULARGE_INTEGER avail,total,freeb;

     root[0]=drive;
     if (GetDiskFreeSpaceExA(root,&avail,&total,&freeb)) {
          return(avail.QuadPart);
     }
     return(0);
}

/****************************************************************************/
/* _dos_gettime() - current local time, for the TIME command                */
/*                                                                          */
/* The Windows NT builds of DS-DOS never changed the system clock (TIME and */
/* DATE accept a new value but leave the clock alone), so there is no       */
/* setter here.                                                             */
/****************************************************************************/

struct dostime_t {
     UINT hour;
     UINT minute;
     UINT second;
     UINT hsecond;                 /* hundredths of a second               */
};

static VOID
_dos_gettime(struct dostime_t *t)
{
     SYSTEMTIME st;

     GetLocalTime(&st);
     t->hour=st.wHour;
     t->minute=st.wMinute;
     t->second=st.wSecond;
     t->hsecond=st.wMilliseconds/10;
}

/****************************************************************************/
/* dspr() - a roomier spr()                                                 */
/*                                                                          */
/* The engine's spr() formats into a 120-byte buffer and only checks the    */
/* length afterwards, so a long result corrupts memory before the board     */
/* stops.  DS-DOS builds paths from directory and file names, and Windows   */
/* file names can be long, so it uses this instead: same rotating-buffer    */
/* behaviour, but the buffers are large and the output is always cut to     */
/* fit.                                                                     */
/****************************************************************************/

#define DSPRBUFS 8                 /* results that can be live at once     */
#define DSPRSIZ  1024              /* size of each result buffer           */

static CHAR *
dspr(const CHAR *fmt,...)
{
     static CHAR result[DSPRBUFS][DSPRSIZ];
     static INT next=0;
     CHAR *buf;
     va_list ap;

     buf=result[next];
     next=(next+1)%DSPRBUFS;
     va_start(ap,fmt);
     _vsnprintf(buf,DSPRSIZ-1,fmt,ap);
     va_end(ap);
     buf[DSPRSIZ-1]='\0';
     return(buf);
}

/****************************************************************************/
/* uprf() - prf() of text that may itself contain % signs                   */
/****************************************************************************/

static VOID
uprf(const CHAR *text)
{
     prf("%s",text);
}

#endif  /* __COMMON_H */
