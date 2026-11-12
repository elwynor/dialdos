/* Build 26.11.12.4 05:15PM */
/****************************************************************************/
/*     File: DIALDOS.C                                                      */
/*   Author: Richard Iglar                                                  */
/*  Started: February 1, 1996                                               */
/* Modified: January 11, 2026 - WG 3.2 NT update - R. Hadsall               */
/*           November 12, 2026 - ported to The Major BBS v10                */
/*           by Mark Laudenbach                                             */
/*  Purpose: Code file for DialSoft DOS Shell                               */
/*                                                                          */
/* Copyright (C) 1996-2026 DialSoft Corp.                                   */
/****************************************************************************/
/*                                                                          */
/* DS-DOS gives the sysop (and any users the sysop sets up) an MS-DOS style */
/* command prompt on the BBS host: DIR, CD, COPY, XCOPY, MOVE, DEL, TYPE,   */
/* EDIT, GREP, file upload and download, batch files and DOSKEY-style      */
/* command history.                                                         */
/*                                                                          */
/* How it runs: the BBS calls uinput() each time the user presses Enter.    */
/* Long jobs (a DIR /S, a big COPY) are never done in one go, because every */
/* user shares one thread and the whole board would freeze.  Instead the    */
/* job is split into small steps: begin_cycle() injects a CYCLE event, the  */
/* BBS calls usrsts(), which does a few steps and injects another CYCLE,    */
/* until end_cycle() finishes the job and shows the prompt again.           */
/*                                                                          */
/* Data: DIALDOS.DAT holds one record per user (struct dosrec).  The        */
/* settings (struct configrec) are one record in the BBS's general data     */
/* file, BBSGEN.DAT.  See DIALDOSH.H.                                       */
/*                                                                          */
/****************************************************************************/

#include "DIALDOSH.H"

/****************************************************************************/
/* Variable Declarations                                                    */
/****************************************************************************/

CHAR verinfo[]="DS-DOS Shell " DIALDOS_VERSION;

/*
  Returns the file-name part of a full path, without its extension.
  Used to tell whether a path names a file before asking rsvnam() whether
  that name is a reserved device (CON, PRN, NUL, ...).
*/
static CHAR *fnroot(CHAR *fulpth)
{
   static CHAR tmpbuf[128];
   return(fileparts(GCPART_FILE,fulpth,tmpbuf,sizeof(tmpbuf)));
}

// System Functions
VOID   EXPORT init__dialdos(VOID);
RETVAL uinput(VOID);
VOID   udeltd(CHAR *uid);
VOID   doshup(VOID);
VOID   shtdwn(VOID);

static CHAR chrprc(INT chan, INT c);
static INT init_dos(VOID);
static VOID exec_cmd(VOID);
static VOID shw_prompt(CHAR LF);
static VOID time_now(VOID);
static INT time_ok(CHAR *timestr);
static INT date_ok(CHAR *datestr);
static INT dir_begin(CHAR *pattern,CHAR *swiches);
static VOID dir_show(VOID);
static VOID usrsts(VOID);
static CHAR *insrt_comma(WULONG number);
static VOID chkdsk_ftr(VOID);

static VOID mdir(CHAR *path);
static INT s_dir(CHAR *path,CHAR flags);
static INT d_dir(CHAR *path,CHAR flags);

static VOID end_cycle(VOID);
static VOID begin_cycle(INT newstt);
static INT filednl(INT status_code);
static INT fileupl(INT fupcod);
static INT edit_file(CHAR *filename);
static FINRET save_file(FINRET quit);
static INT set_label(CHAR *newlabel);
static INT copy_ok(CHAR *file1,CHAR *file2,CHAR *swiches);
static GBOOL file_names(VOID);
static VOID batch_poll(VOID);
static INT recurse_dir(VOID);
static VOID new_speed(INT i);
static VOID edit_user(CHAR *userid);
static INT dosvfy(INT fldno,CHAR *answer);
static VOID finish(FINRET save);
static VOID cfinish(FINRET save);
static INT cvfy(INT fldno,CHAR *answer);
static VOID clrfinish(FINRET save);
static VOID clroptions(VOID);
static VOID checkgrep(VOID);
static GBOOL dirlist_add(const CHAR *entry,size_t maxlen);
static INT inhome(const CHAR *path);

static
CHAR DialDos;                 // module-wide flags (DEBUGMODE, INEDITOR)

static
INT dosstt;                   // our module state number from register_module


CHAR *editorbuffer;           // the one 32K editor buffer (shared, see INEDITOR)

/* The channel using the editor while INEDITOR is set (-1 = none).  The     */
/* editor does not call save_file() when a caller drops, so the hangup      */
/* routine uses this to release the editor.                                 */
static INT editowner=-1;

/* Per channel: 1 once init_dos() has set up this user's session (the file */
/* handles in the VDA are then valid), 0 otherwise.  Cleared on every entry */
/* to the module, on EXIT and on hangup.                                    */
static CHAR *dosactive;

HMCVFILE dosmb;               // DIALDOS.MCV message block
DFAFILE  *dosbt;              // DIALDOS.DAT (per-user records)

/* An empty search key for "start from the first record" queries.  It must */
/* be in writable memory: the database layer ignores a key held in a       */
/* read-only string literal.                                               */
static CHAR emptykey[UIDSIZ];

/****************************************************************************/
/* Module Interface Block                                                   */
/****************************************************************************/

struct module block={         // module interface block
     "",                      // name used to refer to this module
     NULL,                    // user logon supplemental routine
     uinput,                  // input routine if selected
     usrsts,                  // status-input routine if selected
     NULL,                    // "injoth" routine for this module
     NULL,                    // user logoff supplemental routine
     doshup,                  // hangup (lost carrier) routine
     NULL,                    // midnight cleanup routine
     udeltd,                  // delete-account routine
     shtdwn                   // finish-up (sys shutdown) routine
};

/****************************************************************************/
/* Initialize Module                                                        */
/****************************************************************************/

VOID EXPORT init__dialdos(VOID)
{

  stzcpy(block.descrp,gmdnam("DIALDOS.MDF"),MNMSIZ);
  dosstt=register_module(&block);
  dosmb=opnmsg("DIALDOS.MCV");

  /* First run: create DIALDOS.DAT from the empty DIALDOS.VIR.  (The DOS   */
  /* releases had an install batch file do this.)  An existing data file   */
  /* is never overwritten.  dfaVirgin() looks only in the BBS's GCVIRDAT\  */
  /* folder, so a DIALDOS.VIR left in the BBS folder itself is tried too.  */
  if (GetFileAttributesA("DIALDOS.DAT") == INVALID_FILE_ATTRIBUTES) {
     if (!dfaVirgin("DIALDOS","DIALDOS")
      && !CopyFileA("DIALDOS.VIR","DIALDOS.DAT",TRUE)) {
        shocst("DS-DOS: NO DIALDOS.DAT","%s","Copy DIALDOS.VIR into GCVIRDAT\\ and restart");
     }
  }
  dosbt=dfaOpen("DIALDOS.DAT",DOSRECLEN,NULL);

  dclvda(sizeof(struct vdados));
  dclvda(fsdroom(CNF10,cfsp,0));
  dclvda(fsdroom(CNF20,clrfsp,0));
  dclvda(fsdroom(CNF30,dosfsp,0));
  editorbuffer=alczer(32767);
  dosactive=(CHAR *)alczer(nterms);

  /* Load the settings record.  It lives in the BBS's general data file    */
  /* (genbb, BBSGEN.DAT), keyed by userid "Sysop" + module "DIALDOS".  On  */
  /* a new install it is created with the defaults below; the sysop then  */
  /* changes them online with the CONFIG command.                          */
  usrnum=-1;
  dfaSetBlk(genbb);
  setmem((CHAR *)&config,sizeof(struct configrec),0);
  stzcpy(config.userid,"Sysop",UIDSIZ);
  stzcpy(config.modnam,"DIALDOS",MNMSIZ);
  DialDos=0;

  if (!dfaAcqEQ(&config,&config,0)) {
     config.installdate=config.lastrun=today();
     config.daycount=1;
     stzcpy(config.syskey,"SYSOP",16);
     stzcpy(config.mspasswd,"SYSOP",9);
     config.flags=AUDTXFER+BATCHPRT;
     dfaInsertV(&config,sizeof(struct configrec));
  }
  dfaRstBlk();
  shocst("DS-DOS LOADED","%s build %s (The Major BBS v10)",verinfo,DIALDOS_BUILD);
}

/****************************************************************************/
/* Run Time rtkick routines                                                 */
/****************************************************************************/

/****************************************************************************/
/* User Logon Supplemental Routine                                          */
/****************************************************************************/

#define GETDIR(n)  &dosptr.dirs[((n)-'A')*MAXDIR]

/****************************************************************************/
/* Input Routine                                                            */
/****************************************************************************/

RETVAL uinput(VOID)
{
  INT i;
  setmbk(dosmb);

  if (*margv[0] == 3) { /* CTRL-C */
     prf("\n");
     if (dosvda->flags&BATCHFILE) {
         fclose(dosvda->batfile);
         dosvda->flags&=~BATCHFILE;
         stop_polling(usrnum);
     }

     if (usrptr->substt > 0 && usrptr->substt <= 99) end_cycle();
     else shw_prompt(1);
  }
  else {
     switch (usrptr->substt) {
        case -1: //password
                if (!margc) prfmsg(ENTRPASS);
                else if (sameas(margv[0],config.mspasswd) || sameas(margv[0],dosptr.password)) {
                    prf("\n");
                    return(init_dos());
                }
                else {
                    echon();
                    prf("\n");
                    prfmsg(ACCSDEND);
                    outprf(usrnum);
                    return(0);
                }
                break;

        case 0: //user just got in
                dosactive[usrnum]=0;
                dfaSetBlk(dosbt);
                /* User in database */
                if (dfaAcqEQ(&dosptr,usaptr->userid,0)) {
                    if (*dosptr.password) {
                        echsec('*',15);
                        prfmsg(ENTRPASS);
                        usrptr->substt=-1;
                    }
                    else return(init_dos());
                    break;
                }
                /* No record, so no record password.  The lookup failed  */
                /* and left dosptr holding whatever another module wrote */
                /* to the VDA; clear the password so the prompt below    */
                /* accepts only the master password.                      */
                *dosptr.password='\0';
                /* Not a sysop */
                if (haskey(config.syskey) != 1) {
                    prfmsg(NOACCESS);
                    outprf(usrnum);
                    return(0);
                }
                /* Password enabled */
                else if (config.flags&ENPASSWD) {
                    echsec('*',15);
                    prfmsg(ENTRPASS);
                    usrptr->substt=-1;
                }
                /* Master key holders only */
                else if (usrptr->flags&MASTER) return(init_dos());
                /* This shouldn't happen */
                else return(0);
                break;

        case 2: /* DIR command */
        case 3: /* DEL command */
        case 4: /* ATTRIB command */
        case 5: /* TYPE command */
        case 6: /* RENAME command */
        case 7: /* COPY command */
        case 9: /* COPY command */
       case 10: /* MOVE command */
       case 11: /* RECURSE command */
       case 12: /* CHKDSK command */
       case 13: /* HISTORY command */
       case 14: /* DELTREE command */
       case 16: /* USERS command */
       case 17: /* GREP command */
       case 99: /* Blank state */
                if (!(dosvda->flags&ABORTOP)) {
                    dosvda->flags|=(PAUSED+ABORTOP);
                    prfmsg(ABORT);
                }
                else {
                    if ((*margv[0]&223) == 'Y' || (*margv[0]&223) == 'X') end_cycle();
                    else if ((*margv[0]&223) == 'N') dosvda->flags&=~(PAUSED+ABORTOP);
                    else prfmsg(ABORT);
                }
                break;

        case 8: /* COPY overwrite */
                if ((*margv[0]&223) == 'Y') {
                    dosvda->flags|=OVERFILE;
                    usrptr->substt=7;
                    btuinj(usrnum,CYCLE);
                }
                else if ((*margv[0]&223) == 'A') {
                    dosvda->flags|=OVERALL;
                    usrptr->substt=7;
                    btuinj(usrnum,CYCLE);
                }
                else if ((*margv[0]&223) == 'N') {
                    if (fndnxt(&dosfile)) {
                        usrptr->substt=7;
                        btuinj(usrnum,CYCLE);
                    }
                    else if (recurse_dir()) {
                        dosvda->curop=7;
                        btuinj(usrnum,CYCLE);
                    }
                    else {
                        end_cycle();
                        shw_prompt(1);
                    }
                }
                else prfmsg(OVERWRIT,dosvda->dpath);
                break;

      case 100: //In DOS Shell Mode
                if (sameas("x",margv[0]) || sameas("exit",margv[0]))
                  {
                   btuchi(usrnum,NULL);
                   btupbc(usrnum,20);
                   dfaSetBlk(dosbt);

                   if (dfaAcqEQ(NULL,usaptr->userid,0)) dfaUpdateV(&dosptr,sizeof(struct dosrec)+1);
                   else dfaInsertV(&dosptr,sizeof(struct dosrec)+1);
                   prfmsg(EXITNOW);
                   outprf(usrnum);
                   dosactive[usrnum]=0;
                   return(0);
                  }
                else if (!margc) shw_prompt(usrptr->flags&INJOIP ? 1:0);
                else
                  {
                   /* Add's to doskey history */
                   rstrin();
                   if ((i=strlen(input)+1) != 1)
                     {
                      movmem(dosptr.history,vdatmp,2000);
                      strcpy(dosptr.history,input);
                      movmem(vdatmp,&dosptr.history[i],2000-i);

                      dosptr.doshigh=0;
                      dosptr.history[2000]='\0';
                      for (i=0; i < 2000; i++)
                        {if (dosptr.history[i] == '\0')
                           {dosptr.doshigh++;
                            if (dosptr.history[i+1] == '\0') break;
                           }
                        }
                     }
                   exec_cmd();
                  }

                break;

      case 101: //TIME command
                if (margc && !time_ok(margv[0]))
                  {prfmsg(BADTIME);
                   prfmsg(NEWTIME);
                   break;
                  }
                shw_prompt(1);
                break;

      case 102: //DATE command
                if (margc && !date_ok(margv[0]))
                  {prfmsg(BADDATE);
                   prfmsg(NEWDATE);
                   break;
                  }
                shw_prompt(1);
                break;
      case 103: //DEL command
                if ((*margv[0]&223) == 'Y') begin_cycle(3);
                else if ((*margv[0]&223) == 'N') shw_prompt(1);
                else prfmsg(ALLDELTD);
                break;
      case 104: //PAUSE command
                shw_prompt(1);
                break;
      case 105: //EDIT command
                if (!margc || !edit_file(margv[0])) shw_prompt(1);
                break;
      case 106: //LABEL command
                if (!margc || set_label(margv[0]));
                shw_prompt(1);
                break;
      case 107: //DEL command
                if ((*margv[0]&223) == 'Y')
                  {unlink(dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)));
                   if (fndnxt(&dosfile)) prfmsg(QDELETE,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)));
                   else shw_prompt(1);
                  }
                else if ((*margv[0]&223) == 'N')
                  {if (fndnxt(&dosfile)) prfmsg(QDELETE,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)));
                   else shw_prompt(1);
                  }
                else prfmsg(QDELETE,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)));
                break;

      case 109: if ( (*margv[0]&223)=='A' || (*margv[0]&223)=='F' )
                  {prfmsg(FAILINT);
                   shw_prompt(1);
                  }
                else if ((*margv[0]&223) == 'R')
                  {if (isalpha(*dosvda->sdrive) && fnd1st(&dosfile,dspr("%s*.*",dosvda->sdrive),ALLATTR))
                     {stzcpy(dosptr.curdir,dspr("%s%s",dosvda->sdrive,GETDIR(*dosvda->sdrive)),MAXPATH);
                      shw_prompt(1);
                     }
                   else prfmsg(ERRODRIV,*dosvda->sdrive);
                  }
                else prfmsg(ERRODRIV,*dosvda->sdrive);
                break;
      case 114: //DELTREE command
                if ((*margv[0]&223) == 'Y')
                  {fnd1st(&dosfile,dspr("%s%s",dosvda->dpath,dosvda->spath),ALLATTR);
                   prfmsg(DELTREEN,strlwr(dspr("%s%s",dosvda->dfile,dosvda->dext)));
                   begin_cycle(14);
                  }
                else if ((*margv[0]&223) == 'N') shw_prompt(1);
                else prfmsg(DELTREE,strlwr(dspr("%s%s",dosvda->dfile,dosvda->dext)));
                break;

      case 115: if (!margc || sameas(margv[0],"x")) shw_prompt(1);
                else new_speed(atoi(margv[0]));
                break;

      case 117: if (!margc || sameas(margv[0],"x")) shw_prompt(1);
                else
                  {rstrin();
                   stzcpy(input,dspr("edituser %s",input),INPSIZ);
                   exec_cmd();
                  }
                break;

      case 118: //DELUSER command
                if ((*margv[0]&223) == 'Y')
                  {dfaSetBlk(dosbt);
                   if (sameas(usaptr->userid,dosvda->text)) prfmsg(ACCSDEND);
                   else if (dfaAcqEQ(NULL,dosvda->text,0))
                     {dfaDelete();
                      prfmsg(DELUSER2,dosvda->text);
                     }
                   shw_prompt(1);
                  }
                else if ((*margv[0]&223) == 'N') shw_prompt(1);
                else prfmsg(DELUSER,dosvda->text);
                break;
       }
    }
  outprf(usrnum);
  return(1);
}

