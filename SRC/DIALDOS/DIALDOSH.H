/* Build 26.11.12.4 05:15PM */
/****************************************************************************/
/*     File: DIALDOSH.H                                                     */
/*   Author: Richard Iglar                                                  */
/*  Started: February 1, 1996                                               */
/* Modified: January 11, 2026 - WG 3.2 NT update - R. Hadsall               */
/*           November 12, 2026 - ported to The Major BBS v10                */
/*           by Mark Laudenbach                                             */
/*  Purpose: Header file for DialSoft DOS Shell                             */
/*                                                                          */
/* Copyright (C) 1996-2026 DialSoft Corp.                                   */
/****************************************************************************/

#include "COMMON.H"
#include "DIALDOS.H"               /* message numbers (from DIALDOS.MSG)   */

/* Release version.  Keep DIALDOS.rc (FILEVERSION / PRODUCTVERSION) in     */
/* step; TOOLS/MKDIST.PY refuses to package a DLL whose version differs.   */
#define DIALDOS_VERSION "3.9.0"
#define DIALDOS_BUILD   "26.11.12.4"   /* shown in the Audit Trail at startup */

/*
  Settings record (512 bytes).  It is NOT in DIALDOS.DAT: it lives in the
  BBS's general data file (genbb / BBSGEN.DAT), whose records are keyed by
  userid + module name - here "Sysop" + "DIALDOS".

  The layout is unchanged from the WorldGroup releases so existing data
  files keep working.  installdate, lastrun, daycount and actcode belonged
  to the old demo/activation scheme; nothing uses them any more, but they
  stay in place so the fields after them do not move.

  #pragma pack(1) reproduces the Borland compiler's byte packing; without
  it Visual Studio would insert padding and shift every later field.
*/
#pragma pack(push, 1)
struct configrec {
   CHAR   userid[UIDSIZ]; //  30 key: always "Sysop"
   CHAR   modnam[MNMSIZ]; //  25 module name
   USHORT installdate;    //   2 (unused - old demo scheme)
   USHORT lastrun;        //   2 (unused - old demo scheme)
   SHORT  daycount;       //   2 (unused - old demo scheme)
   CHAR   actcode[8];     //   8 (unused - old activation code)
   SHORT  flags;          //   2 flags for DS-DOS
   CHAR   syskey[16];     //  16 sysop/webmaster key
   CHAR   mspasswd[9];    //   9 master password
                        // 96
   CHAR   spare[512-96];
} config;
#pragma pack(pop)

/*
  Per-user record (4100 bytes), stored in DIALDOS.DAT keyed by userid.
  Same layout as the WorldGroup releases (see the note on configrec).
*/
#pragma pack(push, 1)
struct dosrec
{
  CHAR  userid[30];       // 30
  CHAR  prompt[MAXDIR];   // 66
  CHAR  curdir[MAXPATH];  // 80
  CHAR  history[2001];    // 2001
  SHORT doshigh;          // 2
  CHAR  dirs[26*MAXDIR];  // 1716
  SHORT speed;            // 2
  LONG  security;         // 4
  CHAR  homedir[50];      // 50
  CHAR  password[9];      // 9
  CHAR  ext[80];        //  80 extensions
  CHAR  extclr[20];     //  20 colors for extensions
  CHAR  dirclr;         //   1 color for directories

  CHAR  spare[4100-4061]; // -- 4061
};
#pragma pack(pop)

/* Compile-time guards: the build fails if either record changes size. */
typedef CHAR configrec_must_be_512_bytes[(sizeof(struct configrec) == 512) ? 1 : -1];
typedef CHAR dosrec_must_be_4100_bytes[(sizeof(struct dosrec) == 4100) ? 1 : -1];

/* Longest record DIALDOS.DAT can hold (the file was opened with this). */
#define DOSRECLEN (sizeof(struct dosrec)+1)

#define dosbuf  ((struct dosrec *)dosbt->data)

/*
  VDA Struct
*/
struct vdados
{
  struct dosrec data;

  CHAR spath[MAXPATH];
  CHAR sdrive[MAXDRIVE];
  CHAR sdir[MAXDIR];
  CHAR sfile[MAXFILE];
  CHAR sext[MAXEXT];
  FILE *srcfile;
  CHAR srcdir[MAXDIR];
  FILE *batfile;

  CHAR dpath[MAXPATH];
  CHAR ddrive[MAXDRIVE];
  CHAR ddir[MAXDIR];
  CHAR dfile[MAXFILE];
  CHAR dext[MAXEXT];
  FILE *destfile;
  CHAR destdir[MAXDIR];

  struct FNDBLK file;
  struct FNDBLK tempfile;
  CHAR attr;
  CHAR location;
  INT  lastinp;
  INT  doskey;
  INT  line;
  INT  addflags;
  INT  remflags;
  INT  curop;
  LONG flags;
  INT  dnum;
  INT  fnum;
  INT  hfnum;
  WLONG space;                  /* byte totals (64-bit: drives pass 2 GB);  */
  WLONG hspace;                 /* also a counter / record position         */
  CHAR greptext[128];
  CHAR topic[14];
  CHAR text[10001];
  CHAR overflow[256];
};