/****************************************************************************/
/* Status-Input Routine                                                     */
/****************************************************************************/

static VOID usrsts(VOID)
{ CHAR line[257];
  CHAR *c;
  INT i,siz,ok,cnt,COUNT;
  CHAR progress[]=" \304\315\360\315\304";

  if (usrptr->state != dosstt)
    {dfsthn();
     return;
    }

  if (dosvda->doskey)
    {c=dosptr.history;
     i=1;
     while (i < dosvda->doskey)
       {while (*c) c++; //go past first string
        c++; //at second
        i++;
       }

     siz=strlen(c);
     btucli(usrnum);
     for (i=0;i < siz; i++) chiinp(usrnum,c[i]);
     chious(usrnum,"\033[u");
     chious(usrnum,c);
     chious(usrnum,"\033[K");   //
     return;
    }

  setmbk(dosmb);
  COUNT=0;
  do
    {
     /* Turn this bit off, if we need to recycle, it will be set */
     dosvda->flags&=~INCYCLE;

     if (usrptr->substt == 99) return;
     if (dosvda->flags&MOREMODE && dosvda->line >= usaptr->scnfse-1)
       {dosvda->line=0;
        dosvda->flags|=PAUSED;
        btuinj(usrnum,CYCLE);
        prfmsg(MORE);
        outprf(usrnum);
        return;
       }
     if (btuoba(usrnum) < outbsz-512 || dosvda->flags&PAUSED)
       {btuinj(usrnum,CYCLE);
        return;
       }

     // RECURSE Get all directories for recursion
     if (dosvda->flags&RECURSE && !(dosvda->flags&NOWILD) && FATTR(dosfile)&FAMDIR && *FNAME(dosfile) != '.')
       {
        if (!dirlist_add(dspr(",%s%s",dosvda->sdir,FNAME(dosfile)),MAXDIR-2))
          {prfmsg(OUTFMEM2);
           end_cycle();
           outprf(usrnum);
           return;
          }
       }

     switch (usrptr->substt)
       {
        case 2: /* DIR command */
                if (!(dosvda->flags&NOTFOUND))
                  {
                   if (dosvda->line == 0 && dosvda->addflags == 1 && !(dosvda->flags&MOREMODE)) prfmsg(CONTINNG,dosvda->spath);
                   dir_show();
                   if (dosvda->flags&PAGEMODE && dosvda->line >= usaptr->scnfse-2)
                     {
                      dosvda->flags|=PAUSED;
                      prfmsg(PAUSE);
                      dosvda->line=0;
                     }
                  }

                if (fndnxt(&dosfile))   dosvda->flags|=INCYCLE;
                else if (recurse_dir()) dosvda->flags|=INCYCLE;
                else
                  {checkgrep();
                   if (dosvda->fnum || dosvda->dnum)
                     {if (dosvda->flags&WIDEMODE && dosvda->addflags != 1) prf("\n");
                      prfmsg(DIRFTR,
                             insrt_comma((LONG)dosvda->fnum),
                             insrt_comma(dosvda->space),
                             insrt_comma((LONG)dosvda->dnum),
                             insrt_comma(dskfrebytes(*dosvda->spath))
                            );
                      }
                   else prfmsg(FNOTFND);
                   end_cycle();
                  }
                break;

        case 3: /* DEL or ERASE files */
               if (!(FATTR(dosfile)&(FAMHID+FAMDIR))) unlink(dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)));
               if (fndnxt(&dosfile)) dosvda->flags|=INCYCLE;
               else end_cycle();
               break;

        case 4: /* ATTRIB */
                if (!(FATTR(dosfile)&FAMDIR) || (dosvda->attr && FATTR(dosfile)&FAMDIR))
                  {
                   if (dosvda->addflags || dosvda->remflags)
                     {FATTR(dosfile)|=dosvda->addflags;
                      FATTR(dosfile)&=~dosvda->remflags;
                      /* The FAM* attribute bits have the same values as   */
                      /* Windows' FILE_ATTRIBUTE_* bits.  (The WorldGroup  */
                      /* NT build left this out, so ATTRIB +/- did nothing.)*/
                      if (!SetFileAttributesA(dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)),
                                              (FATTR(dosfile)&(FAMRON+FAMHID+FAMSYS+FAMARC)) ?
                                              (DWORD)(FATTR(dosfile)&(FAMRON+FAMHID+FAMSYS+FAMARC)) :
                                              FILE_ATTRIBUTE_NORMAL)) prfmsg(PERMDEND);
                     }
                   else
                     {
                      prf("  ");
                      prf (FATTR(dosfile)&FAMARC ? "A":" ");
                      prf("  ");
                      prf (FATTR(dosfile)&FAMSYS ? "S":" ");
                      prf (FATTR(dosfile)&FAMHID ? "H":" ");
                      prf (FATTR(dosfile)&FAMRON ? "R":" ");
                      prf("     ");
                      prf("%s%s%s\n",dosvda->sdrive,dosvda->sdir,FNAME(dosfile));
                      dosvda->line++;
                     }
                  }
                if (fndnxt(&dosfile)) dosvda->flags|=INCYCLE;
                else end_cycle();
                break;

        case 5: /* TYPE or MORE command */
                if (fgets(line,256,dosvda->srcfile) != NULL) //Read info
                  {
                   if (dosvda->flags&LINENUMS)
                     {dosvda->space++;
                      prf(dspr("\033[0;1m%ld:\033[0m",(LONG)dosvda->space));
                     }
                   siz=strlen(line);
                   for (i=0;i < siz; i++)
                     {
                      if (line[i] == '\n') prf("\r");
                      else if (line[i] == '\t') for (cnt=8; cnt > siz%8; cnt--) prf(" ");
                      else prf("%c",line[i]);

                     }
                   dosvda->line++;
                   dosvda->flags|=INCYCLE;
                  }
                else                    //output to user
                  {
                   fclose(dosvda->srcfile);
                   dosvda->srcfile=NULL;
                   end_cycle();
                  }
                break;

        case 6: /* REN or RENAME files */
                i=1;
                if (!(FATTR(dosfile)&(FAMHID+FAMDIR)))
                  {
                   if (!file_names())
                     {prfmsg(PNOTFND2,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)));
                      i=0;
                     }
                   else if (rename(dosvda->spath,dosvda->dpath))
                     {
                      prfmsg(DUPLFILE);
                      i=0;
                     }
                  }

                if (i && fndnxt(&dosfile)) dosvda->flags|=INCYCLE;
                else end_cycle();
                break;

        case 7: /* COPY files */
                if (!(dosvda->flags&NOTFOUND) && !(FATTR(dosfile)&FAMDIR))
                  {
                   if (!file_names())
                     {/* path too long to hold: skip this file */
                      prfmsg(PNOTFND2,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)));
                      if (fndnxt(&dosfile)) dosvda->flags|=INCYCLE;
                      else if (recurse_dir()) dosvda->flags|=INCYCLE;
                      else
                        {if (!(dosvda->flags&MOVEMODE)) prfmsg(FILESCOP,dosvda->fnum);
                         end_cycle();
                        }
                      break;
                     }

                   /* make sure not copying over ourselves */
                   if (sameas(dosvda->spath,dosvda->dpath))
                     {prfmsg(dosvda->flags&MOVEMODE ? SRCDSSAM:CPNOSELF,dosvda->spath);
                      end_cycle();
                      break;
                     }

                   if (!(dosvda->flags&OVERALL) && !(dosvda->flags&OVERFILE) && fnd1st(&dosvda->tempfile,dosvda->dpath,FAMARC))
                     {prfmsg(OVERWRIT,dosvda->dpath);
                      usrptr->substt=8;
                      break;
                     }
                   dosvda->flags&=~OVERFILE;

                   if ((dosvda->srcfile=fopen(dosvda->spath,FOPRB)) != NULL)
                     {if ((dosvda->destfile=fopen(dosvda->dpath,FOPWB)) != NULL)
                        {usrptr->substt=9;
                         dosvda->fnum++;
                         dosvda->line++;
                         dosvda->space=0;
                         if (dosvda->flags&MOVEMODE) prf("%s => %s \033[s%d%%\033[u",strlwr(dosvda->spath),strlwr(dosvda->dpath),0);
                         else prf("%s \033[s%d%%\033[u",dosvda->spath,0);
                         dosvda->flags|=INCYCLE;
                        }
                      else
                        {prfmsg(PNOTFND2,dspr("%s%s",dosvda->ddrive,dosvda->ddir));
                         end_cycle();
                        }
                     }
                   else
                     {dosvda->srcfile=NULL;
                      prfmsg(PNOTFND2,dspr("%s%s",dosvda->sdrive,dosvda->sdir));
                      end_cycle();
                     }
                  }
                //Archive not found, search
                else if (fndnxt(&dosfile)) dosvda->flags|=INCYCLE;  //fndnxt(&dosfile)
                else if (recurse_dir()) dosvda->flags|=INCYCLE;
                else
                  {if (dosvda->flags&MOVEMODE);
                   else prfmsg(FILESCOP,dosvda->fnum);
                   end_cycle();
                  }
                break;

       case 9: /* COPYing files */
                i=0;
                do {
                    siz=fread(line,1,256,dosvda->srcfile);
                    if (fwrite(line,1,siz,dosvda->destfile) != siz) break;  /* disk full: ferror() is now set */
                    i++;
                    dosvda->space+=siz;
                   } while (siz && i < dosptr.speed+1);

                /* 64-bit math: a 32-bit "bytes x 100" overflows on files */
                /* over about 21 MB and the meter showed garbage.         */
                prf("\033[s%d%%\033[u",(INT)(((WLONG)dosvda->space*100)/((WLONG)FSIZEU(dosfile)+1)));

                if (i < dosptr.speed+1)
                  {GBOOL copyerr;

                   /* (The DOS builds also copied the file's date and    */
                   /* time here; the Windows builds never did.)          */
                   /* A read or write error (such as a full disk) means  */
                   /* the copy is incomplete: MOVE must then keep the    */
                   /* source.  The old code deleted it regardless.       */
                   copyerr=(ferror(dosvda->srcfile) || ferror(dosvda->destfile));
                   fclose(dosvda->srcfile);
                   if (fclose(dosvda->destfile) != 0) copyerr=TRUE;
                   dosvda->srcfile=NULL;
                   dosvda->destfile=NULL;


                   if (copyerr)
                     {prf("\n");
                      prfmsg(FILEBAD);
                      end_cycle();
                      break;
                     }
                   if (dosvda->flags&MOVEMODE)
                     {
                      prf("[ok]\n");
                      unlink(dosvda->spath);
                     }
                   else prf("    \n");

                   if (fndnxt(&dosfile))
                     {usrptr->substt=7;
                      dosvda->flags|=INCYCLE;
                     }
                   else if (recurse_dir())
                     {dosvda->curop=7;
                      dosvda->flags|=INCYCLE;
                     }
                   else
                     {if (dosvda->flags&MOVEMODE);
                      else prfmsg(FILESCOP,dosvda->fnum);
                      end_cycle();
                     }
                  }
                else dosvda->flags|=INCYCLE;
                break;

       case 11: /* RECURSE */
                if (dosvda->flags&NOWILD && fndnxt(&dosfile))
                  {
                   if (FATTR(dosfile)&FAMDIR && *FNAME(dosfile) !='.'
                       && !dirlist_add(dspr(",%s%s",dosvda->sdir,FNAME(dosfile)),MAXDIR-2))
                     {prfmsg(OUTFMEM2);
                      end_cycle();
                      break;
                     }
                  }
                else
                  {
                   //Pop a directory
                   if ((c=strrchr(dosvda->text,',')) != NULL)
                     {*c='\0';
                      /* (dirlist_add() only lists directories whose path */
                      /* fits, so this split always succeeds.)            */
                      s_dir(dspr("%s%s\\%s",dosvda->sdrive,c+1,dspr("%s%s",dosvda->sfile,dosvda->sext)),0);
                      if (fnd1st(&dosfile,dosvda->spath,FAMARC+FAMDIR))
                        {dosvda->flags&=~NOTFOUND;
                         if (dosvda->curop == 2) prfmsg(DIROF,dosvda->spath);
                        }
                      else dosvda->flags|=NOTFOUND;

                      /* Recursive XCOPY routine */
                      if (dosvda->curop == 7)
                        {i=0;
                         while (dosvda->srcdir[i] == dosvda->sdir[i] && dosvda->sdir[i]) i++;
                         stzcpy(line,dspr("%s%s%s",&dosvda->sdir[i],dosvda->dfile,dosvda->dext),MAXPATH);
                         if (!d_dir(dspr("%s%s%s",dosvda->ddrive,dosvda->destdir,line),0))
                           {/* The destination path is too long.  Skip this */
                            /* directory's files: dpath still names the     */
                            /* last directory, and copying there would be   */
                            /* wrong.  "|" can never match, so the search   */
                            /* below ends at once and the job moves on.     */
                            prfmsg(PNOTFND2,line);
                            fnd1st(&dosfile,"|",0);
                            dosvda->flags|=NOTFOUND;
                           }
                         else
                           {stzcpy(line,dspr("%s%s%s",dosvda->ddrive,dosvda->destdir,&dosvda->sdir[i]),MAXPATH);
                            line[strlen(line)-1]='\0';
                            mkdir(line);
                            if (DialDos&DEBUGMODE) prf("\nMKDIR=%s\n",line);
                           }
                        }
                     }
                   else
                     {dosvda->flags&=~RECURSE;
                      dosvda->flags|=NOTFOUND;
                     }
                   usrptr->substt=dosvda->curop;
                  }
                dosvda->flags|=INCYCLE;
                break;

       case 12: /* CHKDSK command */
                if (!(dosvda->flags&NOTFOUND))
                  {
                   if (FATTR(dosfile)&FAMDIR)
                     {
                      if (*FNAME(dosfile) != '.') dosvda->dnum++;
                     }
                   else if (FATTR(dosfile)&FAMHID)
                     {dosvda->hspace+=FSIZEU(dosfile);
                      dosvda->hfnum++;
                     }
                   else
                     {dosvda->space+=FSIZEU(dosfile);
                      dosvda->fnum++;
                     }
                  }
                dosvda->line=(dosvda->line%5)+1;
                prf("\033[s%c\033[u",progress[dosvda->line]);

                if (fndnxt(&dosfile)) dosvda->flags|=INCYCLE;
                else if (recurse_dir()) dosvda->flags|=INCYCLE;
                else
                  {
                   chkdsk_ftr();
                   end_cycle();
                  }
                break;

       case 13: /* HISTORY command */
                c=dosptr.history;
                i=1;
                while (i < dosvda->fnum)
                  {while (*c) c++; //go past first string
                   c++; //at second
                   i++;
                  }
                dosvda->line++;
                prfmsg(HISTORY,dosvda->fnum-1,c);
                dosvda->fnum--;
                if (dosvda->fnum > 1) dosvda->flags|=INCYCLE;
                else end_cycle();
                break;

       case 14: /* DELTREE command */
                if (FATTR(dosfile)&FAMDIR)
                  {/* Grab dir & try to remove, if cant remove add it to recurse list */
                   if (*FNAME(dosfile) != '.' && rmdir(dspr("%s%s%s",dosvda->dpath,dosvda->spath,FNAME(dosfile))))
                     {/* The list entry is copied back into spath (80  */
                      /* characters).  A longer one would be cut, and   */
                      /* "<cut path>*.*" could match - and delete -     */
                      /* files in another directory, so stop instead.   */
                      if (strlen(dosvda->spath)+strlen(FNAME(dosfile))+1 > MAXPATH-1)
                        {prfmsg(PNOTFND2,dspr("%s%s%s",dosvda->dpath,dosvda->spath,FNAME(dosfile)));
                         end_cycle();
                         break;
                        }
                      if (!dirlist_add(dspr(",%s%s\\",dosvda->spath,FNAME(dosfile)),MAXPATH-1))
                        {prfmsg(OUTFMEM2);
                         end_cycle();
                         break;
                        }
                     }
                   else if (*FNAME(dosfile) != '.' && DialDos&DEBUGMODE) prf("Removing %s%s%s\n",dosvda->dpath,dosvda->spath,FNAME(dosfile));
                  }
                else
                  {if (DialDos&DEBUGMODE) prf("Deleting %s%s%s\n",dosvda->dpath,dosvda->spath,FNAME(dosfile));
                   unlink(dspr("%s%s%s",dosvda->dpath,dosvda->spath,FNAME(dosfile)));
                  }

                if (fndnxt(&dosfile)) dosvda->flags|=INCYCLE;
                else if ((c=strrchr(dosvda->text,',')) != NULL)
                  {*c='\0';
                   stzcpy(dosvda->spath,c+1,MAXPATH);
                   fnd1st(&dosfile,dspr("%s%s*.*",dosvda->dpath,dosvda->spath),ALLATTR);
                   dosvda->flags|=INCYCLE;
                  }
                else
                  {rmdir(dosvda->dpath);
                   /* If found something in the original dir we wanted to deltree, cycle again */
                   if (fnd1st(&dosfile,dspr("%s\\*.*",dosvda->dpath),ALLATTR))
                     {*dosvda->text='\0';
                      stzcpy(dosvda->spath,"\\",MAXPATH);
                      dosvda->flags|=INCYCLE;
                     }
                   else end_cycle();
                  }
                break;

        case 16: /* USERS command */
                dfaSetBlk(dosbt);
                if (!(dosvda->space))
                  {dfaQueryGE(emptykey,0);
                   dfaAbsRec(NULL,0);
                   prfmsg(USERSHDR);
                  }
                else dfaGetAbs(NULL,(LONG)dosvda->space,0);
                prfmsg(USERS,dosbuf->userid,dosbuf->homedir,(dosbuf->security&CANMOVE) ? "YES":"NO");

                if (dfaQueryNX())
                  {dosvda->space=dfaAbs();
                   dosvda->flags|=INCYCLE;
                  }
                else
                  {prfmsg(USERSFTR);
                   end_cycle();
                  }
                break;

        case 17: /* GREP */

                 /* Print status gadget */
                 if (dosvda->fnum == 0)
                   {dosvda->dnum=(dosvda->dnum%25)+1;
                    if (!(dosvda->dnum%5)) prf("\033[s%c\033[u",progress[dosvda->dnum/5]);
                   }

                 if (dosvda->space==0)
                   {if ((dosvda->srcfile=fopen(dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)),FOPRA)) != NULL)
                      dosvda->space=1;
                    else
                      {prfmsg(GREPBAD,FNAME(dosfile));
                       if (fndnxt(&dosfile)) dosvda->flags|=INCYCLE;
                       else end_cycle();
                       break;
                      }
                   }

                 /* Read info & scan for text */
                 ok=1;
                 cnt=0;
                 do
                   {cnt++;
                    if (fgets(line,256,dosvda->srcfile) != NULL)
                      {siz=0;
                       if (samein(dosvda->greptext,line))
                         {siz=1;
                          dosvda->hspace=dosvda->hfnum;
                          if (dosvda->fnum==0)
                            {prf(" \b");
                             dosvda->line+=2;
                             prfmsg(GREPCMD,FNAME(dosfile));
                             dosvda->fnum=1;
                            }
                         }
                       /* Takes care of next XX consecutive lines for grep */
                       if (siz || dosvda->hspace > 0)
                         {siz=strlen(line);
                          for (i=0;i < siz; i++) prf("%c",line[i]);
                          dosvda->line++;
                          dosvda->hspace--;
                         }
                      }
                    else ok=0;
                   } while (ok && !siz && cnt < dosptr.speed+1);


                 if (ok) dosvda->flags|=INCYCLE;
                 else
                   {dosvda->space=0;
                    dosvda->hspace=0;
                    dosvda->fnum=0;
                    fclose(dosvda->srcfile);
                    dosvda->srcfile=NULL;
                    if (fndnxt(&dosfile)) dosvda->flags|=INCYCLE;
                    else end_cycle();
                   }
                 break;

        default: dfsthn();
                 break;

       }

     checkgrep();

     outprf(usrnum);
     clrprf();
     COUNT++;
    } while (COUNT < 5 && dosvda->flags&INCYCLE);

  /* Call again if not done... */
  if (dosvda->flags&INCYCLE) btuinj(usrnum,CYCLE);
}

/*
  Checks grep capability
*/
static VOID checkgrep(VOID)
{
  if (dosvda->flags&GREPMODE && !samein(dosvda->greptext,prfbuf))
    {clrprf();
     dosvda->line--;
    }
}

/*
  Appends ",<dir>" to the list of directories still to visit (DIR /S,
  XCOPY /S, CHKDSK, DELTREE), which is kept in dosvda->text.  Returns FALSE
  if it would not fit; the caller then stops with "Out of memory".
  (The old check allowed the list to run past the end of the buffer.)

  maxlen is the longest directory the job can hold once the entry is read
  back (MAXDIR-2 leaves room for the trailing backslash in sdir).  Before,
  a longer one was cut and the job went on in a different directory; it
  is now skipped with "Path not found".
*/
static GBOOL dirlist_add(const CHAR *entry,size_t maxlen)
{
  if (strlen(entry+1) > maxlen)
    {prfmsg(PNOTFND2,entry+1);
     return(TRUE);
    }
  if (strlen(dosvda->text)+strlen(entry) >= sizeof(dosvda->text)) return(FALSE);
  strcat(dosvda->text,entry);
  return(TRUE);
}


/****************************************************************************/
/* "injoth" Routine                                                         */
/****************************************************************************/

/****************************************************************************/
/* User Logoff Supplemental Routine                                         */
/****************************************************************************/

/****************************************************************************/
/* Hangup Routine                                                           */
/****************************************************************************/

/****************************************************************************/
/* Midnight Clean-up Routine                                                */
/****************************************************************************/

/****************************************************************************/
/* Delete Account Routine                                                   */
/****************************************************************************/

VOID udeltd(CHAR *uid)
{
  /* The engine calls this between its own dfaSetBlk()/dfaRstBlk() pair,  */
  /* so the block must be restored here or the engine's restore pops ours */
  /* and leaves its caller on the wrong file.                             */
  dfaSetBlk(dosbt);
  if (dfaAcqEQ(NULL,uid,0)) dfaDelete();
  dfaRstBlk();
}

/****************************************************************************/
/* Hangup (Lost Carrier) Routine                                            */
/****************************************************************************/

VOID doshup(VOID)
{
  /* Close any file a job left open.  The handles are only valid once     */
  /* init_dos() has run for this caller and they are still in DS-DOS.     */
  if (dosactive[usrnum] && usrptr->state == dosstt)
    {if (dosvda->srcfile != NULL) fclose(dosvda->srcfile);
     if (dosvda->destfile != NULL) fclose(dosvda->destfile);
     if (dosvda->flags&BATCHFILE) fclose(dosvda->batfile);
     dosvda->srcfile=NULL;
     dosvda->destfile=NULL;
     dosvda->flags&=~BATCHFILE;
    }
  dosactive[usrnum]=0;

  /* The editor drops its text without calling save_file(), so release    */
  /* the shared editor here or nobody else could use EDIT.                */
  if (editowner == usrnum)
    {DialDos&=~INEDITOR;
     editowner=-1;
    }
}

/****************************************************************************/
/* System Shutdown Routine                                                  */
/****************************************************************************/

VOID shtdwn(VOID)
{
  clsmsg(dosmb);
  dfaClose(dosbt);
}

/****************************************************************************/
/* Global Command Handler                                                   */
/****************************************************************************/

/****************************************************************************/
/* Procedures & Functions                                                   */
/****************************************************************************/

#define Dosvda ((struct vdados *)vdaoff(chan))

#define ANSIIN 1
#define FUNCIN 2

CHAR chrprc(INT chan, INT c)
{
  if (Dosvda->flags&PAUSED && !(Dosvda->flags&ABORTOP) && c != 3)
    {if (Dosvda->flags&MOREMODE) chious(chan,"\r\n");
     Dosvda->flags&=~PAUSED;
     return(0);
    }

  if (Dosvda->lastinp == 27)
    {
     if (c == '[')
       {Dosvda->lastinp=ANSIIN;
        return(0);
       }
     else
       {btucli(chan);
        chious(chan,"\033[u\033[K");
       }
    }


  switch(c)
    {
      /* <Backspace> */
      case 127  :
      case '\b' : return('\b');

      /* CTRL+C */
      case 3    : if (usroff(chan)->substt < 99) usroff(chan)->substt=99;
                  btuclo(chan);
                  btucli(chan);
                  chiinp(chan,c);
                  chiinp(chan,'\r');
                  chious(chan,"^C");
                  return(0);

      /* <ESC> Abort */
      case 27   : Dosvda->lastinp=c;
                  return(0);


      case '\r' : Dosvda->lastinp=0;
                  Dosvda->doskey=0;
                  return('\r');

      default   : if (Dosvda->lastinp == ANSIIN)
                    {
                     switch(c)
                       {
                        /* Cursor Up */
                        case 'A': if (Dosvda->doskey < Dosvda->data.doshigh)
                                    {
                                     Dosvda->doskey++;
                                     chiinj(chan,CYCLE);
                                    }
                                  break;

                        /* Cursor Down */
                        case 'B': if (Dosvda->doskey > 1)
                                    {
                                     Dosvda->doskey--;
                                     chiinj(chan,CYCLE);
                                    }
                                  else
                                    {Dosvda->doskey=0;
                                     btucli(chan);
                                     chious(chan,"\033[u\033[K");
                                    }
                                  break;

                        /* ANSI Home
                        case 'H': Dosvda->location=0;
                                  chious(chan,"\033[u");
                                  break;

                        /* ANSI End
                        case 'K': break;

                        /* Cursor Right
                        case 'C': if (Dosvda->location)
                                    {Dosvda->location--;
                                     chious(chan,"\033[[D");
                                    }
                                  //chious(chan,"RIGHT");
                                  break;

                        /* Cursor Left
                        case 'D': if (Dosvda->location)
                                    {Dosvda->location--;
                                     chious(chan,"\033[[D");
                                    }
                                  break;
                       */
                       }
                     Dosvda->lastinp=0;
                     return(0);
                    }
                  if (c < 32 || c > 126) return(0); //Ignore CTRL chars
                  Dosvda->lastinp=0;
                  return(c);
    }
}


/*
  Shows the Prompt
*/
static VOID shw_prompt(CHAR LF)
{ CHAR *c=dosptr.prompt;
  CHAR i;

  usrptr->substt=100;
  usrptr->state=dosstt;
  prf("\033[0m");
  if (LF) prf("\n");
  while (*c)
    {
     if (*c == '$')
       {c++;
        if (*c == 0) return;
        switch (*c&223)//upcase
          {
           case 'Q':prf("=");break;
           case '$':prf("$");break;
           case 'T':uprf(prntim(4,now()));break;
           case 'D':uprf(ncdate(today()));break;
           case 'P':if ((i=(CHAR)strlen(dosptr.curdir)) != 3) dosptr.curdir[--i]='\0';
                    prf("%s",dosptr.curdir);  /* a % in a dir name must not be read as a format */
                    if (i != 3) dosptr.curdir[i]='\\';
                    break;
           case 'V':prf("%s",verinfo);break;
           case 'N':prf("%c",*dosptr.curdir);break;
           case 'G':prf(">");break;
           case 'L':prf("<");break;
           case 'B':prf("|");break;
           case '_':prf("\n");break;
           case 'E':prf("\033");break;
           case 'H':prf("\b");break;   /* $H = backspace (was "\8", which printed an 8) */
          }
       }
     else prf("%c",*c);
     c++;
    }
  prf("\033[s");
}