#define dosptr  (((struct vdados *)vdaptr)->data)
#define dosvda  ((struct vdados *)vdaptr)

#define dosfile     dosvda->file
#define dostempfile dosvda->tempfile

#define ALLATTR FAMDIR+FAMARC+FAMRON+FAMHID+FAMSYS

/* Flags for DialDOS users */
#define ECHO      0x00000001
#define NOTBUSY   0x00000002
#define PAGEMODE  0x00000004
#define WIDEMODE  0x00000008
#define LOWERCASE 0x00000010
#define BRIEFMODE 0x00000020
#define PAUSED    0x00000040
#define MOREMODE  0x00000080
#define OVERALL   0x00000100
#define OVERFILE  0x00000200
#define MOVEMODE  0x00000400
#define BATCHFILE 0x00000800
#define BATCHECHO 0x00001000
#define SENDNUL   0x00002000
#define ABORTOP   0x00004000
#define RECURSE   0x00008000L
#define NOTFOUND  0x00010000L
#define XCOPY     0x00020000L
#define NOWILD    0x00040000L
#define ACCESSDND 0x00080000L
#define GREPMODE  0x00100000L
#define LINENUMS  0x00200000L
#define INCYCLE   0x00400000L
/*#define 0x00800000L
#define 0x01000000L
#define 0x02000000L
#define 0x04000000L
#define 0x08000000L
*/

/* Flags for S_DIR and D_DIR users */
#define WILDCARD  0x0001
#define MAKEDIR   0x0002
/*
#define 0x0004
#define 0x0008
#define 0x0010
#define 0x0020
#define 0x0040
#define 0x0080
#define 0x0100
#define 0x0200
#define 0x0400
#define 0x0800
#define 0x1000
#define 0x2000
#define 0x4000
#define 0x8000
*/

/* Flags for DialDos (module-wide, shared by every user) */
#define DEBUGMODE  0x02            /* DEBUG command: show path parsing     */
#define INEDITOR   0x08            /* the one shared editor buffer is busy */


/* Flags for DialDos */
#define BATCHPRT   0x0001
#define AUDTXFER   0x0002
#define ENPASSWD   0x0004
/*
#define 0x0008
#define 0x0010
#define 0x0020
#define 0x0040
#define 0x0080
#define 0x0100
#define 0x0200
#define 0x0400
#define 0x0800
#define 0x1000
#define 0x2000
#define 0x4000
#define 0x8000
*/



/* Security Flags for DialDOS users */
#define CANMOVE    0x00000001
#define CMD_DATE   0x00000002
#define CMD_TIME   0x00000004
#define CMD_ATTRIB 0x00000008
#define CMD_COPY   0x00000010
#define CMD_XCOPY  0x00000020
#define CMD_MOVE   0x00000040
#define CMD_RD     0x00000080
#define CMD_MD     0x00000100
#define CMD_GET     0x00000200
#define CMD_PUT     0x00000400
#define CMD_REN     0x00000800
#define CMD_DEL     0x00001000
#define CMD_EDIT    0x00002000
#define CMD_TYPE    0x00004000
#define CMD_DELTREE 0x00008000L
#define CMD_SPEED   0x00010000L
#define CMD_GREP    0x00020000L
/*#define 0x00040000L
#define 0x00080000L
#define 0x00100000L
#define 0x00200000L
#define 0x00400000L
#define 0x00800000L
#define 0x01000000L
#define 0x02000000L
#define 0x04000000L
#define 0x08000000L
*/


#define USERIDFLD   0
#define PASSWORDFLD 1
#define HOMEDIRFLD  2
#define CANMOVEFLD  3
#define TYPEFLD     4
#define EDITFLD     5
#define DATEFLD     6
#define TIMEFLD     7
#define ATTRIBFLD   8
#define RENFLD      9
#define COPYFLD     10
#define XCOPYFLD    11
#define MOVEFLD     12
#define MDFLD       13
#define RDFLD       14
#define GETFLD      15
#define PUTFLD      16
#define DELFLD      17
#define DELTREEFLD  18
#define SPEEDFLD    19
#define GREPFLD     20

static CHAR dosfmt[]=
  "USERID=%s%c"
  "PASSWORD=%s%c"
  "HOMEDIR=%s%c"
  "CANMOVE=%s%c"
  "TYPE=%s%c"
  "EDIT=%s%c"
  "DATE=%s%c"
  "TIME=%s%c"
  "ATTRIB=%s%c"
  "REN=%s%c"
  "COPY=%s%c"
  "XCOPY=%s%c"
  "MOVE=%s%c"
  "MD=%s%c"
  "RD=%s%c"
  "GET=%s%c"
  "PUT=%s%c"
  "DEL=%s%c"
  "DELTREE=%s%c"
  "SPEED=%s%c"
  "GREP=%s%c"
  ;