/*
  Executes commands from command line
*/
static VOID exec_cmd(VOID)
{ INT i,status,switches=0;
  CHAR *param,*param2,*ptr;
  CHAR command[9];
  CHAR swiches[31];

  dosvda->line=dosvda->attr=dosvda->fnum=dosvda->dnum=dosvda->hfnum=0;
  dosvda->space=dosvda->hspace=0;

  *swiches='\0';
  rstrin();
  param=input;
  while (*param)
    {
     if (*param == '/' && !switches)
       {switches=1;
        stzcpy(swiches,param,31);
        *param='\0';
       }
     else if (*param == '|')
       {strupr(param);
        if (!samein("MORE",param) && !samein("GREP ",param))
          {prfmsg(SYNTAX);
           shw_prompt(1);
           return;
          }
        if (samein("MORE",param)) dosvda->flags|=MOREMODE;

        if (samein("GREP ",param))
          {/* In case they pipe it to more */
           if ((ptr=strchr(param+1,'|')) != NULL) *ptr='\0';

           ptr=strstr(param,"GREP")+4;
           while (*ptr==' ') ptr++;
           dosvda->flags|=GREPMODE;
           stzcpy(dosvda->greptext,ptr,128);
           depad(dosvda->greptext);
          }
        *param='\0';
       }
     else if (*param == '>')
       {dosvda->flags|=SENDNUL;
        *param='\0';
       }
     param++;
    }
  parsin();

  param=margv[0];
  while (*param &&  *param != '.' &&  *param != '\\') param++;

  i=(INT)(param-margv[0])+1;
  if (i > 9) i=9;
  stzcpy(command,margv[0],i);

  switch(margc)
    {case 1: param2=margn[0];
             break;

     case 2: if (*param=='\0')
               {param=margv[1];
                param2=margn[1];
               }
             else param2=margv[1];
             break;

    default: if (*param=='\0')
               {param=margv[1];
                param2=margv[2];
               }
             else param2=margv[1];
             break;
    }


  /* begin command interpretation */

  /* HELP */
  if (sameas(command,"help") || sameas(command,"?") || strstr(swiches,"/?"))
    {
     if (strstr(swiches,"/?")) param=command;
     if (!(*param)) prfmsg(HELPME12);
     else
       {if (sameas(param,"help") || sameas(param,"?")) prfmsg(HELPHLP);
        else if (sameas(param,"ver")) prfmsg(VERHLP);
        else if (sameas(param,"cls")) prfmsg(CLSHLP);
        else if (sameas(param,"rem")) prfmsg(REMHLP);
        else if (sameas(param,"debug")) prfmsg(DEBUGHLP);
        else if (sameas(param,"prompt")) prfmsg(PRMPTHLP);
        else if (sameas(param,"echo")) prfmsg(ECHOHLP);
        else if (sameas(param,"pause")) prfmsg(PAUSEHLP);
        else if (sameas(param,"time")) prfmsg(TIMEHLP);
        else if (sameas(param,"date")) prfmsg(DATEHLP);
        else if (sameas(param,"vol")) prfmsg(VOLHLP);
        else if (sameas(param,"dir")) prfmsg(DIRHLP);
        else if (sameas(param,"cd") || sameto(param,"chdir")) prfmsg(CDHLP);
        else if (sameas(param,"md") || sameto(param,"mkdir")) prfmsg(MDHLP);
        else if (sameas(param,"rd") || sameto(param,"rmdir")) prfmsg(RDHLP);
        else if (sameas(param,"del") || sameto(param,"erase")) prfmsg(DELHLP);
        else if (sameas(param,"attrib")) prfmsg(ATTRHLP);
        else if (sameas(param,"type") || sameto(param,"more")) prfmsg(TYPHLP);
        else if (sameas(param,"grep")) prfmsg(GREPHLP);
        else if (sameas(param,"upload") || sameto(param,"put")) prfmsg(UPLHLP);
        else if (sameas(param,"download") || sameto(param,"get")) prfmsg(DNLHLP);
        else if (sameas(param,"edit")) prfmsg(EDITHLP);
        else if (sameas(param,"set")) prfmsg(SETHLP);
        else if (sameas(param,"label")) prfmsg(LABELHLP);
        else if (sameto(param,"ren") || sameto(param,"rename")) prfmsg(RENHLP);
        else if (sameas(param,"copy")) prfmsg(COPYHLP);
        else if (sameas(param,"move")) prfmsg(MOVEHLP);
        else if (sameas(param,"xcopy")) prfmsg(XCOPYHLP);
        else if (sameas(param,"mem")) prfmsg(MEMMHLP);
        else if (sameas(param,"chkdsk")) prfmsg(CHKDSHLP);
        else if (sameas(param,"history")) prfmsg(HISTHLP);
        else if (sameas(param,"clear")) prfmsg(CLEARHLP);
        else if (sameas(param,"deltree")) prfmsg(DELTRHLP);
        else if (sameas(param,"speed")) prfmsg(SPEEDHLP);
        else if (sameas(param,"edituser")) prfmsg(USERHLP);
        else if (sameas(param,"deluser")) prfmsg(DUSRHLP);
        else if (sameas(param,"users")) prfmsg(USERSHLP);
        else if (sameas(param,"users")) prfmsg(CFGHLP);
        else prfmsg(HELPHLP);
       }
    }

  /* Drive Change */
  else if (command[1]==':' && command[2] == '\0')
    {
     if (dosptr.security&CANMOVE)
       {stzcpy(dosvda->sdrive,strupr(command),MAXDRIVE);
        if (isalpha(*dosvda->sdrive) && fnd1st(&dosfile,dspr("%s*.*",dosvda->sdrive),ALLATTR))
          stzcpy(dosptr.curdir,dspr("%s%s",dosvda->sdrive,GETDIR(*dosvda->sdrive)),MAXPATH);
        else
          {usrptr->substt=109;
           prfmsg(ERRODRIV,*dosvda->sdrive);
          }
       }
     else prfmsg(ACCSDEND);
    }

  /* DEBUG mode */
  /* DEBUG is board-wide (it shows every user internal paths) and clears  */
  /* the shared editor lock, so it needs the sysop key, like EDITUSER.     */
  else if (sameas(command,"debug"))
    {if (haskey(config.syskey) != 1) prfmsg(ACCSDEND);
     else
       {DialDos^=DEBUGMODE;
        DialDos&=~INEDITOR;
        editowner=-1;
        prfmsg(DEBUGMOD,(DialDos&DEBUGMODE) ? "ON":"OFF");
       }
    }

  /* REM line */
  else if (sameas(command,"rem"));

  /* CLS */
  else if (sameas(command,"cls")) prf("\033[2J");

  /* VER */
  else if (sameas(command,"ver")) prfmsg(VERINFO,verinfo);

  /* PROMPT */
  else if (sameas(command,"prompt"))
    {if (margc > 1)
       {rstrin();
        stzcpy(dosptr.prompt,param,MAXDIR);
       }
     else prf("%s\n",dosptr.prompt);
    }

  /* ECHO */
  else if (sameas(margv[0],"echo.")) prf("\n");
  else if (sameas(command,"echo"))
    {if (margc==1) prfmsg(ECHOIS,(dosvda->flags&ECHO) ? "on" : "off");
     else
       {if (sameas(margv[1],"on"))
          {if (dosvda->flags&BATCHFILE) dosvda->flags|=BATCHECHO;
           else dosvda->flags|=ECHO;
          }
        else if (sameas(margv[1],"off"))
          {if (dosvda->flags&BATCHFILE) dosvda->flags&=~BATCHECHO;
           else dosvda->flags&=~ECHO;
          }
        else
          {rstrin();
           xltctls(margv[1]);
           prf("%s\n",margv[1]);
          }
       }
    }

  /* PAUSE */
  else if (sameas(command,"pause"))
    {prfmsg(PAUSE);
     usrptr->substt=104;
    }

  /* TIME */
  else if (sameas(command,"time"))
    {if (!(dosptr.security&CMD_TIME)) prfmsg(ACCSDEND);
     else if (margc > 1)
       {if (!time_ok(margv[1]))
          {prfmsg(BADTIME);
           prfmsg(NEWTIME);
           usrptr->substt=101;
          }
       }
     else time_now();
    }

  /* DATE */
  else if (sameas(command,"date"))
    {if (!(dosptr.security&CMD_DATE)) prfmsg(ACCSDEND);
     else if (margc > 1)
       {if (!date_ok(margv[1]))
          {prfmsg(BADDATE);
           prfmsg(NEWDATE);
           usrptr->substt=102;
          }
       }
     else
       {prfmsg(NOWDATE,prnday(today(),3),prndat(16,today(),'-'));
        prfmsg(NEWDATE);
        usrptr->substt=102;
       }
    }

  /* VOL */
  else if (sameas(command,"vol"))
    {if (!(dosptr.security&CANMOVE)) prfmsg(ACCSDEND);
     else if (!(*param) || (isalpha(*param) && param[1]==':' && param[2]=='\0'))
       {status=s_dir(*param ? param:dspr("%c:",*dosptr.curdir),0);
        if (status && fnd1st(&dosfile,dspr("%s\\*.*",dosvda->sdrive),FAMARC+FAMDIR))
          {if (fnd1st(&dosfile,dspr("%s\\*.*",dosvda->sdrive),FAMVID))
             prfmsg(VOLUME,*dosvda->sdrive,FNAME(dosfile),"serial");
           else prfmsg(VOLUMEN,*dosvda->sdrive,"serial");
          }
        else prfmsg(INVLDRIV);
       }
      else prfmsg(PARMNCRR,param);
    }

  /* DIR */
  else if (sameas(command,"dir") || sameas(command,"ls"))
    {rstrin();
     dir_begin(param,swiches);
    }

  /* CD */
  else if (sameas(command,"cd") || sameas(command,"chdir"))
    {if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param=='\0')
       {if ((i=strlen(dosptr.curdir)) != 3) dosptr.curdir[--i]='\0';
        prf("%s\n",dosptr.curdir);
        if (i != 3) dosptr.curdir[i]='\\';
       }
     else
       {
        if (s_dir(param,MAKEDIR) && fnd1st(&dosfile,dspr("%s*.*",dosvda->spath),FAMARC+FAMDIR)) //nul.txt
          {//Move to new directory ONLY if on current drive
           if (*dosptr.curdir == *dosvda->spath) stzcpy(dosptr.curdir,dosvda->spath,MAXPATH);
           stzcpy(GETDIR(*dosvda->spath),&dosvda->spath[2],MAXDIR);
          }
        else
          {
           status=s_dir(param,MAKEDIR);
           dosvda->spath[strlen(dosvda->spath)-1]='\0';
           if (status && fnd1st(&dosfile,dosvda->spath,FAMDIR)) //nul.txt FAMARC+FAMDIR
             {//Move to new directory ONLY if on current drive
              dosvda->spath[strlen(dosvda->spath)]='\\';
              if (*dosptr.curdir == *dosvda->spath) stzcpy(dosptr.curdir,dosvda->spath,MAXPATH);
              stzcpy(GETDIR(*dosvda->spath),&dosvda->spath[2],MAXDIR);
             }
           else prfmsg(INVLDIR);
          }
       }
    }

  /* MD or MKDIR */
  else if (sameas(command,"md") || sameas(command,"mkdir"))
    {if (!(dosptr.security&CMD_MD)) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param=='\0') prfmsg(REQPARM);
     else if (strlen(dosptr.curdir) > MAXDIR-12) prfmsg(ACCSDEND);
     else if (s_dir(param,0))
       {if (fnd1st(&dosfile,dosvda->spath,FAMDIR)) prfmsg(DIREXST); //nul.txt
        else if (mkdir(dosvda->spath)) prfmsg(UNABLEDI);
       }
     else prfmsg(UNABLEDI);
    }

  /* RD or RMDIR */
  else if (sameas(command,"rd") || sameas(command,"rmdir"))
    {if (!(dosptr.security&CMD_RD)) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param == '\0') prfmsg(REQPARM);
     else if (s_dir(param,0))
       {if (sameas(dspr("%s\\",dosvda->spath),dosptr.curdir)) prfmsg(ATMPCUR);
        else if (rmdir(dosvda->spath)) prfmsg(INVPATH);
       }
     else prfmsg(INVPATH);
    }

  /* DEL or ERASE */
  else if (sameas(command,"del") || sameas(command,"erase") || sameas(command,"rm"))
    {if (!(dosptr.security&CMD_DEL)) prfmsg(ACCSDEND);
     else if (switches && !samein("/p",swiches)) prfmsg(INVLSWIT,swiches);
     else if (*param=='\0') prfmsg(REQPARM);
     else
       {status=s_dir(param,0);
        if (dosvda->spath[strlen(dosvda->spath)-1] == '\\') s_dir(dosvda->spath,WILDCARD);
        if (status && fnd1st(&dosfile,dosvda->spath,FAMARC+FAMDIR))
          {
           if ((FATTR(dosfile)&FAMDIR || *FNAME(dosfile)=='.') && !strchr(dosvda->spath,'*') && !strchr(dosvda->spath,'?'))
             {
              mdir(dosvda->spath);
              strcat(dosvda->spath,"*.*");
              s_dir(dosvda->spath,0);
             }

           if (fnd1st(&dosfile,dosvda->spath,FAMARC))
             {
              if (switches)
                {prf("\n");
                 prfmsg(QDELETE,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)));
                 usrptr->substt=107;
                }
              else if (strstr(dosvda->spath,"\\*.*"))
                {prfmsg(ALLDELTD);
                 usrptr->substt=103;
                }
              else begin_cycle(3);
             }
           else prfmsg(DIREMPTY);
          }
        else prfmsg(FNOTFND2,param);
       }
    }

  /* ATTRIB */
  else if (sameas(command,"attrib"))
    {
     if (!(dosptr.security&CMD_ATTRIB)) prfmsg(ACCSDEND);
     else
       { dosvda->addflags=dosvda->remflags=0;
        if (*param=='\0') status=s_dir(dspr("%s*.*",dosptr.curdir),0);
        else
          {param=NULL;
           for (i=1;i < margc;i++)
             {if (*margv[i] != '-' && *margv[i] != '+' && *margv[i] != '/')
                {status=s_dir(margv[i],0);
                 if (dosvda->spath[strlen(dosvda->spath)-1] == '\\') s_dir(dosvda->spath,WILDCARD);
                 param=dosvda->spath;
                }
              else if (sameas(margv[i],"-A") && !(dosvda->addflags&FAMARC)) dosvda->remflags|=FAMARC;
              else if (sameas(margv[i],"-H") && !(dosvda->addflags&FAMHID)) dosvda->remflags|=FAMHID;
              else if (sameas(margv[i],"-R") && !(dosvda->addflags&FAMRON)) dosvda->remflags|=FAMRON;
              else if (sameas(margv[i],"-S") && !(dosvda->addflags&FAMSYS)) dosvda->remflags|=FAMSYS;
              else if (sameas(margv[i],"+A") && !(dosvda->remflags&FAMARC)) dosvda->addflags|=FAMARC;
              else if (sameas(margv[i],"+H") && !(dosvda->remflags&FAMHID)) dosvda->addflags|=FAMHID;
              else if (sameas(margv[i],"+R") && !(dosvda->remflags&FAMRON)) dosvda->addflags|=FAMRON;
              else if (sameas(margv[i],"+S") && !(dosvda->remflags&FAMSYS)) dosvda->addflags|=FAMSYS;
              else prfmsg(PARMNCRR,margv[i]);
             }
           if (param == NULL) status=s_dir(dspr("%s*.*",dosptr.curdir),0);
          }

       if (status && fnd1st(&dosfile,dosvda->spath,ALLATTR))
          {
           if (strchr(dosvda->spath,'*') || strchr(dosvda->spath,'?')) dosvda->attr=0;
           else dosvda->attr=1;
           begin_cycle(4);
          }
        else prfmsg(FNOTFND2,param);
       }
    }

  /* TYPE or MORE */
  else if (sameas(command,"type") || sameas(command,"more") || sameas(command,"cat"))
    {if (!(dosptr.security&CMD_TYPE)) prfmsg(ACCSDEND);
     else if (switches && !sameas(swiches,"/#")) prfmsg(INVLSWIT,swiches);
     else if (*param=='\0') prfmsg(REQPARM);
     else if (s_dir(param,0))
       {if ((dosvda->srcfile=fopen(dosvda->spath,FOPRA)) != NULL)
          {if (sameas(swiches,"/#")) dosvda->flags|=LINENUMS;
           if (sameas(command,"more")) dosvda->flags|=MOREMODE;
           begin_cycle(5);
          }
        else prfmsg(FNOTFND2,param);
       }
     else prfmsg(INVLFIL);
    }

  /* GREP */
  else if (sameas(command,"grep"))
    {if (!(dosptr.security&CMD_GREP)) prfmsg(ACCSDEND);
     else if (*param=='\0' || *param2=='\0') prfmsg(REQPARM);
     else if (s_dir(param2,0))
       {
        dosvda->hfnum=0;
        if (switches) dosvda->hfnum=atoi(swiches+1);

        /* (s_dir() above already succeeded; the old code also tested an  */
        /* uninitialized "status" here.)                                   */
        if (fnd1st(&dosfile,dosvda->spath,FAMARC))
          {stzcpy(dosvda->greptext,param,128);
           depad(dosvda->greptext);
           begin_cycle(17);
          }
        else prfmsg(FNOTFND2,param2);
       }
     else prfmsg(INVLFIL);
    }

  /* DOWNLOAD or GET */
  else if (sameas(command,"download") || sameas(command,"get"))
    {if (!(dosptr.security&CMD_GET)) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param == '\0') prfmsg(REQPARM);
     else if (s_dir(param,0))
       {if (!ftgnew()) ftgsbm("");
        else
          {stzcpy(ftgptr->tagspc,param,TSLENG);
           ftgptr->tshndl=filednl;
           ftgptr->flags=FTGABL;
           if (strchr(param,'?') || strchr(param,'*')) ftgptr->flags|=FTGWLD;
           if (ftgsbm(*param2 ? param2:"?"));
           return;
          }
       }
     else prfmsg(FNOTFND2,param);
    }

  /* UPLOAD or PUT */
  else if (sameas(command,"upload") || sameas(command,"put"))
    {if (!(dosptr.security&CMD_PUT)) prfmsg(ACCSDEND);
     else if (*param == '\0') prfmsg(REQPARM);
     else if (s_dir(param,0))
       {if (strlen(param) > 12) param[12]='\0';
        if (sameto(param,"*")) *dosvda->sfile=*dosvda->sext='\0';
        fileup(param,*param2 ? param2:"?",fileupl);
        return;
       }
     else prfmsg(INVLFIL);
    }

  /* EDIT */
  else if (sameas(command,"edit") || sameas(command,"vi"))
    {if (!(dosptr.security&CMD_EDIT)) prfmsg(ACCSDEND);
     else if (DialDos&INEDITOR) prfmsg(EDITINUS);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param == '\0')
       {prfmsg(EDITNAME);
        usrptr->substt=105;
       }
     else if (edit_file(param)) return;
    }

  /* SET */
  else if (sameas(command,"set"))
    {if (*param == '\0')
       {
        param = getenv("COMSPEC");
        prf("COMSPEC=%s\n",param ? param:"");
        param = getenv("PATH");
        prf("PATH=%s\n",param ? param:"");
        param = getenv("PROMPT");
        prf("PROMPT=%s\n",param ? param:"");
        param = getenv("TEMP");
        prf("TEMP=%s\n",param ? param:"");
//      while (environ[i])
//        printf("%s\n",environ[i++]);

       }
    }

  /* LABEL */
  else if (sameas(command,"label"))
    {
     status=s_dir(*param ? param:dspr("%c:",*dosptr.curdir),0);
     if (!(dosptr.security&CANMOVE)) prfmsg(ACCSDEND);
     else if (status && fnd1st(&dosfile,dspr("%s\\*.*",dosvda->sdrive),FAMARC+FAMDIR))
       {
        if (*param) set_label(param);
        else
          {
           if (fnd1st(&dosfile,dspr("%s\\*.*",dosvda->sdrive),FAMVID))
             prfmsg(VOLUME,*dosvda->sdrive,FNAME(dosfile),"serial");
           else prfmsg(VOLUMEN,*dosvda->sdrive,"serial");
           prfmsg(NEWLABEL);
           usrptr->substt=106;
          }
       }
     else prfmsg(INVLDRIV);
    }

  /* REN or RENAME */
  else if (sameas(command,"ren") || sameas(command,"rename"))
    {if (!(dosptr.security&CMD_REN)) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param=='\0' || param2 == '\0') prfmsg(REQPARM);
     else if (strchr(param2,'\\') || strchr(param2,':')) prfmsg(INVLPARM,param2);
     else if (s_dir(param,0) && d_dir(param2,0) &&
              fnd1st(&dosfile,dosvda->spath,FAMARC) && !(FATTR(dosfile)&(FAMHID+FAMDIR))
             )
       {
        stzcpy(dosvda->ddrive,dosvda->sdrive,MAXDRIVE);
        stzcpy(dosvda->ddir,dosvda->sdir,MAXDIR);
        begin_cycle(6);
       }
     else prfmsg(INVLPATH);
    }

  /* COPY */
  else if (sameas(command,"copy") || sameas(command,"cp"))
    {if (!(dosptr.security&CMD_COPY)) prfmsg(ACCSDEND);
     else if (*param=='\0') prfmsg(REQPARM);
     else
       {copy_ok(param,param2,swiches);
       }
     }

  /* XCOPY */
  else if (sameas(command,"xcopy") || sameas(command,"xcp"))
    {if (!(dosptr.security&CMD_XCOPY)) prfmsg(ACCSDEND);
     else if (*param=='\0') prfmsg(REQPARM);
     else
       {dosvda->flags|=XCOPY;
        copy_ok(param,param2,swiches);
       }
     }

  /* MOVE */
  else if (sameas(command,"move") || sameas(command,"mv"))
    {
     if (!(dosptr.security&CMD_MOVE)) prfmsg(ACCSDEND);
     else if (*param=='\0' || *param2=='\0') prfmsg(REQPARM);
     else
       {dosvda->flags|=MOVEMODE;
        if (!copy_ok(param,param2,swiches)) dosvda->flags&=~MOVEMODE;
       }
     }

  /* MEM */
  else if (sameas(command,"mem"))
    {prfmsg(MEMMHDR);

     /* Extended Memory */
     prfmsg(MEMMEXT,"?","?",insrt_comma(sizmem()/1024));

     /* VDA Memory */
     prfmsg(MEMMVDA,
            insrt_comma((LONG)vdasiz),
            insrt_comma((LONG)sizeof(struct vdados)),
            insrt_comma((LONG)vdasiz-sizeof(struct vdados)));
     prfmsg(MEMMFTR);
    }

  /* HISTORY */
  else if (sameas(command,"history"))
    {
     dosvda->fnum=dosptr.doshigh;
     if (dosvda->fnum > 1)
       {dosvda->flags|=MOREMODE;
        begin_cycle(13);
       }
     else prfmsg(NOCMDSRC);
    }

  /* RESET */
  else if (sameas(command,"clear"))
    {
     dosptr.doshigh=0;
     setmem(dosptr.history,2001,0);
     prfmsg(CLEARED);
    }


  /* CHKDSK */
  else if (sameas(command,"chkdsk"))
    {if (!(dosptr.security&CANMOVE)) prfmsg(ACCSDEND);
     else if (!(*param) || (isalpha(*param) && param[1]==':' && param[2]=='\0'))
       {status=s_dir(dspr("%c:\\*.*",*param ? *param:*dosptr.curdir),WILDCARD);

         if (status && fnd1st(&dosfile,dspr("%s\\*.*",dosvda->sdrive),FAMARC+FAMDIR))
           {
            if (fnd1st(&dosfile,dspr("%s\\*.*",dosvda->sdrive),FAMVID))
              prfmsg(CHKDSK1,FNAME(dosfile),prndat(4,FDATE(dosvda->file),'-'),prntim(1,FTIME(dosvda->file)),"serial");
            else prfmsg(CHKDSK1N,"serial");

            fnd1st(&dosfile,dosvda->spath,ALLATTR);
            /* start with an empty directory list, as DIR /S and XCOPY /S */
            /* do; an aborted job can leave entries behind               */
            setmem(dosvda->text,sizeof(dosvda->text),0);
            dosvda->flags|=RECURSE;
            begin_cycle(12);
           }
        else prfmsg(INVLDRIV);
       }
     else prfmsg(PARMNCRR,param);
    }

  /* DELTREE */
  else if (sameas(command,"deltree"))
    {if (!(dosptr.security&CMD_DELTREE)) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param == '\0') prfmsg(REQPARM);
     else if (s_dir(param,MAKEDIR) && fnd1st(&dosfile,dspr("%s*.*",dosvda->spath),FAMARC+FAMDIR)) //nul.txt
       {if (strlen(dosvda->spath) <= 3) prfmsg(NDELTREE);
        else if (sameto(dosvda->spath,dosptr.curdir)) prfmsg(ATMPCUR);
        else
          {dosvda->spath[strlen(dosvda->spath)-1]='\0'; //remove trailing
           d_dir(dosvda->spath,0);
           stzcpy(dosvda->spath,"\\",MAXPATH);
           *dosvda->text='\0';
           usrptr->substt=114;
           prfmsg(DELTREE,strlwr(dspr("%s%s",dosvda->dfile,dosvda->dext)));
          }
       }
     else prfmsg(INVPATH);
    }

  /* SPEED */
  else if (sameas(command,"speed"))
    {if (!(dosptr.security&CMD_SPEED)) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param == '\0')
       {usrptr->substt=115;
        prfmsg(CURSPEED,dosptr.speed);
       }
     else new_speed(atoi(param));
    }


  else if (sameas(command,"edituser"))
    {if (haskey(config.syskey) != 1) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param == '\0')
       {usrptr->substt=117;
        prfmsg(EDITUSR);
       }
     else
       {rstrin();
        edit_user(param);
       }
    }

  else if (sameas(command,"deluser"))
    {if (haskey(config.syskey) != 1) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else if (*param == '\0') prfmsg(REQPARM);
     else
       {dfaSetBlk(dosbt);
        if (dfaAcqGE(NULL,param,0) && sameto(param,dosbuf->userid))
          {stzcpy(dosvda->text,dosbuf->userid,UIDSIZ);
           prfmsg(DELUSER,dosvda->text);
           usrptr->substt=118;
          }
        else prfmsg(USNOTFND,param);
       }
    }

  else if (sameas(command,"users"))
    {if (haskey(config.syskey) != 1) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else
       {dfaSetBlk(dosbt);
        if (dfaQueryGE(emptykey,0))
          {dosvda->space=0;
           begin_cycle(16);
          }
        else prfmsg(NOUSERS);
       }
    }

  /* CONFIG */
  else if (sameas(command,"config"))
    {if (!(usaptr->flags&HASMST)) prfmsg(ACCSDEND);
     else if (switches) prfmsg(INVLSWIT,swiches);
     else
       {fsdroom(CNF10,cfsp,1);  //full page edit session
        sprintf(vdatmp,cfmt,
                config.syskey,'\0',
                (config.flags&ENPASSWD) ? "YES":"NO",'\0',
                config.mspasswd,'\0',
                (config.flags&AUDTXFER) ? "YES":"NO",'\0',
                (config.flags&BATCHPRT) ? "YES":"NO",'\0'
               );
        fsdapr(dosvda->text,8000,vdatmp);
        fsdbkg(fsdrft());
        fsdego(cvfy,cfinish);
       }
    }


  /* COLORS */
  else if (sameas(command,"colors"))
    {if (switches) prfmsg(INVLSWIT,swiches);
     else clroptions();
    }

  /* FORMAT */
  else if (sameas(command,"format"))
    {if ((*param&223)=='C') prf("\nWARNING, ALL DATA ON NON-REMOVABLE DISK\nDRIVE C: WILL BE LOST!\nProceed with Format (Y/N)?y\n\nFormatting...\n\nJust kidding.\n");
     else prf("\nYou wish.\n");
    }

  /* FDISK */
  else if (sameas(command,"fdisk"))
    {if (switches) prfmsg(INVLSWIT,swiches);
     else prf("\nI don't think that would be a good idea.\n");
    }

  /* Look for batch file */
  else
    {
     if (!(dosvda->flags&BATCHFILE) && *command && !strchr(command,'*') &&
         !strchr(command,'?') && s_dir(command,0) &&
         (sameas(dosvda->sext,".bat") || *dosvda->sext=='\0')
        )
       {if (!samend(dosvda->spath,".BAT")) strcat(dosvda->spath,".BAT");
        if ((dosvda->batfile=fopen(dosvda->spath,FOPRA)) != NULL)
          {if (config.flags&BATCHPRT)
            {if (fgets(dosvda->text,128,dosvda->batfile) != NULL && samein("DIALDOS",dosvda->text))
               {dosvda->flags|=BATCHECHO;
                begin_polling(usrnum,batch_poll);
               }
             else
               {prfmsg(BATCHBAD);
                shw_prompt(1);
               }
            }
           else
             {dosvda->flags|=BATCHECHO;
              begin_polling(usrnum,batch_poll);
             }
           return;
          }
       }

     /* COMMAND NOT FOUND */
     prfmsg(BADCMND);
    }

  if (dosvda->flags&SENDNUL)
    {clrprf();
     dosvda->flags&=~SENDNUL;
    }

  if (dosvda->flags&ACCESSDND)
    {clrprf();
     prfmsg(ACCSDEND);
     dosvda->flags&=~ACCESSDND;
     end_cycle();
    }
  else if (usrptr->substt == 100 && !(dosvda->flags&BATCHFILE)) shw_prompt(1);
}



/*
  DATE / TIME entry.  As in the WorldGroup NT builds, a new date or time is
  checked for validity but the server's clock is never changed (the DOS
  builds used to set it).
*/
static INT date_ok(CHAR *datestr)
{
  CHAR *s=datestr;

  while (*s)
    {if (*s == '-') *s='/';
     s++;
    }

  return(dcdate(datestr) != -1);
}


static VOID time_now(VOID)
{
  CHAR pm;
  struct dostime_t curtime;

  _dos_gettime(&curtime);   /* (the NT build never filled this in and showed garbage) */

  pm=(curtime.hour >= 12);
  if (curtime.hour > 12) curtime.hour-=12;
  else if (!curtime.hour) curtime.hour=12;

  prfmsg(NOWTIME,dspr("%d:%02d:%02d.%02d%c",curtime.hour,curtime.minute,curtime.second,curtime.hsecond,pm ? 'p':'a'));
  prfmsg(NEWTIME);
  usrptr->substt=101;
}


static INT time_ok(CHAR *timestr)
{
  if (*timestr == '\0') return(0);
  switch (timestr[strlen(timestr)-1]&223)
    {case 'P':
     case 'A':timestr[strlen(timestr)-1]='\0';
    }

  return(dctime(timestr) != -1);
}

/*
  Begin directory
*/
static INT dir_begin(CHAR *filespec,CHAR *swiches)
{ CHAR *s=swiches;
  CHAR not;

  dosvda->addflags=1;
  dosvda->flags&=~(WIDEMODE+LOWERCASE+BRIEFMODE);

  while (*s)
    {if (*s == '/')
       {s++;
        switch (*s&223)
          {
           case 'S':dosvda->flags|=RECURSE;
                    setmem(dosvda->text,8000,0);//='\0';
                    break;
           case 'P':dosvda->flags|=PAGEMODE;
                    break;
           case 'W':dosvda->flags|=WIDEMODE;
                    break;
           case 'L':dosvda->flags|=LOWERCASE;
                    break;
           case 'B':dosvda->flags&=~WIDEMODE;
                    dosvda->flags|=BRIEFMODE;
                    break;
           case 'A':s++;
                    if (*s == '-' ) {not=1;s++;} //Takes care of /a- paramters
                    else not=0;
                    switch(*s&223)
                      {case 'H':dosvda->attr|=(not) ? ~FAMHID:FAMHID;
                                break;
                       case 'D':dosvda->attr|=(not) ? ~FAMDIR:FAMDIR;
                                break;
                       case 'A':dosvda->attr|=(not) ? ~FAMARC:FAMARC;
                                break;
                       case 'R':dosvda->attr|=(not) ? ~FAMRON:FAMRON;
                                break;
                       case 'S':dosvda->attr|=(not) ? ~FAMSYS:FAMSYS;
                                break;
                       default:prfmsg(INVLSWIT,dspr("/%s%c",(not) ? "A-":"A",*s));
                               return(0);
                      }
                    break;

             default:prfmsg(INVLSWIT,dspr("/%c",*s));
                     return(0);
          }
       }
     else if (*s != ' ')
       {prfmsg(TOOMNYPR,s);
        return(0);
       }
     s++;
    }

  /* A path s_dir() refuses leaves spath holding the previous command's   */
  /* path, which must not be listed instead.                              */
  if (!s_dir(filespec,WILDCARD) && !(dosvda->flags&ACCESSDND))
    {prfmsg(INVLPATH);
     return(0);
    }

  //security
  if (!(dosptr.security&CANMOVE) && ((*dosptr.homedir&223) != (*dosvda->spath)))
    {dosvda->flags|=ACCESSDND;
     return(0);
    }


  if (!(dosvda->flags&BRIEFMODE))
    {if (fnd1st(&dosfile,dspr("%c:*.*",*dosvda->spath),FAMVID))
       prfmsg(DIRHDR,*dosvda->spath,FNAME(dosfile),"serial",dosvda->spath);
     else
       prfmsg(DIRHDRN,*dosvda->spath,"serial",dosvda->spath);
    }

  if (dosvda->attr == 0) dosvda->attr=FAMARC+FAMDIR;

  if (fnd1st(&dosfile,dosvda->spath,ALLATTR)) //63 == all attr's
    {
     dosvda->flags&=~NOTFOUND;
     //In the case user does dir <dirname>
     if (FATTR(dosfile)&FAMDIR && !strchr(dosvda->spath,'*') && !strchr(dosvda->spath,'?'))
       {clrprf();
        strcat(dosvda->spath,"\\*.*");
        s_dir(dosvda->spath,0);
        if (!(dosvda->flags&BRIEFMODE))
          {if (fnd1st(&dosfile,dspr("%c:*.*",*dosvda->spath),FAMVID))
             prfmsg(DIRHDR,*dosvda->spath,FNAME(dosfile),"serial",dosvda->spath);
           else
             prfmsg(DIRHDRN,*dosvda->spath,"serial",dosvda->spath);
          }
        if (fnd1st(&dosfile,dosvda->spath,FAMARC+FAMDIR+FAMRON+FAMHID+FAMSYS)) dosvda->flags&=~NOTFOUND;
        else dosvda->flags|=NOTFOUND;
       }
    }
  else dosvda->flags|=NOTFOUND;

  if (!(dosptr.security&CANMOVE) && !inhome(dosvda->spath))
    {dosvda->flags|=ACCESSDND;
     return(0);
    }

  begin_cycle(2);
  dosvda->line=5; //5 lines outputted
  return(1);
}