static CHAR dosfsp[]=
  "USERID(MIN=1) "
  "PASSWORD(SECRET) "
  "HOMEDIR(MIN=3) "
  "CANMOVE(ALT=NO ALT=YES MULTICHOICE) "
  "TYPE(ALT=NO ALT=YES MULTICHOICE) "
  "EDIT(ALT=NO ALT=YES MULTICHOICE) "
  "DATE(ALT=NO ALT=YES MULTICHOICE) "
  "TIME(ALT=NO ALT=YES MULTICHOICE) "
  "ATTRIB(ALT=NO ALT=YES MULTICHOICE) "
  "REN(ALT=NO ALT=YES MULTICHOICE) "
  "COPY(ALT=NO ALT=YES MULTICHOICE) "
  "XCOPY(ALT=NO ALT=YES MULTICHOICE) "
  "MOVE(ALT=NO ALT=YES MULTICHOICE) "
  "MD(ALT=NO ALT=YES MULTICHOICE) "
  "RD(ALT=NO ALT=YES MULTICHOICE) "
  "GET(ALT=NO ALT=YES MULTICHOICE) "
  "PUT(ALT=NO ALT=YES MULTICHOICE) "
  "DEL(ALT=NO ALT=YES MULTICHOICE) "
  "DELTREE(ALT=NO ALT=YES MULTICHOICE) "
  "SPEED(ALT=NO ALT=YES MULTICHOICE) "
  "GREP(ALT=NO ALT=YES MULTICHOICE) "
  "DONE(ALT=SAVE ALT=QUIT ALT=EDIT MULTICHOICE)";


/*
  CONFIG form (CNF10).  The field numbers, the cfmt answer order, the cfsp
  field specs and the ??? marker runs in the CNF10 template must all line
  up.  The activation-code field that used to be field 0 has been removed
  from all four.
*/
#define CSYSKEY    0
#define CENPASSWD  1
#define CMSPASSWD  2
#define CAUDTXFER  3
#define CBATCHPRT  4
#define CEXITFLD   5


static CHAR cfmt[]=
  "CSYSKEY=%s%c"
  "CENPASSWD=%s%c"
  "CMSPASSWD=%s%c"
  "CAUDTXFER=%s%c"
  "CBATCHPRT=%s%c";

static CHAR cfsp[]=
  "CSYSKEY(MIN=1) "
  "CENPASSWD(ALT=NO ALT=YES MULTICHOICE) "
  "CMSPASSWD "
  "CAUDTXFER(ALT=NO ALT=YES MULTICHOICE) "
  "CBATCHPRT(ALT=NO ALT=YES MULTICHOICE) "
  "DONE(ALT=SAVE ALT=QUIT ALT=EDIT MULTICHOICE)";



static CHAR *colors[]={"\033[0;37m","\033[0;34m","\033[0;32m","\033[0;36m",
                        "\033[0;31m","\033[0;35m","\033[0;33m","\033[0;37m",
                        "\033[1;30m","\033[1;34m","\033[1;32m","\033[1;36m",
                        "\033[1;31m","\033[1;35m","\033[1;33m","\033[1;37m"};

static CHAR clrfmt[]=
  "D1=%s%c"
  "E1=%s%c"
  "C1=%s%c"
  "E2=%s%c"
  "C2=%s%c"
  "E3=%s%c"
  "C3=%s%c"
  "E4=%s%c"
  "C4=%s%c"
  "E5=%s%c"
  "C5=%s%c"
  "E6=%s%c"
  "C6=%s%c"
  "E7=%s%c"
  "C7=%s%c"
  "E8=%s%c"
  "C8=%s%c"
  "E9=%s%c"
  "C9=%s%c"
  "E10=%s%c"
  "C10=%s%c"
  "E11=%s%c"
  "C11=%s%c"
  "E12=%s%c"
  "C12=%s%c"
  "E13=%s%c"
  "C13=%s%c"
  "E14=%s%c"
  "C14=%s%c"
  "E15=%s%c"
  "C15=%s%c"
  "E16=%s%c"
  "C16=%s%c"
  "E17=%s%c"
  "C17=%s%c"
  "E18=%s%c"
  "C18=%s%c"
  "E19=%s%c"
  "C19=%s%c"
  "E20=%s%c"
  "C20=%s%c";

static CHAR clrfsp[]=
  "D1(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E1 "
  "C1(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E2 "
  "C2(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E3 "
  "C3(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E4 "
  "C4(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E5 "
  "C5(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E6 "
  "C6(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E7 "
  "C7(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E8 "
  "C8(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E9 "
  "C9(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E10 "
  "C10(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E11 "
  "C11(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E12 "
  "C12(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E13 "
  "C13(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E14 "
  "C14(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E15 "
  "C15(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E16 "
  "C16(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E17 "
  "C17(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E18 "
  "C18(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E19 "
  "C19(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "E20 "
  "C20(ALT=BLUE ALT=GREEN ALT=CYAN ALT=RED ALT=MAGENTA ALT=BROWN ALT=GRAY ALT=MED.GRAY ALT=LT.BLUE ALT=LT.GREEN ALT=LT.CYAN ALT=LT.RED ALT=PINK ALT=YELLOW ALT=WHITE MULTICHOICE) "
  "DONE(ALT=SAVE ALT=EDIT ALT=QUIT MULTICHOICE)";