static VOID dir_show(VOID)
{ CHAR *ext,*Color,c;
  CHAR fname[9],fsize[20];

  if (FATTR(dosfile)&FAMHID && dosvda->attr==FAMDIR+FAMARC) return;
  if (FATTR(dosfile)&(dosvda->attr) || (dosvda->attr==FAMDIR+FAMARC && !(FATTR(dosfile)&FAMHID)))
    {
     if (dosvda->flags&LOWERCASE) strlwr(FNAME(dosfile));
     dosvda->line++; //increase line count on successful finds

     //Pulls out extension
     Color=colors[7];
     if ((ext=strchr(FNAME(dosfile),'.')) == FNAME(dosfile)) ext=NULL;
     if (ext != NULL)
       {*ext='\0';
        stzcpy(fname,FNAME(dosfile),9);
        *ext='.';
        ext++;
        for (c=0; c < 20; c++)
          {if (*(&dosptr.ext[c*4]) && sameas(&dosptr.ext[c*4],ext))
             {Color=colors[dosptr.extclr[c]];
              break;
             }
          }
       }
     else stzcpy(fname,FNAME(dosfile),9);


     if (FATTR(dosvda->file)&FAMDIR)
       {Color=colors[dosptr.dirclr];
        dosvda->dnum++;
        if (dosvda->flags&BRIEFMODE) prf("%s%s\n",Color,FNAME(dosfile));
        else if (dosvda->flags&WIDEMODE)
          {/* pad to a 15-column cell; long names get at least one space */
           stzcpy(fsize,"               ",strlen(FNAME(dosfile)) < 13 ? 14-(INT)strlen(FNAME(dosfile)) : 2);
           prf("%s[%s]%s",Color,FNAME(dosfile),fsize);
           dosvda->addflags=(dosvda->addflags%5)+1;
           if (dosvda->addflags==1) prf("\n");
           else dosvda->line--;
          }
        else prfmsg(DIRDLST1,Color,fname,(ext) ? ext : "","<DIR>",prndat(4,FDATE(dosvda->file),'-'),prntim(1,FTIME(dosvda->file)));
       }
     else
       {dosvda->fnum++;
        dosvda->space+=FSIZEU(dosfile);
        if (dosvda->flags&BRIEFMODE) prf("%s%s\n",Color,FNAME(dosfile));
        else if (dosvda->flags&WIDEMODE)
          {
           prf("%s%-15s",Color,FNAME(dosfile));
           dosvda->addflags=(dosvda->addflags%5)+1;
           if (dosvda->addflags==1) prf("\n");
           else dosvda->line--;
          }
        else prfmsg(DIRFLST1,Color,fname,(ext) ? ext : "",insrt_comma(FSIZEU(dosfile)),prndat(4,FDATE(dosvda->file),'-'),prntim(1,FTIME(dosvda->file)));
       }
    }
}


/*
  Inserts commas into all integers
*/
static CHAR *insrt_comma(WULONG number)
{
  CHAR raw[24],dst[32];
  INT len,i,j;

  sprintf(raw,"%I64u",number);
  len=(INT)strlen(raw);
  for (i=0,j=0; i < len; i++)
    {/* a comma before every group of three digits, counted from the right */
     if (i > 0 && (len-i)%3 == 0) dst[j++]=',';
     dst[j++]=raw[i];
    }
  dst[j]='\0';
  return(dspr("%s",dst));
}


/*
  Is a path inside this user's home directory?  (Users without CANMOVE
  are kept there.)  The sysop may type the home directory with or without
  a trailing backslash, so this checks for a whole path element; a plain
  prefix test let home C:\USERS\BOB reach C:\USERS\BOBBY\.
*/
static INT inhome(const CHAR *path)
{
  return(fns_under(dosptr.homedir,path));
}


/*
  Makes sure that a directory is correctly specified with a trailing \
*/
static VOID mdir(CHAR *path)
{ INT i;

  if (path[(i=strlen(path))-1] != '\\')
    {path[i++]='\\';
     path[i]='\0';
    }
}


/*
  Status handler input
*/
static VOID begin_cycle(INT newstt)
{
  usrptr->substt=newstt;
  if (!(usrptr->flags&NOINJO))  //If user not busy, set busy but restore
    {usrptr->flags|=NOINJO;     //when done
     dosvda->flags|=NOTBUSY;
    }
  //In case of dir *.msg /s, will recurse properly
  if (dosvda->flags&RECURSE && !samein("\\*.*",dosvda->spath)) dosvda->flags|=NOWILD;
  btuinj(usrnum,CYCLE);
}


/*
  Status handler exit
*/
static VOID end_cycle(VOID)
{
  if (dosvda->srcfile != NULL) fclose(dosvda->srcfile);
  if (dosvda->destfile != NULL) fclose(dosvda->destfile);
  dosvda->srcfile=NULL;
  dosvda->destfile=NULL;
  if (dosvda->flags&NOTBUSY) usrptr->flags&=~NOINJO;
  dosvda->flags&=~(NOTFOUND+PAUSED+MOREMODE+PAGEMODE+MOVEMODE+ABORTOP+RECURSE+XCOPY+NOWILD+NOTBUSY+GREPMODE+LINENUMS+INCYCLE);
  shw_prompt(1);
}

/*
  Download handler routine
*/
static INT filednl(INT status_code)
{
  INT rc=0;
  FILE *fp;

  setmbk(dosmb);
  switch(status_code)
    {
     case TSHDSC: sprintf(tshmsg,"file %s",ftgptr->tagspc);
                  break;
     case TSHVIS: if (ftgptr->flags&FTGWLD)
                    {
                     if (!fnd1st(&ftuptr->fb,ftgptr->tagspc,0))
                       shw_prompt(1);
                     break;
                    }
                  if ((fp=fopen(dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,ftgptr->tagspc),FOPRB)) != NULL)
                    {tshmsg[fread(tshmsg,1,TSHLEN,fp)]='\0';
                     rc=1;
                     fclose(fp);
                    }
                  break;
     /* tshmsg (81 bytes) and ftfscb->fname (13 bytes) are engine buffers: */
     /* every copy into them is cut to fit.                                 */
     case TSHSCN: if (fnd1st(&ftuptr->fb,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,ftgptr->tagspc),0))
                    {stzcpy(tshmsg,FNAME(ftuptr->fb),TSHLEN+1);
                     rc=1;
                    }
                  break;
     case TSHNXT: if (fndnxt(&ftuptr->fb))
                    {stzcpy(tshmsg,FNAME(ftuptr->fb),TSHLEN+1);
                     rc=1;
                    }
                  break;
     case TSHBEG: stzcpy(tshmsg,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,ftgptr->tagspc),TSHLEN+1);
                  stzcpy(ftfscb->fname,ftgptr->tagspc,sizeof(ftfscb->fname));
                  rc=1;
                  break;
     case TSHFIN: if (config.flags&AUDTXFER) shocst("User downloaded file(s).","%s dl'd: %s",usaptr->userid,dosvda->spath);
                  prf("\n");
                  shw_prompt(1);
                  rc=1;
                  break;
    }
  return(rc);
}


/*
  Upload handler routine
*/
static INT fileupl(INT fupcod)
{
  INT rc = 0;

  setmbk(dosmb);
  switch(fupcod)
    {/* Beginning upload */
     case FUPBEG: if (*dosvda->sfile == '\0' && rsvnam(ftfscb->fname))
                    {strcpy(ftfbuf,"Filename is a reserved DOS device name.");
                     break;
                    }
                  strcpy(ftfbuf,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,
                    (*dosvda->sfile) ? dspr("%s%s",dosvda->sfile,dosvda->sext) : ftfscb->fname));
                  rc=1;
                  break;
     /* End of upload */
     case FUPEND: strcpy(ftfbuf,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,
                    (*dosvda->sfile) ? dspr("%s%s",dosvda->sfile,dosvda->sext) : ftfscb->fname));
                  break;
     /* Upload skipped */
     case FUPSKP: unlink(dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,
                    (*dosvda->sfile) ? dspr("%s%s",dosvda->sfile,dosvda->sext) : ftfscb->fname));
                  break;
     /* Restore module */
     case FUPFIN: //if (ftfscb->actfil >= 1);// prfmsg(UPLOK);
                  if (config.flags&AUDTXFER) shocst("User uploaded file(s).","%s ul'd: %s",usaptr->userid,dosvda->spath);
                  rc=1;
                  prf("\n");
                  shw_prompt(1);
                  break;
    }
  return(rc);
}

/*
  File editor kicker
*/
static INT edit_file(CHAR *filename)
{ INT i;
  INT lines;

 if (s_dir(filename,0) && !strchr(dosvda->spath,'?') && !strchr(dosvda->spath,'*'))
    {i=0;
     strcpy(dosvda->topic,dspr("%s%s",dosvda->sfile,dosvda->sext));
     if ((dosvda->srcfile=fopen(dosvda->spath,FOPRA)) != NULL)
       {INT bad,more;

        /* Stop at 32,000 bytes, then check whether any of the file is    */
        /* left: a file that does not fit must be refused, because saving */
        /* a cut-down copy would overwrite the rest of it.  (The old loop */
        /* stopped silently, and tested ferror() after fclose().)         */
        lines=0;
        while (i < 32000 && fgets(editorbuffer+i,256,dosvda->srcfile) != NULL)
          {i+=(INT)strlen(editorbuffer+i);
           lines++;
          }
        bad=ferror(dosvda->srcfile);
        more=(!bad && fgetc(dosvda->srcfile) != EOF);
        fclose(dosvda->srcfile);
        dosvda->srcfile=NULL;
        if (bad)
          {prfmsg(FILEBAD);
           return(0); /* File error */
          }
        else if (more || i > 32081 || lines > 400)
          {prfmsg(OUTMEM);
           return(0);  /* File too large for editor */
          }

       }
     //bgnedt(8001,editorbuffer,13,dosvda->topic,save_file,i ? 0 : ED_CLRTXT);
     //extern INT (* WGSEXP bgnedt)(INT,CHAR *,INT,CHAR *,SHORT (*)(SHORT),INT);
     bgnedt(32081,editorbuffer,13,dosvda->topic,save_file,i ? 0 : ED_CLRTXT);
     DialDos|=INEDITOR;
     editowner=usrnum;
     return(1);
    }
  prfmsg(INVLFIL);
  return(0);
}


/*
  Full Screen Editor finish routine
*/
static FINRET save_file(FINRET quit)
{
  CHAR *s;

  setmbk(dosmb);
  if (!quit)
    {for (s=editorbuffer; *s ; s++) if (*s=='\r') *s='\n';
     if ((dosvda->srcfile=fopen(dosvda->spath,FOPWA)) == NULL) prfmsg(FILEBAD);
     else
       {fprintf(dosvda->srcfile,"%s\n",&editorbuffer[1]);
        fclose(dosvda->srcfile);
        dosvda->srcfile=NULL;
       }
    }
  btuchi(usrnum,chrprc);                 //Character input routine
  btupbc(usrnum,0);
  shw_prompt(1);
  outprf(usrnum);
  DialDos&=~INEDITOR;
  editowner=-1;
  return(1);
}


static INT set_label(CHAR *newlabel)
{ 
  if (strchr(newlabel,'?') || strchr(newlabel,'*') || strchr(newlabel,'?') ||
      strchr(newlabel,'<') || strchr(newlabel,'>') || strchr(newlabel,'^') ||
      strchr(newlabel,'&') || strchr(newlabel,'\\')|| strchr(newlabel,'|')
     )
    {prfmsg(INVLCHAR);
     if (fnd1st(&dosfile,dspr("%s\\*.*",dosvda->sdrive),FAMVID))
       prfmsg(VOLUME,*dosvda->sdrive,FNAME(dosfile),"serial");
     else prfmsg(VOLUMEN,*dosvda->sdrive,"serial");

     prfmsg(NEWLABEL);
     usrptr->substt=106;
     return(0);
    }
 //    set label here
  return(1);
}



/*
  Parses and creates Source Directory
*/
static INT s_dir(CHAR *path,CHAR flags)
{
  CHAR dir[MAXDIR];
  CHAR tmpdir[MAXDIR*2];
  INT nameflags;
  CHAR *c,*s;


  if (strstr(path,"\\\\") || strstr(path,"...") || strstr(path,". ") ||
      (path[(nameflags=strlen(path))-1]=='\\' && nameflags >= 2 && path[nameflags-2] !=':')
     ) return(0);

  strupr(path);
  *dosvda->sdir='\\';
  if (flags&MAKEDIR) mdir(path);
  nameflags=fnsplit(path,dosvda->sdrive,dir,dosvda->sfile,dosvda->sext);
  /* a long name cut to 8.3 would name a different file: refuse it */
  if (!fns_whole(path,dosvda->sdrive,dir,dosvda->sfile,dosvda->sext)) return(0);

  if (!(nameflags&DRIVE)) stzcpy(dosvda->sdrive,dspr("%c:",*dosptr.curdir),MAXDRIVE);

  if (!(nameflags&DIRECTORY)) *dir='\0'; //just to be safe..
  if (*dir=='\\') stzcpy(tmpdir,dir,MAXDIR);
  else
    {
     //should pull current directory off of current drive
     stzcpy(tmpdir,GETDIR(*dosvda->sdrive),MAXDIR);
     if (!(nameflags&DIRECTORY) && !(nameflags&FILENAME) && !(nameflags&EXTENSION) && tmpdir[1])
       tmpdir[strlen(tmpdir)-1]='\0';
     else
       strcat(tmpdir,dir);
    }

  if (!strchr(dir,'.')) stzcpy(dosvda->sdir,tmpdir,MAXDIR);
  else
    {/* this takes care of .\ and \. directory entries */
     s=tmpdir;
     do
       {s++;
        if (*s == '.' && *(s-1) != '.' && *(s+1) != '.')
          {
           if (*(s+1)== '\\')
             {stzcpy(s,s+2,strlen(s+2)+1);
              s--;
             }
           else if (*(s-1)=='\\')
             {stzcpy(s-1,s+1,strlen(s+1)+1);
              s-=2;
             }
          }
       } while (*s);
     if (DialDos&DEBUGMODE) prf("\ntmpdir=%s",tmpdir);

     /* this takes care of all .. directories */
     while ((s=c=strstr(tmpdir,"..")) != NULL)
       {c+=2;
        s--;
        if (sameto("\\..",tmpdir)) return(0);
        if (*s == '\\' && s != tmpdir) for(s--; s != tmpdir && *s !='\\'; s--); //recurse to previous dir
        stzcpy(s,c,strlen(c)+1);
       }
     stzcpy(dosvda->sdir,tmpdir,MAXDIR);
    }
  if (DialDos&DEBUGMODE) prf("\nsdir=%s",dosvda->sdir);


  if (!(nameflags&FILENAME))
    {if (flags&WILDCARD)
       {mdir(dosvda->sdir);
        stzcpy(dosvda->sfile,"*",MAXFILE);
       }
     else *dosvda->sfile='\0';
    }

  if (!(nameflags&EXTENSION))
    {if (flags&WILDCARD && !(nameflags&FILENAME)) stzcpy(dosvda->sext,".*",MAXEXT); //
     else *dosvda->sext='\0';
    }

  if (*dosvda->sdir=='\0') stzcpy(dosvda->sdir,"\\",MAXDIR);

  sprintf(dosvda->spath,"%s%s%s%s",dosvda->sdrive,dosvda->sdir,dosvda->sfile,dosvda->sext);
  if (DialDos&DEBUGMODE) prf("\nfinal path=%s\n",dosvda->spath);

  //security
  if (!(dosptr.security&CANMOVE) && !inhome(dosvda->spath))
    {dosvda->flags|=ACCESSDND;
     return(0);
    }

  if (*fnroot(dosvda->spath) !='\0' && rsvnam(dosvda->spath)) return(0);
  return(1);
}


/*
  Parses and creates Destination Directory
*/
static INT d_dir(CHAR *path,CHAR flags)
{
  CHAR dir[MAXDIR];
  CHAR tmpdir[MAXDIR*2];
  INT nameflags;
  CHAR *c,*s;

  if (strstr(path,"\\\\") || strstr(path,"...") || strstr(path,". ") ||
      (path[(nameflags=strlen(path))-1]=='\\' && nameflags >= 2 && path[nameflags-2] !=':')
     ) return(0);

  strupr(path);
  *dosvda->ddir='\\';
  if (flags&MAKEDIR) mdir(path);
  nameflags=fnsplit(path,dosvda->ddrive,dir,dosvda->dfile,dosvda->dext);
  /* a long name cut to 8.3 would name a different file: refuse it */
  if (!fns_whole(path,dosvda->ddrive,dir,dosvda->dfile,dosvda->dext)) return(0);

  if (!(nameflags&DRIVE)) stzcpy(dosvda->ddrive,dspr("%c:",*dosptr.curdir),MAXDRIVE);

  if (!(nameflags&DIRECTORY)) *dir='\0'; //just to be safe..
  if (*dir=='\\') stzcpy(tmpdir,dir,MAXDIR);
  else
    {
     //should pull current directory off of current drive
     stzcpy(tmpdir,GETDIR(*dosvda->ddrive),MAXDIR);
     if (!(nameflags&DIRECTORY) && !(nameflags&FILENAME) && !(nameflags&EXTENSION) && tmpdir[1])
       tmpdir[strlen(tmpdir)-1]='\0';
     else
       strcat(tmpdir,dir);
    }

  if (!strchr(dir,'.')) stzcpy(dosvda->ddir,tmpdir,MAXDIR);
  else
    {/* this takes care of .\ and \. directory entries */
     s=tmpdir;
     do
       {s++;
        if (*s == '.' && *(s-1) != '.' && *(s+1) != '.')
          {
           if (*(s+1)== '\\')
             {stzcpy(s,s+2,strlen(s+2)+1);
              s--;
             }
           else if (*(s-1)=='\\')
             {stzcpy(s-1,s+1,strlen(s+1)+1);
              s-=2;
             }
          }
       } while (*s);
     if (DialDos&DEBUGMODE) prf("\ntmpdir=%s",tmpdir);

     /* this takes care of all .. directories */
     while ((s=c=strstr(tmpdir,"..")) != NULL)
       {c+=2;
        s--;
        if (sameto("\\..",tmpdir)) return(0);
        if (*s == '\\' && s != tmpdir) for(s--; s != tmpdir && *s !='\\'; s--); //recurse to previous dir
        stzcpy(s,c,strlen(c)+1);
       }
     stzcpy(dosvda->ddir,tmpdir,MAXDIR);
    }
  if (DialDos&DEBUGMODE) prf("\nddir=%s",dosvda->ddir);


  if (!(nameflags&FILENAME))
    {if (flags&WILDCARD)
       {mdir(dosvda->ddir);
        stzcpy(dosvda->dfile,"*",MAXFILE);
       }
     else *dosvda->dfile='\0';
    }

  if (!(nameflags&EXTENSION))
    {if (flags&WILDCARD && !(nameflags&FILENAME)) stzcpy(dosvda->dext,".*",MAXEXT);
     else *dosvda->dext='\0';
    }


  if (*dosvda->ddir=='\0') stzcpy(dosvda->ddir,"\\",MAXDIR);

  /*
  //security
  if (!(dosptr.security&CANMOVE) && sameas(dosvda->ddir,"\\"))
    {stzcpy(dosvda->ddir,&dosptr.homedir[2],MAXDIR);
     if (dspr("%s%s",dosvda->dfile,dosvda->dext) == '\0') dosvda->ddir[strlen(dosvda->ddir)-1]='\0';
    }
  */

  sprintf(dosvda->dpath,"%s%s%s%s",dosvda->ddrive,dosvda->ddir,dosvda->dfile,dosvda->dext);
  if (DialDos&DEBUGMODE) prf("\nfinal path=%s\n",dosvda->dpath);

  //security
  if (!(dosptr.security&CANMOVE) && !inhome(dosvda->dpath))
    {dosvda->flags|=ACCESSDND;
     return(0);
    }

  if (*fnroot(dosvda->dpath) != '\0' && rsvnam(dosvda->dpath)) return(0);
  return(1);
}


/*
  Launches COPY, XCOPY, and MOVE
*/
static INT copy_ok(CHAR *file1,CHAR *file2,CHAR *swiches)
{ INT status;
  CHAR *s;

  dosvda->flags&=~(OVERALL+OVERFILE);

  s=swiches;
  while (*s)
    {if (*s == '/')
       {s++;
        switch (*s&223)
          {
           case 'S':if (!(dosvda->flags&XCOPY))
                      {prfmsg(INVLSWIT,dspr("/%c",*s));
                       return(0);
                      }
                    dosvda->flags|=RECURSE;
                    setmem(dosvda->text,10000,0);//='\0';
                    break;
           case 'Y':dosvda->flags|=OVERALL;
                    break;
           case 'E':
                    break;

            default:prfmsg(INVLSWIT,dspr("/%c",*s));
                    return(0);
          }
       }
     else if (*s != ' ')
       {prfmsg(TOOMNYPR,s);
        return(0);
       }
     s++;
    }


  /* Check destination */
  if (d_dir(file2,0))
    {
     //root directory
     if (dosvda->dpath[strlen(dosvda->dpath)-1] == '\\') d_dir(dosvda->dpath,WILDCARD);
     else
       {
        if (dosvda->flags&XCOPY) //xcopy
          {
           if (fnd1st(&dosfile,dosvda->dpath,ALLATTR))
             {if (!(FATTR(dosfile)&FAMDIR))
               {prfmsg(CNTCRTDS,dosvda->dpath);
                return(0);
               }
             }
           else
             {
              if (mkdir(dosvda->dpath))
                {prfmsg(UNABLEDI);
                 return(0);
                }
             }
          }

        if (*file2 == '\0' ||
            (fnd1st(&dosfile,dosvda->dpath,FAMDIR) &&
            (FATTR(dosfile)&FAMDIR || *FNAME(dosfile)=='.'))
           )
          {mdir(dosvda->dpath);
           strcat(dosvda->dpath,"*.*");
           d_dir(dosvda->dpath,0);
          }

        //Attempt to move to a non direcory check
        if (dosvda->flags&MOVEMODE && !(fnd1st(&dosfile,dosvda->dpath,ALLATTR) && FATTR(dosfile)&FAMDIR))
          {prfmsg(PNOTFND2,dosvda->dpath);
           return(0);
           }
       } //
    }
  else
    {prfmsg(dosvda->flags&MOVEMODE ? INVLDIR:CNTCRTDS,dosvda->dpath);
     return(0);
    }

  status=s_dir(file1,0);
  if (dosvda->spath[strlen(dosvda->spath)-1] == '\\') s_dir(dosvda->spath,WILDCARD);

  /* Check source file */
  if (!status || !fnd1st(&dosfile,dosvda->spath,ALLATTR))  //FAMARC+FAMDIR
    {prfmsg(dosvda->flags&MOVEMODE ? CNTMOVE:FNOTFND2,file1);
     return(0);
    }

  /* Check to see if source is a directory */
  if (FATTR(dosfile)&FAMDIR || *FNAME(dosfile)=='.' || dosvda->spath[strlen(dosvda->spath)-1] == '\\')
    {
     if (!strchr(dosvda->spath,'*') && !strchr(dosvda->spath,'?'))
       {
        mdir(dosvda->spath);
        strcat(dosvda->spath,"*.*");
        s_dir(dosvda->spath,0);
       }

     if (!fnd1st(&dosfile,dosvda->spath,ALLATTR) && !(dosvda->flags&XCOPY))  //FAMARC
       {prfmsg(DIREMPTY);
        return(0);
       }
    }

  stzcpy(dosvda->srcdir,dosvda->sdir,MAXDIR);
  stzcpy(dosvda->destdir,dosvda->ddir,MAXDIR);
  begin_cycle(7);
  return(1);
}


/*
  Does wildcard name handling
*/
static GBOOL file_names(VOID)
{ /* Windows file names can be up to 259 characters, so the work buffer   */
  /* is sized for that and every write into it is bounded.  (It used to   */
  /* be 13 bytes - room for an 8.3 name only.)                            */
  CHAR filename[GCMAXPTH];
  INT c,d;
  CHAR *ext;
  const INT room=GCMAXPTH-5;   /* leave space for ".EXT" and the NUL      */

  c=0;
  setmem(filename,sizeof(filename),0);

  ext=FNAME(dosfile);
  while (*ext && *ext !='.') ext++;

  while (dosvda->dfile[c] && c < room)
    {
     if (dosvda->dfile[c] == '*')
       {while (c < room && &FNAME(dosfile)[c] < ext && FNAME(dosfile)[c] != '.')
          {filename[c]=FNAME(dosfile)[c];
           c++;
          }
        break;
       }
     else if (dosvda->dfile[c] == '?') filename[c]=FNAME(dosfile)[c];
     else if (dosvda->dfile[c] == '.') break;
     else filename[c]=dosvda->dfile[c];
     c++;
    }

  d=0;
  while (d < 4 && dosvda->dext[d])
    {
     if (dosvda->dext[d] == '*')
       {while (d < 4 && ext[d] && *ext)
          {filename[c+d]=ext[d];
           d++;
          }
        break;
       }
     else if (dosvda->dext[d] == '?') filename[c+d]=ext[d];
     else filename[c+d]=dosvda->dext[d];
     d++;
    }

  /* spath/dpath hold MAXPATH (80) characters.  A longer path cannot be  */
  /* used: cut short, it names a different file, and COPY or REN would    */
  /* create that file (and MOVE then delete the source).  Return FALSE so */
  /* the caller reports it instead.                                       */
  if (strlen(dosvda->sdrive)+strlen(dosvda->sdir)+strlen(FNAME(dosfile)) > MAXPATH-1
   || strlen(dosvda->ddrive)+strlen(dosvda->ddir)+strlen(filename) > MAXPATH-1)
    return(FALSE);
  stzcpy(dosvda->spath,dspr("%s%s%s",dosvda->sdrive,dosvda->sdir,FNAME(dosfile)),MAXPATH);
  stzcpy(dosvda->dpath,dspr("%s%s%s",dosvda->ddrive,dosvda->ddir,filename),MAXPATH);
  if (DialDos&DEBUGMODE)
    {prf("\nFile spath=%s",dosvda->spath);
     prf("\nFile dpath=%s\n",dosvda->dpath);
    }
  return(TRUE);
}


/*
  Batch file polling routine
*/
static VOID batch_poll(VOID)
{ CHAR line[128];

  setmbk(dosmb);
  if (usrptr->substt != 100) return;
  else if (fgets(line,128,dosvda->batfile) != NULL)
    {
     line[strlen(line)-1]='\0';
     if (*line == '@') stzcpy(input,line+1,127);
     else
       {stzcpy(input,line,128);
        if (dosvda->flags&BATCHECHO)
          {shw_prompt(1);
           prf("%s\n",line);
          }
       }
     dosvda->flags|=BATCHFILE;
     parsin();
     if (!margc)
       {if (dosvda->flags&BATCHECHO) shw_prompt(0);}
     else exec_cmd();
    }
  else
    {fclose(dosvda->batfile);
     dosvda->flags&=~BATCHFILE;
     stop_polling(usrnum);
     shw_prompt(1);
    }
  outprf(usrnum);
}


/*
  Recurses subdirectories
*/
static INT recurse_dir(VOID)
{
  if (dosvda->flags&RECURSE)
    {if (dosvda->flags&NOWILD && !fnd1st(&dosfile,dspr("%s%s*.*",dosvda->sdrive,dosvda->sdir),ALLATTR)) return(0);
     dosvda->curop=usrptr->substt;
     usrptr->substt=11;
     return(1);
    }
  return(0);
}

/*
  CHKDSK Info
*/
static VOID chkdsk_ftr(VOID)
{ 
  CHAR temp[15];
  struct fatinfo diskinfo;

  /* get drive information */
  getfat(*dosvda->spath - 'A' + 1, &diskinfo);
  
  stzcpy(temp, insrt_comma((WLONG)dosvda->dnum), 15); //directory files

  prfmsg(CHKDSK2,
         insrt_comma((WLONG)diskinfo.fi_bysec * (WLONG)diskinfo.fi_sclus * (WLONG)diskinfo.fi_nclus),  //total disk space
         insrt_comma(dosvda->hspace),//hidden space
         insrt_comma((WLONG)dosvda->hfnum),//hidden files
         insrt_comma((WLONG)diskinfo.fi_bysec * (WLONG)diskinfo.fi_sclus * (WLONG)dosvda->dnum),//directory space
         temp);

  prfmsg(CHKDSK3,
         insrt_comma(dosvda->space),//files space
         insrt_comma((WLONG)dosvda->fnum),//files
         insrt_comma(dskfrebytes(*dosvda->spath)));

  /* (If the drive geometry could not be read the cluster size is 0;      */
  /* report 0 free clusters rather than divide by zero.)                  */
  prfmsg(CHKDSKF,
         insrt_comma((WLONG)diskinfo.fi_bysec * (WLONG)diskinfo.fi_sclus),//bytes / allocation unit
         insrt_comma((WLONG)diskinfo.fi_nclus),//total allocation units
         insrt_comma((diskinfo.fi_bysec*diskinfo.fi_sclus) == 0 ? 0 :
                     dskfrebytes(*dosvda->spath)/((WULONG)diskinfo.fi_bysec * (WULONG)diskinfo.fi_sclus)));//available allocation units
}


static VOID new_speed(INT i)
{
  if (i > 0 && i <= 200)
    {dosptr.speed=i;
     shw_prompt(1);
    }
  else
    {usrptr->substt=115;
     prfmsg(INVLSPD);
     prfmsg(CURSPEED,dosptr.speed);
    }
}


static VOID edit_user(CHAR *userid)
{ INT found;

  dfaSetBlk(dosbt);
  if (sameas("new",userid))
    {
     found=0;
     stzcpy(dosbuf->userid,"NEW",UIDSIZ);
     stzcpy(dosbuf->homedir,"C:\\",50);
     *dosbuf->password='\0';
     dosbuf->security=0;
    }
  else if (dfaAcqGE(NULL,userid,0) && sameto(userid,dosbuf->userid))
    {
     found=1;
    }
  else
    {prfmsg(USNOTFND,userid);
     usrptr->substt=100;
     return;
    }

  fsdroom(CNF30,dosfsp,1); //full page edit session
  sprintf(vdatmp,dosfmt,dosbuf->userid,'\0',
                        dosbuf->password,'\0',
		        dosbuf->homedir,'\0',
		        (dosbuf->security&CANMOVE) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_TYPE) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_EDIT) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_DATE) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_TIME) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_ATTRIB) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_REN) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_COPY) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_XCOPY) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_MOVE) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_MD) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_RD) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_GET) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_PUT) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_DEL) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_DELTREE) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_SPEED) ? "YES":"NO",'\0',
		        (dosbuf->security&CMD_GREP) ? "YES":"NO",'\0'
                        );

  fsdapr(dosvda->text,4096,vdatmp);
  fsdbkg(fsdrft());
  if (found) fsdscb->flddat[USERIDFLD].flags|=FFFAVD;
  fsdego(dosvfy,finish);
}


static INT dosvfy(INT fldno,CHAR *answer)
{
  struct usracc *accptr; //=NULL;
  INT i;

  switch (fldno)
    {case USERIDFLD: dfaSetBlk(accbb);
                     if (dfaQueryGE(answer,0))
                       {dfaAbsRec(vdatmp,0);
                        accptr=(struct usracc *)vdatmp;
                        stzcpy(answer,accptr->userid,UIDSIZ);
                        dfaSetBlk(dosbt);
                        if (dfaAcqEQ(vdatmp,answer,0))
                          {strcpy(fsdemg,"That UserId already has an account!");
                           return(VFYREJ);
                          }
                        return(vfyadn(fldno,answer));
                       }
                     else
                       {strcpy(fsdemg,"UserID not found!");
                        return(VFYREJ);
                       }

    case HOMEDIRFLD: stzcpy(dosvda->spath,strupr(answer),50);
                     if (answer[1] != ':' || answer[2] != '\\' ||
                         !s_dir(dosvda->spath,MAKEDIR))
                       {strcpy(fsdemg,"Invalid path!  Example: C:\\USERS\\RICHIE\\");
                        return(VFYREJ);
                       }

                     if (fnd1st(&dosfile,dspr("%s*.*",dosvda->spath),FAMDIR))
                       {if ((i=strlen(dosvda->spath)) != 3) dosvda->spath[--i]='\0';
                        return(vfyadn(fldno,dosvda->spath));
                       }
                     else
                       {strcpy(fsdemg,"Path not found!");
                        return(VFYREJ);
                       }

            default: return(vfyadn(fldno,answer));
    }
}


/*
  Finish routine
*/
static VOID finish(FINRET save)
{
  setmbk(dosmb);
  dfaSetBlk(dosbt);
  if (!(fsdscb->chgcnt) || !save) prfmsg(USERNSAV);
  else
    {
     fsdfxt(USERIDFLD,dosbuf->userid,UIDSIZ);

     if (sameas(dosbuf->userid,"new")) prfmsg(USERNGUD);
     else
       {if (!dfaAcqEQ(NULL,dosbuf->userid,0))
          {//null out if new user
           setmem(dosbuf,sizeof(struct dosrec),0);
           fsdfxt(USERIDFLD,dosbuf->userid,UIDSIZ);
          }
        fsdfxt(HOMEDIRFLD,dosbuf->homedir,50);
        stzcpy(dosbuf->curdir,dosbuf->homedir,50);
        mdir(dosbuf->curdir);

        fsdfxt(PASSWORDFLD,dosbuf->password,9);

        dosbuf->security=0;
        if (fsdord(CANMOVEFLD)) dosbuf->security|=CANMOVE;
        if (fsdord(TYPEFLD)) dosbuf->security|=CMD_TYPE;
        if (fsdord(EDITFLD)) dosbuf->security|=CMD_EDIT;
        if (fsdord(DATEFLD)) dosbuf->security|=CMD_DATE;
        if (fsdord(TIMEFLD)) dosbuf->security|=CMD_TIME;
        if (fsdord(ATTRIBFLD)) dosbuf->security|=CMD_ATTRIB;
        if (fsdord(RENFLD)) dosbuf->security|=CMD_REN;
        if (fsdord(COPYFLD)) dosbuf->security|=CMD_COPY;
        if (fsdord(XCOPYFLD)) dosbuf->security|=CMD_XCOPY;
        if (fsdord(MOVEFLD)) dosbuf->security|=CMD_MOVE;
        if (fsdord(MDFLD)) dosbuf->security|=CMD_MD;
        if (fsdord(RDFLD)) dosbuf->security|=CMD_RD;
        if (fsdord(GETFLD)) dosbuf->security|=CMD_GET;
        if (fsdord(PUTFLD)) dosbuf->security|=CMD_PUT;
        if (fsdord(DELFLD)) dosbuf->security|=CMD_DEL;
        if (fsdord(DELTREEFLD)) dosbuf->security|=CMD_DELTREE;
        if (fsdord(SPEEDFLD)) dosbuf->security|=CMD_SPEED;
        if (fsdord(GREPFLD)) dosbuf->security|=CMD_GREP;

        if (dfaAcqEQ(vdatmp,dosbuf->userid,0)) dfaUpdateV(NULL,sizeof(struct dosrec)+1);
        else dfaInsertV(NULL,sizeof(struct dosrec)+1);
        //update settings if user command was applied to self
        if (sameas(usaptr->userid,dosbuf->userid)) dfaAcqEQ(&dosptr,dosbuf->userid,0);
        prfmsg(USERSAVD);
       }
    }
  shw_prompt(1);
  btuchi(usrnum,chrprc);                 //Character input routine
  btupbc(usrnum,0);
}


/*
  Init the user
*/
static INT init_dos(VOID)
{
  CHAR *envprompt;

  dfaSetBlk(dosbt);
  if (dfaAcqEQ(&dosptr,usaptr->userid,0))
    {/* Starting directory */
     if (!fnd1st(&dosfile,dspr("%s*.*",dosptr.curdir),ALLATTR))
       {/* revert to home directory if not found */
        stzcpy(dosptr.curdir,dosptr.homedir,50);
        if (!fnd1st(&dosfile,dspr("%s",dosptr.curdir),ALLATTR))
          {prfmsg(CWDNTFND,dosptr.curdir);
           if (usrptr->flags&MASTER)
             {stzcpy(dosptr.curdir,dspr("%c:\\",'A'+getdisk()),4);
              getcurdir(0,&dosptr.curdir[3]);
             }
           else
             {//just incase...
              outprf(usrnum);
              return(0);
             }
          }
       }
    }
  else
    {//new dos user
     setmem(&dosptr,sizeof(struct dosrec),0);
     stzcpy(dosptr.userid,usaptr->userid,30);
     /* Starting directory */
     stzcpy(dosptr.curdir,dspr("%c:\\",'A'+getdisk()),4);
     getcurdir(0,&dosptr.curdir[3]);
     stzcpy(dosptr.homedir,dspr("%c:\\",*dosptr.curdir),50);

     dosptr.security=0xFFFFFFFFL;

     /* Default Colorization */
     dosptr.dirclr=15;
     stzcpy(&dosptr.ext[0],"EXE",4);
     dosptr.extclr[0]=15;
     stzcpy(&dosptr.ext[1*4],"DLL",4);
     dosptr.extclr[1]=14;
     stzcpy(&dosptr.ext[2*4],"BAT",4);
     dosptr.extclr[2]=11;
    }

  if (dosptr.speed == 0)
    {dosptr.speed=50;
     /* Reset all current working directories */
     for (dosvda->fnum=0;dosvda->fnum < 26;dosvda->fnum++) stzcpy(GETDIR(dosvda->fnum+'A'),"\\",MAXDIR-1);
     /* Get prompt from environment.  Under DOS PROMPT was always set; a    */
     /* Windows BBS often has no PROMPT variable, so fall back to MS-DOS's */
     /* own default rather than copy from a NULL pointer.                  */
     envprompt=getenv("PROMPT");
     stzcpy(dosptr.prompt,(envprompt != NULL && *envprompt != '\0') ? envprompt : "$p$g",MAXDIR);
     if (dfaAcqEQ(NULL,usaptr->userid,0)) dfaUpdateV(&dosptr,sizeof(struct dosrec)+1);
     else dfaInsertV(&dosptr,sizeof(struct dosrec)+1);
    }
  //Get current working directory
  mdir(dosptr.curdir);
  stzcpy(GETDIR(*dosptr.curdir),&dosptr.curdir[2],MAXDIR);
  dosvda->flags=ECHO;
  dosvda->doskey=0;
  dosvda->srcfile=NULL;
  dosvda->destfile=NULL;
  dosvda->flags&=~BATCHFILE;
  dosactive[usrnum]=1;
  echon();
  btuchi(usrnum,chrprc); //Character input routine
  btupbc(usrnum,0);

  prfmsg(ENTERDOS);
  shw_prompt(1);
  outprf(usrnum);

  return(1);
}


/*
  CONFIG form field check: everything is stored in upper case.
*/
static INT cvfy(INT fldno,CHAR *answer)
{
  strupr(answer);
  return(vfyadn(fldno,answer));
}


/*
  Called after FSDE session
*/
static VOID cfinish(FINRET save)
{
  setmbk(dosmb);
  dfaSetBlk(genbb);
  if (fsdscb->chgcnt == 0 || !save) prfmsg(NOTSAVD);
  else
    {
     fsdfxt(CSYSKEY,config.syskey,16);
     fsdfxt(CMSPASSWD,config.mspasswd,9);
     (fsdord(CENPASSWD)) ? (config.flags|=ENPASSWD) : (config.flags&=~ENPASSWD);
     (fsdord(CAUDTXFER)) ? (config.flags|=AUDTXFER) : (config.flags&=~AUDTXFER);
     (fsdord(CBATCHPRT)) ? (config.flags|=BATCHPRT) : (config.flags&=~BATCHPRT);
     if (dfaAcqEQ(vdatmp,&config,0)) dfaUpdateV(&config,sizeof(struct configrec));
     prfmsg(SETSAVD);
    }
  btuchi(usrnum,chrprc);                 //Character input routine
  btupbc(usrnum,0);
  shw_prompt(0);
}



static VOID clroptions(VOID)
{ CHAR c;
  CHAR *clrtxt[]={"","BLUE","GREEN","CYAN","RED","MAGENTA","BROWN","GRAY",
                  "MED.GRAY","LT.BLUE","LT.GREEN","LT.CYAN","LT.RED","PINK","YELLOW","WHITE"};

  if (!(dosptr.dirclr))
    {for (c=0; c < 20; c++)
       {dosptr.ext[c*4]='\0';
        dosptr.extclr[c]=0;
       }
     dosptr.dirclr=15;
     stzcpy(&dosptr.ext[0],"EXE",4);
     dosptr.extclr[0]=15;
     stzcpy(&dosptr.ext[1*4],"DLL",4);
     dosptr.extclr[1]=14;
     stzcpy(&dosptr.ext[2*4],"BAT",4);
     dosptr.extclr[2]=11;
    }

  fsdroom(CNF20,clrfsp,1);
  sprintf(vdatmp,clrfmt,
          clrtxt[dosptr.dirclr],'\0',
          &dosptr.ext[0],'\0',
          clrtxt[dosptr.extclr[0]],'\0',
          &dosptr.ext[1*4],'\0',
          clrtxt[dosptr.extclr[1]],'\0',
          &dosptr.ext[2*4],'\0',
          clrtxt[dosptr.extclr[2]],'\0',
          &dosptr.ext[3*4],'\0',
          clrtxt[dosptr.extclr[3]],'\0',
          &dosptr.ext[4*4],'\0',
          clrtxt[dosptr.extclr[4]],'\0',
          &dosptr.ext[5*4],'\0',
          clrtxt[dosptr.extclr[5]],'\0',
          &dosptr.ext[6*4],'\0',
          clrtxt[dosptr.extclr[6]],'\0',
          &dosptr.ext[7*4],'\0',
          clrtxt[dosptr.extclr[7]],'\0',
          &dosptr.ext[8*4],'\0',
          clrtxt[dosptr.extclr[8]],'\0',
          &dosptr.ext[9*4],'\0',
          clrtxt[dosptr.extclr[9]],'\0',
          &dosptr.ext[10*4],'\0',
          clrtxt[dosptr.extclr[10]],'\0',
          &dosptr.ext[11*4],'\0',
          clrtxt[dosptr.extclr[11]],'\0',
          &dosptr.ext[12*4],'\0',
          clrtxt[dosptr.extclr[12]],'\0',
          &dosptr.ext[13*4],'\0',
          clrtxt[dosptr.extclr[13]],'\0',
          &dosptr.ext[14*4],'\0',
          clrtxt[dosptr.extclr[14]],'\0',
          &dosptr.ext[15*4],'\0',
          clrtxt[dosptr.extclr[15]],'\0',
          &dosptr.ext[16*4],'\0',
          clrtxt[dosptr.extclr[16]],'\0',
          &dosptr.ext[17*4],'\0',
          clrtxt[dosptr.extclr[17]],'\0',
          &dosptr.ext[18*4],'\0',
          clrtxt[dosptr.extclr[18]],'\0',
          &dosptr.ext[19*4],'\0',
          clrtxt[dosptr.extclr[19]],'\0'
         );
  fsdapr(dosvda->text,4096,vdatmp);
  fsdbkg(fsdrft());
  fsdego(vfyadn,clrfinish);
}



/*
  Config2 color finish
*/
static VOID clrfinish(FINRET save)
{ INT cnt;

  setmbk(dosmb);
  dfaSetBlk(genbb);
  if (fsdscb->chgcnt == 0 || !save) prfmsg(NOTSAVD);
  else
    {dosptr.dirclr=fsdord(0)+1;
     for (cnt=1; cnt < 41; cnt+=2)
       {fsdfxt(cnt,&dosptr.ext[((cnt-1)/2)*4],4);
        if (dosptr.ext[((cnt-1)/2)*4]) dosptr.extclr[(cnt-1)/2]=fsdord(cnt+1)+1;
        else dosptr.extclr[(cnt-1)/2]=0;
       }
     prfmsg(SETSAVD);
    }
  btuchi(usrnum,chrprc);                 //Character input routine
  btupbc(usrnum,0);
  shw_prompt(0);
}


