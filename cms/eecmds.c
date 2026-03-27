/*
** EECMDS.C    - MECAFF EE editor commands handler
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements the commands supported by the EE editor including
** the related support functionality.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012,2013
** Released to the public domain.
*/

#include "glblpre.h"

#include <stdio.h>
#include <cmssys.h>
#include <CMSRUNTM.H>
#include <string.h>

#include "errhndlg.h"

#include "ee_first.h"
#include "eecore.h"
#include "eeutil.h"
#include "fs3270.h"
#include "eescrn.h"
#include "eemain.h"
#include "fs3270.h"
#include "fsio.h"
#include "ee_pgm.h"

#include "glblpost.h"

/* set the filename for error messages from memory protection in EEUTIL */
static const char *_FILE_NAME_ = "eecmds.c";

extern void LoadPoint() ;

/* forward declarations */
static bool parseTabs(char *params, int *tabs, int *count);
static bool isInternalEE(EditorPtr ed);
static int CmdSqmetDisplay(ScreenPtr scr, char sqmet, char *params, char *msg);
static int closeAllFiles(ScreenPtr scr, bool saveModified, char *msg);

/*
** ****** global screen data
*/

/*
#define CMD_HISTORY_LEN 1024
#define CMD_HISTORY_DUPE_CHECK 32
*/
/* static EditorPtr commandHistory;   */ /* eecore-editor with the command history */

/* static EditorPtr filetypeDefaults; */ /* eecore-editor with the ft-defaults     */
/* static EditorPtr filetypeTabs;     */ /* eecore-editor with the ft-default-tabs */
/* static EditorPtr macroLibrary;     */ /* eecore-editor with the macro library   */

/*
static char pfCmds[25][CMDLINELENGTH+1];      / * the PF key commands        * /
static int fileCount = 0;                     / * number of open files       * /
static char searchPattern[CMDLINELENGTH + 1]; / * last search pattern        * /
static bool searchUp;                         / * was last search upwards ?  * /
static char *saveMsgPtr;                      / * debugging SUBCOM           * /
static ScreenPtr saveScreenPtr;               / * debugging SUBCOM           * /
static t_PGMB *savePGMB_loc;                  / * debugging SUBCOM           * /
static long versionCount;                     / * debugging SUBCOM           * /
*/




/*
** ****** utilities ******
*/

/* function pointer type for all command implementation routines, with
    'scr': the screen on which the command was entered
    'params': the parameter string of the command
    'msg': the message string to which to copy infos/warnings/errors
   returns: terminate processing of the current file ?

   Routines implementing this function pointer type are named 'CmdXXXX' with
   XXXX being the EE command implemented.
*/
typedef int (*CmdImpl)(ScreenPtr scr, char *params, char *msg);
typedef int (*CmdSqmetImpl)(ScreenPtr scr, char sqmet, char *params, char *msg);




/************************************************************************************
*************************************************************************************
**                                                                                 **
** http://bitsavers.informatik.uni-stuttgart.de/pdf/ibm/370/                       **
**        VM/SP/Release_3.0_Jul83/                                                 **
**        SC19-6203-2_VM_SP_System_Programmers_Guide_Release_3_Aug83.pdf           **
**                                                                                 **
**   paqge 346(371) : Dynamic Linkage--Subcom                                      **
**                                                                                 **
**   Note: When control passes to the specified entry point,                       **
**         the register contents are:                                              **
**    R2   Address of SCBLOCK for this entry point.                                **
**    R12  Entry point address.                                                    **
**    R13  24-word save area address.                                              **
**    R14  Return address (CMSRET).                                                **
**    R15  Entry point address.                                                    **
**                                                                                 **
*************************************************************************************
************************************************************************************/

/* we are called from SC@ENTRY : see EESUBCOM ASSEMBLE */

extern int sc_hndlr()  {
  char dummy_msg[4096];
  char command_line[256];
  dummy_msg[0] = '\0';
  command_line[0] = '\0';
  t_PGMB *PGMB_loc = CMSGetPG();  /* did not work - why ? needs R13 set correctly */
  /* PGMB_loc = savePGMB_loc; */              /* chicken or egg - which was first ? */

  /* return ++versionCount; */
  ++(PGMB_loc->versionCount);

  unsigned long *p_R0 = PGMB_loc->GPR_SUBCOM[0];
  unsigned char *q1 = *++p_R0;
  unsigned char *q2 = *++p_R0;
  unsigned long qq = q2-q1;

  if (qq > 255) {qq = 255 ;}
  int i=0;
  while (i < qq) {command_line[i++] = *q1++ ;}
  command_line[i] = '\0';


/* return execCmd(saveScreenPtr, command_line, dummy_msg, false) ; */
  return execCmd(PGMB_loc->saveScreenPtr, command_line, PGMB_loc->saveMsgPtr, false) ;
/*
  sprintf(command_line,"input PGMB_loc = %08x   p_R0 =  %08x    q1 = %08x    q2 = %08x    qq = %d   versionCount = %d  ",
                              PGMB_loc    ,     p_R0     ,      q1      ,    q2       ,   qq      , versionCount    );
  return qq;
  return PGMB_loc;
  return versionCount;
*/
}




extern int Subcom(int mode /* set=1 query=0 delete=-1 */ ) {
  int rc_subcom;
  typedef struct t_subcom_plist {
    char subcom[8]     ;
    char xedit[8]      ;
    long SUBCPSW       ;
    long ENTRY_ADDRESS ;
    long USER_WORD     ;

  } t_subcom_plist;

  t_PGMB *PGMB_loc = /* savePGMB_loc = */ CMSGetPG();
  unsigned long R13;
  __asm__("LR %0,13"    : "=d" (R13));
  PGMB_loc->cmscrab = R13;

  t_subcom_plist subcom_plist = { "SUBCOM  ", SUBCOM_name_8,0 ,&sc_entry, PGMB_loc } ;
  t_subcom_plist eplist_dummy ;

  if (mode == SUBCOM_DELETE) {
        /* SUBCOM delete */
        subcom_plist.SUBCPSW = subcom_plist.ENTRY_ADDRESS = 0;
        rc_subcom = __SVC202(&subcom_plist, &eplist_dummy,0);
  } else if (mode == SUBCOM_SET) {
        /* SUBCOM Set */
        subcom_plist.SUBCPSW = 0;
        subcom_plist.ENTRY_ADDRESS = &sc_entry;
        rc_subcom = __SVC202(&subcom_plist, &eplist_dummy,0);
  } else if (mode == SUBCOM_QUERY) {
        /* SUBCOM query */
        subcom_plist.SUBCPSW = 0;
        subcom_plist.ENTRY_ADDRESS = 0xFFffFFff;
        rc_subcom = __SVC202(&subcom_plist, &eplist_dummy,0);
        PGMB_loc->sc_block = subcom_plist.SUBCPSW ;

/*
        sprintf(msg, "SUBCOM: PGMB_loc = %08x     SCBLOCK = %08x    rc = %08x   R13 = %08x   CmdSqmetSubcom",
            PGMB_loc, PGMB_loc->sc_block, rc_subcom, R13 );
*/
  }
  return rc_subcom;
}



static int CmdSqmetSubcom(ScreenPtr scr, char sqmet, char *params, char *msg) {
  return -1; /* temporarily disabled */

  int rc_subcom;
  typedef struct t_subcom_plist {
    char subcom[8]     ;
    char xedit[8]      ;
    long SUBCPSW       ;
    long ENTRY_ADDRESS ;
    long USER_WORD     ;

  } t_subcom_plist;

  t_PGMB *PGMB_loc = /* savePGMB_loc = */ CMSGetPG();
  unsigned long R13;
  __asm__("LR %0,13"    : "=d" (R13));
  PGMB_loc->cmscrab = R13;

  t_subcom_plist subcom_plist = { "SUBCOM  ", SUBCOM_name_8,0 ,&sc_entry, PGMB_loc } ;
  t_subcom_plist eplist_dummy ;

  /* SUBCOM delete */
  subcom_plist.SUBCPSW = subcom_plist.ENTRY_ADDRESS = 0;
  __SVC202(&subcom_plist, &eplist_dummy,0);

  /* SUBCOM Set */
  subcom_plist.SUBCPSW = 0;
  subcom_plist.ENTRY_ADDRESS = &sc_entry;
  rc_subcom = __SVC202(&subcom_plist, &eplist_dummy,0);

  /* SUBCOM query */
  subcom_plist.SUBCPSW = 0;
  subcom_plist.ENTRY_ADDRESS = 0xFFffFFff;
  __SVC202(&subcom_plist, &eplist_dummy,0);
  PGMB_loc->sc_block = subcom_plist.SUBCPSW ;

  sprintf(msg, "SUBCOM: PGMB_loc = %08x     SCBLOCK = %08x    rc = %08x   R13 = %08x   CmdSqmetSubcom",
      PGMB_loc, PGMB_loc->sc_block, rc_subcom, R13 );

  return rc_subcom;
}

int ExecCommSet(char *msg, char *var_name, char *value, unsigned long len) {

  /* from z/VM 6.4 Help MACROS SHVBLOCK
  *        ***  LAYOUT OF SHARED-VARIABLE ACCESS CONTROL BLOCK  ***
  *
  *   THE CONTROL BLOCKS FOR ACCESSING SHARED VARIABLES ARE CHAINED
  *   AS A LIST TERMINATED BY A NULL POINTER.  THE LIST IS ADDRESSED
  *   VIA THE 'PRIVATE INTERFACE' PLIST IN A SUBCOMMAND CALL TO A
  *   PUBLIC VARIABLE-SHARING ENVIRONMENT (E.G. AS SET UP BY THE
  *   EXEC 2 OR REXX/VM).
  *
  SHVBLOCK DSECT ,
  SHVNEXT  DS    A        (+0)  CHAIN POINTER (0 IF LAST)
  SHVUSER  DS    A        (+4)  NOT USED, AVAILABLE FOR PRIVATE
  *                       use EXCEPT DURING 'FETCH NEXT'
  SHVCODE  DS    CL1      (+8)  INDIVIDUAL FUNCTION CODE
  SHVRET   DS    XL1      (+9)  INDIVIDUAL RETURN CODE FLAG
           DS    H'0'           RESERVED, SHOULD BE ZERO
  SHVBUFL  DS    F       (+12)  LENGTH OF 'FETCH' VALUE BUFFER
  SHVNAMA  DS    A       (+16)  ADDR OF PUBLIC VARIABLE NAME
  SHVNAML  DS    F       (+20)  LENGTH OF PUBLIC VARIABLE NAME
  SHVVALA  DS    A       (+24)  ADDR OF VALUE BUFFER (0 IF NONE)
  SHVVALL  DS    F       (+28)  LENGTH OF VALUE (SET BY 'FETCH')
  SHVBLEN  EQU   *-SHVBLOCK     (LENGTH OF THIS BLOCK = 32)
  */


  typedef struct t_shv_block {
    void          *shv_next ;
    unsigned long shv_user  ;
    char          shv_code  ;
    char          shv_ret   ;
    short         shv_zero  ;
    unsigned long shv_bufl  ;
    unsigned long shv_nama  ;
    unsigned long shv_naml  ;
    unsigned long shv_vala  ;
    unsigned long shv_vall  ;
  } t_shv_block;

  int rc_execcomm;
  typedef struct t_execcomm_plist {
    char execcomm[8] ;   /* = EXECCOMM (always) */
  } t_execcomm_plist;

  typedef struct t_execcomm_eplist {
    unsigned long R1_pure;
    unsigned long blank1;
    unsigned long blank2;
    t_shv_block *p_shv_block;
  } t_execcomm_eplist;


  int l1=0; while(var_name[l1]) {l1++;}
  int l2=0;

  if (len) {
    l2 = len;  /* length given: terminating null byte might not be present */
  } else {
    l2=0; while(value[l2]) {l2++;}
  }
  t_shv_block  shv_block = { 0, 0, 'S', 0, 0, 0, &var_name[0], l1, &value[0], l2} ;
  t_execcomm_plist  execcomm_plist  = { "EXECCOMM" } ;
  t_execcomm_eplist execcomm_eplist = { &execcomm_plist, 0, 0, &shv_block } ;

  rc_execcomm = __SVC202(&execcomm_plist, &execcomm_eplist, 0x02);
  if (rc_execcomm) {
    sprintf(msg,"EXTRACT command valid only when issued from a macro: RC = %d", rc_execcomm);
    return rc_execcomm;
  }
}


static int CmdSqmetVersion(ScreenPtr scr, char sqmet, char *params, char *msg) {
  t_PGMB *PGMB_loc = CMSGetPG();
  sprintf(msg, "version --- 2024-09-17 23:45 %d --- " VERSION, PGMB_loc->versionCount);
  return false;
}


static void checkNoParams(char *params, char *msg) {
  if (!params) { return; }
  while(*params == ' ' || *params == '\t') { params++; }
  if (*params) {
    if (*msg) { strcat(msg, "\n"); }
    strcat(msg, "Extra parameters ignored!");
  }
}

static char* parseFnFtFm(
    EditorPtr ed, char *params,
    char *fn, char *ft, char *fm,
    bool *found, char *msg) {

  *found = false;

  char fnDefault[9];
  char ftDefault[9];
  char fmDefault[3];

  int tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen == 0) { return params; }

  getFnFtFm(ed, fnDefault, ftDefault, fmDefault);

  if (msg) { /* ensure we let append the message, if requested */
    msg = &msg[strlen(msg)];
  }

  char *lastCharRead = NULL;
  int consumed = 0;
  int parseRes = parse_fileid(
        &params, 0, 1,
        fn, ft, fm, &consumed,
        fnDefault, ftDefault, fmDefault,
        &lastCharRead, msg);

  *found = (parseRes == PARSEFID_OK);
  return lastCharRead;
}

/*
** ****** filetype defaults & tabs ******
*/

static void fillFtPattern(char *trg, char *ft) {
  int len = minInt(strlen(ft), 8);
  strcpy(trg, "#########");
  while(len) { *trg++ = c_upper(*ft++); len--; }
}

static void addFtDefault(char *ft, int lrecl, char recfm, char caseMode,
                         int workLrecl) {
  t_PGMB *PGMB_loc = CMSGetPG();
  char pattern[10];
  char ftDef[18];

  fillFtPattern(pattern, ft);
  sprintf(ftDef, "%s %c %c %03d %03d",
    pattern,
    c_upper(recfm),
    c_upper(caseMode),
    maxInt(1, minInt(lrecl, 255)),
    maxInt(1, minInt(workLrecl, 255)));
  if (strlen(ftDef) != 21) {
    printf("** ERROR: strlen('%s') -> %d (not 21)!\n",
      ftDef, strlen(ftDef));
  }
  /*
  printf("addFtDefault() -> pattern = '%s'\n", pattern);
  printf("addFtDefault() -> ftDef   = '%s'\n", ftDef);*/

  moveToBOF(PGMB_loc->filetypeDefaults);
  if (findString(PGMB_loc->filetypeDefaults, pattern, false, NULL)) {
    /* filetype already defined */
    LinePtr line = getCurrentLine(PGMB_loc->filetypeDefaults);
    updateLine(PGMB_loc->filetypeDefaults, line, ftDef, strlen(ftDef));
    /*printf("addFtDefault() -> definition updated\n");*/
  } else {
    /* new filetype */
    moveToBOF(PGMB_loc->filetypeDefaults);
    insertLine(PGMB_loc->filetypeDefaults, ftDef);
    /*printf("addFtDefault() -> new definition inserted\n");*/
  }
}

static void addFtTabs(char *ft, int *tabs) {
  t_PGMB *PGMB_loc = CMSGetPG();
  char pattern[10];
  char tabsLine[81];

  fillFtPattern(pattern, ft);
  strcpy(tabsLine, pattern);
  /* first tab text will start at offset 10 in the line */

  int i;
  for (i = 0; i < MAX_TAB_COUNT; i++) {
    char *tabsLineEnd = tabsLine + strlen(tabsLine);
    sprintf(tabsLineEnd, " %d", tabs[i]+1);
  }

  moveToBOF(PGMB_loc->filetypeTabs);
  if (findString(PGMB_loc->filetypeTabs, pattern, false, NULL)) {
    /* filetype already defined */
    LinePtr line = getCurrentLine(PGMB_loc->filetypeTabs);
    updateLine(PGMB_loc->filetypeTabs, line, tabsLine, strlen(tabsLine));
  } else {
    /* new filetype */
    moveToBOF(PGMB_loc->filetypeTabs);
    insertLine(PGMB_loc->filetypeTabs, tabsLine);
  }
}

/*
** Open / Close a file
*/

static const char* FNFT_ALLOWED
    = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$+-_";

static const char* FM1_ALLOWED = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char* FM2_ALLOWED = "012345"; /* was "0123456789" */
/* VM/370 and VM/SP allow the file mode number from 0 to 5.
   see SC19-6204-1 IBM Virtual Machine/System Product: System Messages and Codes
   048E INVALID MODE 'mode'
   The mode number, if specified, is not between 0 and 5    */

/* return first char of 'cand' not in 'allowed' or NULL if all were found */
static char* strchk(char *cand, const char *allowed) {
  if (!cand || !allowed) { return NULL; }
  while(*cand) {
    if (!strchr(allowed, *cand)) { return cand; }
    cand++;
  }
  return NULL;
}

static void switchToEditor(ScreenPtr scr, EditorPtr newEd) {
  switchPrefixesToFile(scr, newEd);
  scr->ed = newEd;
}

void openFile(
    ScreenPtr scr,
    char *fn,
    char *ft,
    char *fm,
    int *state, /* 0=OK, 1=FileNotFound, 2=ReadError, 3=OtherError */
    char *msg) {

  t_PGMB *PGMB_loc = CMSGetPG();
  char pattern[10];
  char recfm;
  char caseMode;
  int defaultLrecl = scr->screenColumns - 7; /* 7 = prefix-zone overhead */
  int lrecl = defaultLrecl;
  int workLrecl = defaultLrecl;

  s_upper(fn, fn);
  s_upper(ft, ft);
  s_upper(fm, fm);

  fillFtPattern(pattern, ft);
  /*printf("openFile() -> pattern = '%s'\n", pattern);*/

  moveToBOF(PGMB_loc->filetypeDefaults);
  if (findString(PGMB_loc->filetypeDefaults, pattern, false, NULL)) {
    /* filetype defaults found */
    /*printf("openFile() -> filetype defaults found\n");*/
    LinePtr line = getCurrentLine(PGMB_loc->filetypeDefaults);
    char *txt = line->text;
    tryParseInt(&txt[14], &lrecl);
    tryParseInt(&txt[18], &workLrecl);
    recfm = txt[10];
    caseMode = txt[12];
  } else {
    /* no defaults defined */
    /*printf("openFile() -> NO filetype defaults found, using defaults\n");*/
    recfm = 'V';
    caseMode = 'M';
  }
  /*
  printf("openFile() -> lrecl = %d, recfm=%c, caseMode=%c\n",
    lrecl, recfm, caseMode);*/

  /* check if the file is already open */
  EditorPtr guardEd = scr->ed;
  char ofn[9];
  char oft[9];
  char ofm[3];
  if (guardEd != NULL) {
    EditorPtr oldEd = guardEd;
    while(true) {
      getFnFtFm(oldEd, ofn, oft, ofm);
      if (sncmp(fn, ofn) == 0
          && sncmp(ft, oft) == 0
          && c_upper(fm[0]) == c_upper(ofm[0])) {
        strcpy(msg, "File already open, switched to open file");
        switchToEditor(scr, oldEd);
        return;
      }
      oldEd = getNextEd(oldEd);
      if (oldEd == guardEd) { break; }
    }
  }

  char *firstInv = strchk(fn, FNFT_ALLOWED);
  if (firstInv) {
    *state = 3;
    sprintf(msg, "Invalid character '%c' in filename (fileid: %s %s %s)",
      *firstInv, fn, ft, fm);
    return;
  }
  firstInv = strchk(ft, FNFT_ALLOWED);
  if (firstInv) {
    *state = 3;
    sprintf(msg, "Invalid character '%c' in filetype (fileid: %s %s %s)",
      *firstInv, fn, ft, fm);
    return;
  }
  if (!strchr(FM1_ALLOWED, fm[0])
      || (fm[1] && !strchr(FM2_ALLOWED, fm[1]))) {
    *state = 3;
    sprintf(msg, "Invalid character in filemode (fileid: %s %s %s)",
      fn, ft, fm);
    return;
  }

  EditorPtr ed = createEditorForFile(
                       scr->ed,
                       fn, ft, fm,
                       lrecl, recfm,
                       state, msg);
  if (*state >= 2) {
    return;
  }
  if (ed) {
    if (workLrecl != lrecl) {
      setWorkLrecl(ed, workLrecl);
    }
    if (caseMode == 'U') {
      setCaseMode(ed, true);
      setCaseRespect(ed, false);
    } else if (caseMode == 'M') {
      setCaseMode(ed, false);
      setCaseRespect(ed, false);
    } else {
      setCaseMode(ed, false);
      setCaseRespect(ed, true);
    }
    moveToBOF(PGMB_loc->filetypeTabs);
    if (findString(PGMB_loc->filetypeTabs, pattern, false, NULL)) {
      LinePtr line = getCurrentLine(PGMB_loc->filetypeTabs);
      int tabs[MAX_TAB_COUNT];
      int tabCount;
      bool someIgnored = parseTabs(&line->text[10], tabs, &tabCount);
      if (tabCount > 0) { setTabs(ed, tabs); }
    }
    moveToBOF(ed);
    switchToEditor(scr, ed);
    PGMB_loc->fileCount++;
    char buffer[255];
    sprintf (buffer, "PROFILER %s %s %2s \n\0", fn, ft, fm);
    execCmd(scr, buffer , msg, false);
  }
}

static int closeFile(ScreenPtr scr, char *msg) {
  t_PGMB *PGMB_loc = CMSGetPG();
  EditorPtr ed = scr->ed;
  if (!ed) { return true; }

    if (isInternalEE(ed)) {
      sprintf(msg, "Cannot close internal file HISTORY/DEFAULT/TABS/MACROS");
      return false;
    }

/* 2024-09-17-2152 switch to previous file in ring as XEDIT, KEDIT do */

/*EditorPtr nextEd = getNextEd(ed);*/
  EditorPtr prevEd = getPrevEd(ed);
  freeEditor(ed);
  PGMB_loc->fileCount--;
/*if (nextEd == ed) {*/
  if (prevEd == ed) {
    /* the current editor was the last one, so closing also closes EE */
    scr->ed = NULL;
    return true;
  }
/*EditorPtr guardEd = nextEd;*/
  EditorPtr guardEd = prevEd;
  while true {
/*  switchToEditor(scr, nextEd);*/
    switchToEditor(scr, prevEd);
    ed = scr->ed;
    if (!isInternalEE(ed)) { break; }
/*  nextEd = getNextEd(ed);*/
    prevEd = getPrevEd(ed);
/*  if (nextEd == guardEd) { return closeAllFiles(scr, false, msg); }*/
    if (prevEd == guardEd) { return closeAllFiles(scr, false, msg); }
  }

  return false;
}

static int closeAllFiles(ScreenPtr scr, bool saveModified, char *msg) {
  t_PGMB *PGMB_loc = CMSGetPG();
  EditorPtr ed = scr->ed;
  while(PGMB_loc->fileCount > 0) {
    EditorPtr nextEd = getNextEd(ed);

    if (getModified(ed) && saveModified && !isInternalEE(ed)) {
      char myMsg[120];
      int result = saveFile(ed, myMsg);
      if (result != 0) {
        strcpy(msg, myMsg);
        switchToEditor(scr, ed);
        return false;
      }
    }

    if (!isInternalEE(ed)) {
      freeEditor(ed);
      PGMB_loc->fileCount--;
    } else {
      /* detachEditor(ed); */     /* DEBUG */
      PGMB_loc->fileCount--;                /* DEBUG */
    }
    ed = nextEd;
  }
  scr->ed = NULL;
  return false;
  return true;
}

int _fcount() {
  t_PGMB *PGMB_loc = CMSGetPG();
  return PGMB_loc->fileCount;
}


/* SET, Query, MODify, EXTract, TRAnsfer */
typedef struct _mysqmetdef {
  char *sqmetName;
  char *sqmetFlag;
  CmdSqmetImpl sqmetImpl;
} MySqmetDef;





/*
** ****** commands ******
*/

static int CmdInput(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  /* we are called from Input, INPUTB, REPLace, REPLACEB */
  bool replace    = false;
  bool blank_line = false;
  bool no_blank   = false;
  bool input_mode = false;
  char *check     = params;
  char c;

  if (!params) { input_mode = true; }  /* NULL pointer -> cannot check command name */
  else {
    if (!*params) { /* end of string after command ? */
      input_mode = true;  /* input mode assumed, but check for blank line later */
      int count_blanks = 0;
      while (*(--params) == ' ') {count_blanks++;};
      if ( *params == 'B' || *params == 'b' || count_blanks >= 2) {
        /* INPUTB or REPLACEB or 2+ blanks: allow insertion of blank line */
        blank_line = true;
        input_mode = false;
        params--;
      };
    } else {
      if (*(--params) != ' ') { no_blank = true; }
        /* check for something like "INPUT123ABC", there might be no blank between */
      else {
        while (*(--params) == ' ') {};
        /* skip back to command token like "INPUT", care for leading blanks */
        check = params+2;  /* input line including leading blanks */
      };
      /* now we are at the last character of command name */
      if ( *params == 'B' || *params == 'b' ) {
        /* INPUTB or REPLACEB : allow insertion of blank line */
        blank_line = true;
        input_mode = false;
        params--;
      };
      /* is it REPLace ? last character can be l,a,c,e  -> cannot be Input */
      c = *params;
      if (   c == 'l' || c == 'L' || c == 'a' || c == 'A'
          || c == 'c' || c == 'C' || c == 'e' || c == 'E' ) {replace = true;};
    };
  };

  if (!input_mode) {
    if (replace) {
      LinePtr line = getCurrentLine(scr->ed);
      updateLine(scr->ed, line, check, strlen(check));
    } else {
      insertLine(scr->ed, check);
    }
  } else {
    processInputMode(scr);
  }
  return false;
}

static int CmdProgrammersInput(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  checkNoParams(params, msg);
  processProgrammersInputMode(scr);
  return false;
}

static int CmdTop(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  checkNoParams(params, msg);
  moveToBOF(scr->ed);
  return false;
}

static int CmdBottom(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  checkNoParams(params, msg);
  moveToLastLine(scr->ed);
  return false;
}

static int CmdNext(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int count = 1;
  if (tryParseInt(params, &count)) {
    params =  getCmdParam(params);
  }
  checkNoParams(params, msg);
  if (count > 0) {
    moveDown(scr->ed, count);
  } else if (count < 0){
    moveUp(scr->ed, -count);
  }
  return false;
}

static int CmdPrevious(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int count = 1;
  if (tryParseInt(params, &count)) {
    params =  getCmdParam(params);
  }
  checkNoParams(params, msg);
  if (count > 0) {
    moveUp(scr->ed, count);
  } else if (count < 0){
    moveDown(scr->ed, -count);
  }
  return false;
}

static int getLineDistance(ScreenPtr scr, char **params) {
  int number = 100;
  int lines = scr->visibleEdLines - 1;
  if (tryParseInt(*params, &number)) {
    *params = getCmdParam(*params);
    if (number < 0) {
      number = maxInt(1, minInt(scr->visibleEdLines * 2 / 3, -number));
      lines = scr->visibleEdLines - number;
    } else {
      number = maxInt(33, minInt(100, number));
      lines = ((scr->visibleEdLines * number) / 100) - 1;
    }
  }
  return lines;
}

static int CmdPgUp(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int distance = getLineDistance(scr, &params);
  bool doMoveHere = false;
  if (isAbbrev(params, "MOVEHere")) {
    doMoveHere = true;
    params = getCmdParam(params);
  }
  if (scr->cElemType == 2 && doMoveHere) {
    moveToLine(scr->ed, scr->cElem);
    scr->cursorPlacement = 2;
    scr->cursorLine = scr->cElem;
    scr->cursorOffset = scr->cElemOffset;
  } else {
    moveUp(scr->ed, distance);
  }
  checkNoParams(params, msg);
  return false;
}

static int CmdPgDown(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int distance = getLineDistance(scr, &params);
  bool doMoveHere = false;
  if (isAbbrev(params, "MOVEHere")) {
    doMoveHere = true;
    params = getCmdParam(params);
  }
  if (scr->cElemType == 2 && doMoveHere) {
    moveToLine(scr->ed, scr->cElem);
    scr->cursorPlacement = 2;
    scr->cursorLine = scr->cElem;
    scr->cursorOffset = scr->cElemOffset;
  } else {
    moveDown(scr->ed, distance);
  }
  checkNoParams(params, msg);
  return false;
}

static int CmdMoveHere(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (scr->cElemType == 2) {
    moveToLine(scr->ed, scr->cElem);
    scr->cursorPlacement = 2;
    scr->cursorLine = scr->cElem;
    scr->cursorOffset = scr->cElemOffset;
  }
  checkNoParams(params, msg);
  return false;
}

static int CmdSaveInner(
    ScreenPtr scr,
    char *params,
    char *msg,
    bool force,
    bool allowFileClose) {
  if (!scr->ed) { return false; }
  char fn[9];
  char ft[9];
  char fm[3];
  bool fFound = false;
  char myMsg[81] = "";
  params = parseFnFtFm(scr->ed, params, fn, ft, fm, &fFound, myMsg);
  checkNoParams(params, msg);
  if (!fFound && *myMsg) {
    if (*msg) { strcat(msg, "\n"); }
    strcat(msg, myMsg);
    return false;
  }
  msg = &msg[strlen(msg)];
  int result;
  if (fFound) {
    result = writeFile(scr->ed, fn, ft, fm, force, false, msg);
  } else {
    result = saveFile(scr->ed, msg);
  }
  /*printf("CmdSaveInner: writeFile()/saveFile() => result = %d\n", result);*/
  if (allowFileClose
     && result == 0
     && closeFile(scr, msg)) {
    /*printf("%s\n", msg);*/
    return true;
  }
  return false;
}

static int CmdSave(ScreenPtr scr, char *params, char *msg) {
  CmdSaveInner(scr, params, msg, false, false);
  return false;
}

static int CmdSSave(ScreenPtr scr, char *params, char *msg) {
  CmdSaveInner(scr, params, msg, true, false);
  return false;
}

static int CmdFile(ScreenPtr scr, char *params, char *msg) {
  CmdSaveInner(scr, params, msg, false, true);
  return false;
}

static int CmdFFile(ScreenPtr scr, char *params, char *msg) {
  CmdSaveInner(scr, params, msg, true, true);
  return false;
}

static int CmdQuit(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  bool isModified = getModified(scr->ed);
  if (isModified) {
    sprintf(msg, "File is modified, use QQuit to leave file without changes");
    return false;
  }
  checkNoParams(params, msg);
  closeFile(scr, msg);
  return false;
}

static int CmdQQuit(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (isAbbrev(params, "ALL")) {
    params = getCmdParam(params);
    checkNoParams(params, msg);
    return closeAllFiles(scr, false, msg);
  }
  checkNoParams(params, msg);
  closeFile(scr, msg);
  return false;
}

static int CmdRingPN(ScreenPtr scr, char *params, char *msg, bool backward) {
  t_PGMB *PGMB_loc = CMSGetPG();
  if (!scr->ed) { return false; }
  EditorPtr ed;
  int count = 1;
  int i = 0;
  bool goto_hidden   = false;
  bool goto_unhidden = false;
  bool goto_internal = false;
  bool goto_normal   = false;
  bool goto_modified = false;
  bool goto_binary   = false;
  bool goto_filtered = false;

  if (PGMB_loc->fileCount == 1) {
    strcpy(msg, "1 file in ring");
  } else {
    if (!getToken(params, ' ')) {
      goto_unhidden = true;
      goto_filtered = true;
    } else {
      if (isAbbrev(params, "Hidden")) {
        goto_hidden = true;
        goto_filtered = true;
        params = getCmdParam(params);
      } else if (isAbbrev(params, "Unhidden")) {
        goto_unhidden = true;
        goto_filtered = true;
        params = getCmdParam(params);
      } else if (isAbbrev(params, "Internal")) {
        goto_internal = true;
        goto_filtered = true;
        params = getCmdParam(params);
      } else if (isAbbrev(params, "Normal")) {
        goto_normal = true;
        goto_filtered = true;
        params = getCmdParam(params);
      } else if (isAbbrev(params, "Modified")) {
        goto_modified = true;
        goto_filtered = true;
        params = getCmdParam(params);
      } else if (isAbbrev(params, "Binary")) {
        goto_binary = true;
        goto_filtered = true;
        params = getCmdParam(params);
      } else if (tryParseInt(params, &count)) {
        params = getCmdParam(params);
      } else {
        sprintf(msg, "Ring index is not numeric: %s", params);
        return _rc_error;
      }
      if ((count < 1) || (count >= PGMB_loc->fileCount)) {
        sprintf(msg, "Ring index number must be 1 .. %d",PGMB_loc->fileCount-1);
        return _rc_error;
      }
    }
    if (goto_filtered) { count = PGMB_loc->fileCount; }
    for (i = 1; i <= count; i++)
    {
      if (backward) { switchToEditor(scr, ed = getPrevEd(scr->ed)); }
       else         { switchToEditor(scr, ed = getNextEd(scr->ed)); }
      if (goto_internal &&  isInternalEE(ed))                    { break; }
      if (goto_normal   && !isInternalEE(ed))                    { break; }
      if (goto_hidden   &&  isHidden(ed))                        { break; }
      if (goto_unhidden && !isHidden(ed))                        { break; }
      if (goto_binary   &&  isBinary(ed))                        { break; }
      if (goto_modified && getModified(ed) && !isInternalEE(ed)) { break; }
    }

  }

  checkNoParams(params, msg);
  return false;
}

static int CmdRingNext(ScreenPtr scr, char *params, char *msg) {
  return CmdRingPN(scr, params, msg, false);
}

static int CmdRingPrev(ScreenPtr scr, char *params, char *msg) {
  return CmdRingPN(scr, params, msg, true);
}

static int CmdEditFile(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }

  if ((params[0] == '-') && (params[1] == '\0')) {
    return CmdRingPrev(scr, ++params, msg);  /* behave like KEDIT   */
  }
  if ((params[0] == '+') && (params[1] == '\0')) {
    return CmdRingNext(scr, ++params, msg);
  }

  char fn[9];
  char ft[9];
  char fm[3];
  bool fFound = false;
  char myMsg[81];
  params = parseFnFtFm(scr->ed, params, fn, ft, fm, &fFound, myMsg);
  if (*myMsg) {
    strcpy(msg, "Error in specified filename:\n");
    strcat(msg, myMsg);
    return false;
  }
  if (!fFound) {
    /* strcpy(msg, "No file specified"); */
    return CmdRingNext(scr, params, msg);  /* behave like XEDIT, KEDIT   */
  }

  int state;
  openFile(scr, fn, ft, fm, &state, msg);
  if (state > 1) {
    return false;
  }

  checkNoParams(params, msg);
  return false;
}

static int CmdExit(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  checkNoParams(params, msg);
  return closeAllFiles(scr, true, msg);
}

static int CmdCaseold(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  bool paramErr = true;
  if (params && params[1] == '\0') {
    if (c_upper(*params) == 'U') {
      setCaseMode(scr->ed, true);
      setCaseRespect(scr->ed, false);
      paramErr = false;
    } else if (c_upper(*params) == 'M') {
      setCaseMode(scr->ed, false);
      setCaseRespect(scr->ed, false);
      paramErr = false;
    } else if (c_upper(*params) == 'R') {
      setCaseMode(scr->ed, false);
      setCaseRespect(scr->ed, true);
      paramErr = false;
    }
  }
  if (paramErr) {
    if (params) {
      sprintf(msg, "invalid parameter for CASE: '%s'", params);
    } else {
      sprintf(msg, "missing parameter for CASE (valid: U , M, R)");
    }
  }
  return false;
}

static int CmdReset(ScreenPtr scr, char *params, char *msg) {
  checkNoParams(params, msg);
  /* do nothing, as RESET is handled as part of prefix handling */
  return false;
}

static int CmdCmdline(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "TOP")) {
    scr->ed->view->cmdLinePos = -1;
  } else if (isAbbrev(params, "BOTtom")) {
    scr->ed->view->cmdLinePos = 1;
  } else {
    sprintf(msg, "invalid parameter for CMDLINE: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static int CmdMsglines(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "TOP")) {
    scr->msgLinePos = -1;
  } else if (isAbbrev(params, "BOTtom")) {
    scr->msgLinePos = 1;
  } else {
    sprintf(msg, "invalid parameter for MSGLINE: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static int CmdPrefix(ScreenPtr scr, char *params, char *msg) {
  bool forFsList = false;
  if (isAbbrev(params, "FSLIST")) {
    forFsList = true;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "EE")) {
    forFsList = false;
    params = getCmdParam(params);
  }
  if (isAbbrev(params, "OFf")) {
    if (forFsList) { setFSLPrefix(false); return false; }
    scr->ed->view->prefixMode = 0;
  } else if (isAbbrev(params, "LEft") || isAbbrev(params, "ON")) {
    if (forFsList) { setFSLPrefix(true); return false; }
    scr->ed->view->prefixMode = 1;
  } else if (isAbbrev(params, "RIght")) {
    if (forFsList) { setFSLPrefix(true); return false; }
    scr->ed->view->prefixMode = 2;
  } else {
    sprintf(msg, "invalid parameter for PREFIX: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static int CmdTofeof(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "ON")) {
    scr->ed->view->showTofBof = true;
  } else if (isAbbrev(params, "OFf")) {
    scr->ed->view->showTofBof = false;
  } else {
    sprintf(msg, "invalid parameter for TOFEOF: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static int CmdNumbers(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "ON")) {
    scr->ed->view->prefixNumbered = true;
  } else if (isAbbrev(params, "OFf")) {
    scr->ed->view->prefixNumbered = false;
  } else {
    sprintf(msg, "invalid parameter for NUMBERS: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static int CmdCurrline(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "TOp")) {
    scr->ed->view->currLinePos = 0;
  } else if (isAbbrev(params, "MIddle")) {
    scr->ed->view->currLinePos = 1;
  } else {
    sprintf(msg, "invalid parameter for CURRLINE: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static int CmdScale(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "OFf")) {
    scr->ed->view->scaleLinePos = 0;
  } else if (isAbbrev(params, "TOp")) {
    scr->ed->view->scaleLinePos = -1;
  } else if (isAbbrev(params, "ABOve")) {
    scr->ed->view->scaleLinePos = 1;
  } else if (isAbbrev(params, "BELow")) {
    scr->ed->view->scaleLinePos = 2;
  } else {
    sprintf(msg, "invalid parameter for SCALE: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static int CmdInfolines(ScreenPtr scr, char *params, char *msg) {
  bool forFsList = false;
  bool forFsView = false;
  bool forFsHelp = false;
  bool forEe = true;

  if (isAbbrev(params, "FSLIST")) {
    forFsList = true;
    forEe = false;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "FSVIEW")) {
    forFsView = true;
    forEe = false;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "FSHELP")) {
    forFsHelp = true;
    forEe = false;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "EE")) {
    /* EE is the default */
  }

  if (isAbbrev(params, "OFf")) {
    if (forEe) { scr->infoLinesPos = 0; }
  } else if (isAbbrev(params, "TOp")) {
    if (forEe) { scr->infoLinesPos = -1; }
  } else if (isAbbrev(params, "BOTtom")) {
    if (forEe) { scr->infoLinesPos = 1; }
  } else if (isAbbrev(params, "CLEAR")) {
    if (forFsList) {
      setFSLInfoLine(NULL);
    } else if (forFsView) {
      setFSVInfoLine(NULL);
    } else if (forFsHelp) {
      setFSHInfoLine(NULL);
    } else {
      clearInfolines();
    }
  } else if (isAbbrev(params, "ADD")) {
    params =  getCmdParam(params);
    if (!params || !*params) {
      sprintf(msg, "Missing line text for INFOLINES ADD");
      return false;
    } else {
      if (forFsList) {
        setFSLInfoLine(params);
      } else if (forFsView) {
        setFSVInfoLine(params);
      } else if (forFsHelp) {
        setFSHInfoLine(params);
      } else {
        addInfoline(params);
      }
    }
    params = NULL;
  } else {
    sprintf(msg, "invalid parameter for INFOLINES: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static int CmdNulls(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "OFf")) {
    scr->lineEndBlankFill = true;
  } else if (isAbbrev(params, "ON")) {
    scr->lineEndBlankFill = false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static char *locNames[] = {
    "INVALID TOKEN",
    "RELATIVE",
    "ABSOLUTE",
    "MARK",
    "PATTERN(DOWN)",
    "PATTERN(UP)"
    };


static int CmdAll(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  EditorPtr ed = scr->ed;
  LinePtr oldCurrentLine = getCurrentLine(ed);
  int oldCurrentLineNum = getLineNumber(oldCurrentLine);


  int found = 0;
  int rc = 0;
  char msg2[255];
  char param2[255];
  msg2[0] = '\0';
  rc = execCmd(scr, "SET SCOPE ALL"         , msg2, false);
  msg2[0] = '\0';
  rc = execCmd(scr, "SET DISPLAY 0 *"       , msg2, false);
  msg2[0] = '\0';
  rc = execCmd(scr, "TOP"                   , msg2, false);

  LinePtr CurrentLine = getCurrentLine(ed);
  int i = 0;
  int j = getLineCount(ed);
  while (i <= j) {
    i++;
    msg2[0] = '\0';
    rc = execCmd(scr, "LOCATE 1", msg2, false);
    LinePtr NextLine = getCurrentLine(ed);
    if ( CurrentLine == NextLine ) break;  /* end of file ? */
    CurrentLine = NextLine;
    msg2[0] = '\0';
    rc = execCmd(scr, "SET SELECT 0", msg2, false);
    if (i > 999999999) break;
  };

  msg2[0] = '\0';
  rc = execCmd(scr, "TOP"                   , msg2, false);

  while (*params == ' ') params++;

  sprintf(param2,"LOCATE %s", params);

  CurrentLine = getCurrentLine(ed);
  while (*params) {
    msg2[0] = '\0';
    rc = execCmd(scr, param2, msg2, false);
    LinePtr NextLine = getCurrentLine(ed);
    if ( CurrentLine == NextLine ) break;  /* target not found ? */
    found++;
    CurrentLine = NextLine;
    msg2[0] = '\0';
    rc = execCmd(scr, "SET SELECT 1"          , msg2, false);
  }


  if (found > 0) {
    msg2[0] = '\0';
    rc = execCmd(scr, "SET SCOPE DISPLAY"     , msg2, false);
    msg2[0] = '\0';
    rc = execCmd(scr, "SET DISPLAY 1 1"       , msg2, false);
    msg2[0] = '\0';
    rc = execCmd(scr, "TOP"                   , msg2, false);
    return _rc_success;
  } else {
    msg2[0] = '\0';
    rc = execCmd(scr, "SET SCOPE DISPLAY"     , msg2, false);
    msg2[0] = '\0';
    rc = execCmd(scr, "SET DISPLAY 0 0"       , msg2, false);
    msg2[0] = '\0';
    sprintf(param2,"LOCATE :%d", oldCurrentLineNum);
    rc = execCmd(scr, param2                  , msg2, false);
    return _rc_error;
  }


/*
  sprintf(msg, "DEBUG 22:39 'ALL': %d %d %d %d", 966, rc, i, j);
  return false;
*/



  return 0*rc;
};


static int CmdLocate(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  t_PGMB *PGMB_loc = CMSGetPG();
  EditorPtr ed = scr->ed;
  LinePtr oldCurrentLine = getCurrentLine(ed);

  bool tmpSearchUp = false;
  char tmpSearchPattern[CMDLINELENGTH + 1];
  int patternCount = 0;
  int othersCount = 0;
  int rc = 0;

  char buffer[2048];
  int val;

  int locCount = 1;
  int locType = parseLocation(&params, &val, buffer);
  while(locType != LOC_NONE && !IS_LOC_ERROR(locType)) {
    /*printf("--> locType = %d , remaining: '%s'\n", locType, args);*/
    if (locType == LOC_RELATIVE) {
      othersCount++;
      /*printf("--> RELATIVE %d\n", val);*/
      if (val > 0) { moveDown(ed, val); }
      if (val < 0) { moveUp(ed, - val); }
    } else if (locType == LOC_ABSOLUTE) {
      othersCount++;
      /* printf("--> ABSOLUTE %d\n", val); */
      if (!isInScope(moveToLineNo(ed, val))) {
        sprintf(msg, "Absolute line target not in scope: %d", val);
        moveToLine(ed, oldCurrentLine);
        rc = _rc_not_found;
        break;
      }
    } else if (locType == LOC_MARK) {
      othersCount++;
      /* printf("--> MARK '%s'\n", buffer); */
      if (!moveToLineMark(ed, buffer, msg)) {
        moveToLine(ed, oldCurrentLine);
        break;
      }
    } else if (locType == LOC_PATTERN) {
      patternCount++;
      tmpSearchUp = false;
      strcpy(tmpSearchPattern, buffer);
      /* printf("--> PATTERN '%s'\n", buffer); */
      if (!findString(ed, buffer, false, NULL)) {
        sprintf(msg, "%s\nPattern \"%s\" not found (downwards)", msg, buffer);
        moveToLine(ed, oldCurrentLine);
        rc = _rc_not_found;
        break;
      }
    } else if (locType == LOC_PATTERNUP) {
      patternCount++;
      tmpSearchUp = true;
      strcpy(tmpSearchPattern, buffer);
      /* printf("--> PATTERN_UP '%s'\n", buffer); */
      if (!findString(ed, buffer, true, NULL)) {
        sprintf(msg, "%s\nPattern \"%s\" not found (upwards)", msg, buffer);
        moveToLine(ed, oldCurrentLine);
        rc = _rc_not_found;
        break;
      }
    }
    locType = parseLocation(&params, &val, buffer);
    locCount++;
  }
  if (IS_LOC_ERROR(locType)) {
    sprintf(
      msg,
      "Error for location token %d (%s) starting with: %s",
      locCount, locNames[LOC_TYPE(locType)], params);
    moveToLine(ed, oldCurrentLine);
  }

  if (patternCount == 1 && othersCount == 0) {
    PGMB_loc->searchUp = tmpSearchUp;
    strcpy(PGMB_loc->searchPattern, tmpSearchPattern);
  } else {
    PGMB_loc->searchPattern[0] = '\0';
  }

  return rc;
}

static int CmdSearchNext(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  t_PGMB *PGMB_loc = CMSGetPG();
  EditorPtr ed = scr->ed;
  if (!*PGMB_loc->searchPattern) {
    sprintf(msg, "No current search pattern");
  } else {
    LinePtr oldCurrentLine = getCurrentLine(ed);
    if (!findString(ed, PGMB_loc->searchPattern, PGMB_loc->searchUp, NULL)) {
      sprintf(msg,
        (PGMB_loc->searchUp)
           ? "Pattern \"%s\" not found (upwards)"
           : "Pattern \"%s\" not found (downwards)",
        PGMB_loc->searchPattern);
      moveToLine(ed, oldCurrentLine);
      return _rc_not_found;
    }
  }
  return _rc_success;
;
}

static int CmdReverseSearchNext(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return _rc_failure; }
  t_PGMB *PGMB_loc = CMSGetPG();
  PGMB_loc->searchUp = !PGMB_loc->searchUp;
  return CmdSearchNext(scr, params, msg);
}

static int CmdMark(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  bool clear = false;
  bool paramsOk = false;

  if (isAbbrev(params, "CLear")) {
    clear = true;
    params = getCmdParam(params);
  }
  if (*params == '.') {
    setLineMark(
      scr->ed,
      (clear) ? NULL : getCurrentLine(scr->ed),
      ++params,
      msg);
    paramsOk = true;
    params = getCmdParam(params);
  } else if ((*params == '*' || isAbbrev(params, "ALL")) && clear) {
    setLineMark(scr->ed, NULL, "*", msg);
    paramsOk = true;
    params = getCmdParam(params);
  }

  if (!paramsOk) {
    strcpy(msg, "Invalid parameters for MARK");
    return false;
  }

  checkNoParams(params, msg);
  return false;
}

static int CmdChange(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  char *fromText;
  int fromTextLen;
  char *toText;
  int toTextLen;
  char separator;

  bool argsOk = parseChangePatterns(
        &params,
        &fromText, &fromTextLen,
        &toText, &toTextLen,
        &separator);

  if (!argsOk) {
    strcpy(msg, "Parameters for CHANGE could not be parsed");
    return false;
  }

  /* skip blanks after change strings */
  while(*params && *params == ' ') { params++; }

  /* verify optional 'confirm' parameter */
  char infoTxt[CMDLINELENGTH];
  bool doConfirm = false;
  if (isAbbrev(params, "CONFirm")) {
    doConfirm = true;
    params = getCmdParam(params);
  }

  /* get first param after change pattern */
  int changesPerLine = 1;
  int linesToChange = 1;
  if (*params) {
    if (*params == '*' && (params[1] == ' ' || params[1] == '\0')) {
      changesPerLine = 9999999;
      params =  getCmdParam(params);
    } else if (tryParseInt(params, &changesPerLine)) {
      params =  getCmdParam(params);
    }
  }
  if (*params) {
    if (*params == '*' && (params[1] == ' ' || params[1] == '\0')) {
      linesToChange = 9999999;
      params =  getCmdParam(params);
    } else if (tryParseInt(params, &linesToChange)) {
      params =  getCmdParam(params);
    }
  }

  fromText[fromTextLen] = '\0';
  toText[toTextLen] = '\0';

  if (doConfirm) {
    sprintf(infoTxt, "C%c%s%c%s%c",
      separator, fromText, separator, toText, separator);
  }

  bool overallFound = false;
  bool overallTruncated = false;

  LinePtr _guard = getLastLine(scr->ed);
  LinePtr _curr = getCurrentLine(scr->ed);
  LinePtr lineOrig = _curr;
  int linesDone = 0;
  int lrecl = getWorkLrecl(scr->ed);

  int changesCount = 0;

  while(linesDone < linesToChange && _curr != NULL) {
    int changesDone = 0;
    int currOffset = 0;

    while(changesDone < changesPerLine && currOffset < lrecl) {
      if (doConfirm) {
        int markFrom = (fromTextLen > 0)
                       ? findStringInLine(scr->ed, fromText, _curr, currOffset)
                       : currOffset;
        if (markFrom < 0) {
          break;
        }
        overallFound = true;
        moveToLine(scr->ed, _curr);
        int result = doConfirmChange(scr, infoTxt, markFrom, fromTextLen);
        if (result == 1) { /* not this one */
          break;
        } else if (result == 2) { /* terminate whole change command */
          linesDone = linesToChange;
          break;
        }
      }

      bool found;
      bool truncated;
      currOffset = changeString(
                    scr->ed,
                    fromText, toText,
                    _curr, currOffset,
                    &found, &truncated);
      overallFound |= found;
      overallTruncated |= truncated;
      changesDone++;
      if (found) { changesCount++; } else { break; }
    } /* end of loop over change in current line */

    _curr = getNextLine(scr->ed, _curr);
    linesDone++;
  } /* end of loop over lines */

  moveToLine(scr->ed, lineOrig);

  if (!overallFound) {
    strcpy(msg, "Source text for CHANGE not found");
    return false;
  }

  sprintf(msg,
    " %d occurence(s) changed %s",
    changesCount,
    (overallTruncated) ? "(some lines truncated)" : "");

  return false;
}

static int CmdSplitjoin(ScreenPtr scr, char *params, char *msg) {
  EditorPtr ed = scr->ed;
  if (!ed) { return false; }
  if (scr->cElemType != 2) {
    strcpy(msg, "Cursor must be placed in file area for SPLTJOIN");
    return false;
  }

  bool force = false;
  if (isAbbrev(params, "Force")) {
    force = true;
    params = getCmdParam(params);
  }

  LinePtr line = scr->cElem;
  int linePos = scr->cElemOffset;
  int lineLen = lineLength(ed, line);

  if (linePos >= lineLen) {
    /* cursor after last char => join with next line */
    if (line == getLastLine(ed)) {
      strcpy(msg, "Nothing to join with last line");
      return false;
    }
    int result = edJoin(ed, line, linePos, force);
    if (result == 0) {
      strcpy(msg, "Joining would truncate, not joined (use Force)");
    } else if (result == 2) {
      strcpy(msg, "Truncated ...");
    }
    scr->cursorPlacement = 2;
    scr->cursorOffset = linePos;
    scr->cursorLine = line;
  } else {
    /* cursor somewhere in the line => split */
    LinePtr newLine = edSplit(ed, line, linePos);
    LinePtr cLine = (linePos > 0) ? newLine : line;
    char *s = cLine->text;
    int cPos = 0;
    lineLen = lineLength(ed, cLine);
    while(*s == ' ' && cPos < lineLen) { s++; cPos++; }
    if (cPos >= lineLen) { cPos = 0; }
    scr->cursorPlacement = 2;
    scr->cursorOffset = cPos;
    scr->cursorLine = cLine;
  }

  return false;
}

static int CmdDefaults(ScreenPtr scr, char *params, char *msg) {
  bool have_GetPut = false;
  bool have_Put = false;
  bool have_Get = false;

  if (isAbbrev(params, "Get")) {
    have_Get = have_GetPut = true;
  }
  if (isAbbrev(params, "Put")) {
    have_Put = have_GetPut = true;
  }

  if (have_Get) {
    memcpy(scr->ed->view, scr->view, sizeof(struct _publicView));
    return 0;
  } else if (have_Put) {
    memcpy(scr->view, scr->ed->view, sizeof(struct _publicView));
    return 0;
  } else {
    return 5;
  }

  return false;
}

static int CmdPf(ScreenPtr scr, char *params, char *msg) {
  int pfNo    = -1;
  int pfScope = PFSCOPE_GLOBAL;
  int pfMode  = PFMODE_BEFORE;
  bool have_pfScreen = false;
  bool have_pfScope  = false;
  bool have_pfMode   = false;
  bool have_pfNo     = false;
  bool clear = false;
  bool forFsList = false;
  bool forFsView = false;
  bool forFsHelp = false;

  /* if "pfScreen" is present it must be the first token */
  if (isAbbrev(params, "FSLIST")) {
    forFsList = true;
    have_pfScreen = true;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "FSVIEW")) {
    forFsView = true;
    have_pfScreen = true;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "FSHELP")) {
    forFsHelp = true;
    have_pfScreen = true;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "EE")) {
    /* EE is the default, skip over if present */
    params = getCmdParam(params);
  }

  while (true) { /* option loop */
    if  (!have_pfScope) {
      if        (isAbbrev(params, "GLOBAL")) {
        pfScope = PFSCOPE_GLOBAL;
        have_pfScope = true;
        params = getCmdParam(params);
        continue;
      } else if (isAbbrev(params, "VIEW"))   {
        pfScope = PFSCOPE_VIEW;
        have_pfScope = true;
        params = getCmdParam(params);
/*        strcpy(msg, "DEBUG: 1737 VIEW CmdPf(ScreenPtr scr, char *params, char *msg)");    */
/*        return _rc_invalid;                                                               */

        continue;
      } else if (isAbbrev(params, "PERM"))   {
        pfScope = PFSCOPE_GLOBAL;
        have_pfScope = true;
        params = getCmdParam(params);
        continue;
      } else if (isAbbrev(params, "FILE"))   {
        pfScope = PFSCOPE_VIEW;
        have_pfScope = true;
        params = getCmdParam(params);
        continue;
      } else if (isAbbrev(params, "AUTO"))   {
        pfScope = PFSCOPE_VIEW;
        have_pfScope = true;
        params = getCmdParam(params);
        continue;
      } else if (isAbbrev(params, "SCREEN")) {
        pfScope = PFSCOPE_GLOBAL;
        have_pfScope = true;
        params = getCmdParam(params);
        continue;
      } else {
        ; /* nop */
      }
    }  /* if (!have_pfScope) */
    if (!have_pfMode) {
      if        (isAbbrev(params, "BEFORE")) {
        pfMode  = PFMODE_BEFORE;
        have_pfMode = true;
        params = getCmdParam(params);
        continue;
      } else if (isAbbrev(params, "AFTER" ))   {
        pfMode  = PFMODE_AFTER ;
        have_pfMode = true;
        params = getCmdParam(params);
/*        strcpy(msg, "DEBUG: 1774 AFTER CmdPf(ScreenPtr scr, char *params, char *msg)");   */
/*        return _rc_invalid;                                                               */

        continue;
      } else if (isAbbrev(params, "ONLY"  ))   {
        pfMode  = PFMODE_ONLY  ;
        have_pfMode = true;
        params = getCmdParam(params);
        continue;
      } else if (isAbbrev(params, "IGNORE"))   {
        pfMode  = PFMODE_IGNORE;
        have_pfMode = true;
        params = getCmdParam(params);
        continue;
      } else if (isAbbrev(params, "CLEAR" ))   {
        clear = true;
        pfMode  = PFMODE_CLEAR ;
        have_pfMode = true;
        params = getCmdParam(params);
        continue;
      } else if (isAbbrev(params, "BOTH" ))   {
        pfMode  = PFMODE_BOTH ;
        have_pfMode = true;
        params = getCmdParam(params);
        continue;
      } else if (isAbbrev(params, "TWICE" ))   {
        pfMode  = PFMODE_TWICE ;
        have_pfMode = true;
        params = getCmdParam(params);
        continue;
      } else {
        ; /* nop */
      }
    } /* if (!have_pfMode) */
    if (!have_pfNo) {
      /* no option word recognized in this iteration, must be PF Key number now */
      if (tryParseInt(params, &pfNo)) {
        have_pfNo     = true;
        params = getCmdParam(params);
      } else {
        strcpy(msg, "PF Key number must be numeric");
        return _rc_invalid;
      }
      if (pfNo < 0 || pfNo > 24) {
        strcpy(msg, "PF Key number must be 0 .. 24 (0=ENTER)");
        return _rc_invalid;
      }
      continue;
    }
    /* nothing recognized ? then we are done */
    break;
  /* option loop */ }

  if (clear) {
    if (forFsList) {
      setFSLPFKey(pfNo, NULL);
    } else if (forFsView) {
      setFSVPFKey(pfNo, NULL);
    } else if (forFsHelp) {
      setFSHPFKey(pfNo, NULL);
    } else {
      setPF(scr, pfScope, pfMode, pfNo, NULL);
    }
    checkNoParams(params, msg);
    return _rc_success;
  }

  if (strlen(params) > CMDLINELENGTH) {
    sprintf(msg, "Command line for PF Key too long (max. %d chars)",
            CMDLINELENGTH);
    return _rc_invalid;
  }

  if (forFsList) {
    setFSLPFKey(pfNo, params);
  } else if (forFsView) {
    setFSVPFKey(pfNo, params);
  } else if (forFsHelp) {
    setFSHPFKey(pfNo, params);
  } else {
    setPF(scr, pfScope, pfMode, pfNo, params);
  }
  return _rc_success;
}

static int CmdSqmetColor(ScreenPtr scr, char sqmet, char *params, char *msg) {
  bool our_name_is_BE = isAbbrev(params, "COLOUr");  /* else use "COLOR" */
  params = getCmdParam(params); /* skip COLOr/COLOUr/ATTRibute */
  int set_attr    = 0;
  int set_HiLit   = 0;
  int set_intens  = 0;
  unsigned char attr  = DA_Mono;
  unsigned char HiLit = HiLit_None;

  char *whatName = params;
  char whatTokenLen = getToken(params, ' ');
  if (whatTokenLen == 0) {
    sprintf(msg, "Missing screen object for SET %s",(our_name_is_BE ? "COLOUR" : "COLOR"));
    return _rc_error;
  }

  int i = 0;
  while (i++ < 4) {
    params =  getCmdParam(params);
    whatTokenLen = getToken(params, ' ');
    if (whatTokenLen == 0) {
      if (i > 1) break;
      sprintf(msg, "Missing %s/highlight parameter for SET %s",
                       (our_name_is_BE ? "colour" : "color"),
                       (our_name_is_BE ? "COLOUR" : "COLOR"));
      return _rc_error;
    }
    if        (isAbbrev(params, "Blue")) {
      set_attr++; attr = DA_Blue;
    } else if (isAbbrev(params, "Red")) {
      set_attr++; attr = DA_Red;
    } else if (isAbbrev(params, "Pink")) {
      set_attr++; attr = DA_Pink;
    } else if (isAbbrev(params, "Green")) {
      set_attr++; attr = DA_Green;
    } else if (isAbbrev(params, "Turquoise")) {
      set_attr++; attr = DA_Turquoise;
    } else if (isAbbrev(params, "Yellow")) {
      set_attr++; attr = DA_Yellow;
    } else if (isAbbrev(params, "White")) {
      set_attr++; attr = DA_White;
    } else if (isAbbrev(params, "Mono")) {
      set_attr++; attr = DA_Mono;
    } else if (isAbbrev(params, "None")) {
      set_HiLit++; HiLit = HiLit_None;
    } else if (isAbbrev(params, "BLInk")) {
      set_HiLit++; HiLit = HiLit_Blink;
    } else if (isAbbrev(params, "REVvideo")) {      /* IBM wording from LCM+L's VM/SP 5 */
      set_HiLit++; HiLit = HiLit_Reverse;
    } else if (isAbbrev(params, "REVerse")) {
      set_HiLit++; HiLit = HiLit_Reverse;
    } else if (isAbbrev(params, "Underscore")) {
      set_HiLit++; HiLit = HiLit_Underscore;
    } else if (isAbbrev(params, "Underline")) {     /* IBM wording from LCM+L's VM/SP 5 */
      set_HiLit++; HiLit = HiLit_Underscore;
    } else if (isAbbrev(params, "High")) {          /* IBM wording from LCM+L's VM/SP 5 */
      set_intens++;
    } else if (isAbbrev(params, "HIlight")) {
      set_intens++;
    } else if (isAbbrev(params, "Nohigh")) {        /* IBM wording from LCM+L's VM/SP 5 */
      ;
    } else if (isAbbrev(params, "PS0")) {           /* IBM wording from LCM+L's VM/SP 5 */
      ;
    } else {
      sprintf(msg, "Invalid %s/highlight parameter for SET %s : %s",
                       (our_name_is_BE ? "colour" : "color"),
                       (our_name_is_BE ? "COLOUR" : "COLOR"), params);
      return _rc_error;
    }
  }

  if (set_attr > 1) {
    sprintf(msg, "%s parameter specified %d times",
                     (our_name_is_BE ? "Colour" : "Color"), set_attr);
    return _rc_error;
  }
  if (set_HiLit > 1) {
    sprintf(msg, "Extended highlighting parameter specified %d times", set_HiLit);
    return _rc_error;
  }
  if (set_intens > 1) {
    sprintf(msg, "Intensity parameter specified %d times", set_intens);
    return _rc_error;
  } else if (set_intens == 1) {
    set_attr = 1;
    attr |= (unsigned char)0x01;
  }


  if ( (isAbbrev(whatName, "ALL")) || ((whatName[0] == '*') && (whatName[1] == ' ')) ) {
    if (set_attr  == 1) {  scr->ed->view->attrArrow = attr         ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitArrow = HiLit        ; }
    if (set_attr  == 1) {  scr->ed->view->attrBlock = attr         ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitBlock = HiLit        ; }
    if (set_attr  == 1) {  scr->ed->view->attrCBlock = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCBlock = HiLit       ; }
    if (set_attr  == 1) {  scr->ed->view->attrCHighLight = attr    ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCHighLight = HiLit   ; }
    if (set_attr  == 1) {  scr->ed->view->attrCmd = attr           ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCmd = HiLit          ; }
    if (set_attr  == 1) {  scr->ed->view->attrCPrefix = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCPrefix = HiLit      ; }
    if (set_attr  == 1) {  scr->ed->view->attrCTofeof = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCTofeof = HiLit      ; }
    if (set_attr  == 1) {  scr->ed->view->attrCurLine = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCurLine = HiLit      ; }
    if (set_attr  == 1) {  scr->ed->view->attrEMPTY = attr         ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitEMPTY = HiLit        ; }
    if (set_attr  == 1) {  scr->ed->view->attrFilearea = attr      ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitFilearea = HiLit     ; }
    if (set_attr  == 1) {  scr->ed->view->attrFileToPrefix = attr  ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitFileToPrefix = HiLit ; }
    if (set_attr  == 1) {  scr->ed->view->attrFootLine = attr      ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitFootLine = HiLit     ; }
    if (set_attr  == 1) {  scr->ed->view->attrHeadLine = attr      ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitHeadLine = HiLit     ; }
    if (set_attr  == 1) {  scr->ed->view->attrHighLight = attr     ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitHighLight = HiLit    ; }
    if (set_attr  == 1) {  scr->ed->view->attrInfoLines = attr     ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitInfoLines = HiLit    ; }
    if (set_attr  == 1) {  scr->ed->view->attrMsg = attr           ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitMsg = HiLit          ; }
    if (set_attr  == 1) {  scr->ed->view->attrPending = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitPending = HiLit      ; }
    if (set_attr  == 1) {  scr->ed->view->attrPrefix = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitPrefix = HiLit       ; }
    if (set_attr  == 1) {  scr->ed->view->attrScaleLine = attr     ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitScaleLine = HiLit    ; }
    if (set_attr  == 1) {  scr->ed->view->attrSelectedLine = attr  ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitSelectedLine = HiLit ; }
    if (set_attr  == 1) {  scr->ed->view->attrShadow = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitShadow = HiLit       ; }
    if (set_attr  == 1) {  scr->ed->view->attrTabline = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitTabline = HiLit      ; }
    if (set_attr  == 1) {  scr->ed->view->attrTofeof = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitTofeof = HiLit       ; }
  } else if (isAbbrev(whatName, "Filearea")) {
    if (set_attr  == 1) {  scr->ed->view->attrFilearea = attr      ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitFilearea = HiLit     ; }
  } else if ((isAbbrev(whatName, "CUrline")) || (isAbbrev(whatName, "CUrrline"))) {
    if (set_attr  == 1) {  scr->ed->view->attrCurLine = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCurLine = HiLit      ; }
    if (set_attr  == 1) {  scr->ed->view->attrCBlock = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCBlock = HiLit       ; }
    if (set_attr  == 1) {  scr->ed->view->attrCTofeof = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCTofeof = HiLit      ; }
    if (set_attr  == 1) {  scr->ed->view->attrCHighLight = attr    ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCHighLight = HiLit   ; }
    if (set_attr  == 1) {  scr->ed->view->attrCPrefix = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCPrefix = HiLit      ; }
  } else if (isAbbrev(whatName, "PRefix")) {
    if (set_attr  == 1) {  scr->ed->view->attrPrefix = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitPrefix = HiLit       ; }
  } else if (isAbbrev(whatName, "GAPfill")) {
    if (set_attr  == 1) {  scr->ed->view->attrFileToPrefix = attr  ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitFileToPrefix = HiLit ; }
  } else if (isAbbrev(whatName, "Cmdline")) {
    if (set_attr  == 1) {  scr->ed->view->attrCmd = attr           ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCmd = HiLit          ; }
  } else if ((isAbbrev(whatName, "Arrow")) || (isAbbrev(whatName, "CMDARRow"))) {
    if (set_attr  == 1) {  scr->ed->view->attrArrow = attr         ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitArrow = HiLit        ; }
  } else if (isAbbrev(whatName, "Msglines")) {
    if (set_attr  == 1) {  scr->ed->view->attrMsg = attr           ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitMsg = HiLit          ; }
  } else if (isAbbrev(whatName, "INFOlines")) {
    if (set_attr  == 1) {  scr->ed->view->attrInfoLines = attr     ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitInfoLines = HiLit    ; }
  } else if ((isAbbrev(whatName, "Idline")) || (isAbbrev(whatName, "HEADline"))) {
    if (set_attr  == 1) {  scr->ed->view->attrHeadLine = attr      ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitHeadLine = HiLit     ; }
  } else if ((isAbbrev(whatName, "STatarea")) || (isAbbrev(whatName, "FOOTline"))) {
    if (set_attr  == 1) {  scr->ed->view->attrFootLine = attr      ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitFootLine = HiLit     ; }
  } else if (isAbbrev(whatName, "Scaleline")) {
    if (set_attr  == 1) {  scr->ed->view->attrScaleLine = attr     ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitScaleLine = HiLit    ; }
  } else if (isAbbrev(whatName, "HIGHlight")) {
    if (set_attr  == 1) {  scr->ed->view->attrHighLight = attr     ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitHighLight = HiLit    ; }
  } else if (isAbbrev(whatName, "SHadow")) {
    if (set_attr  == 1) {  scr->ed->view->attrShadow = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitShadow = HiLit       ; }
  } else if (isAbbrev(whatName, "SELECTEDLINE")) {
    if (set_attr  == 1) {  scr->ed->view->attrSelectedLine = attr  ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitSelectedLine = HiLit ; }
  } else if (isAbbrev(whatName, "Pending")) {
    if (set_attr  == 1) {  scr->ed->view->attrPending = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitPending = HiLit      ; }
  } else if (isAbbrev(whatName, "Tabline")) {
    if (set_attr  == 1) {  scr->ed->view->attrTabline = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitTabline = HiLit      ; }
  } else if (isAbbrev(whatName, "TOfeof")) {
    if (set_attr  == 1) {  scr->ed->view->attrTofeof = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitTofeof = HiLit       ; }
  } else if (isAbbrev(whatName, "Block")) {
    if (set_attr  == 1) {  scr->ed->view->attrBlock = attr         ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitBlock = HiLit        ; }
    if (set_attr  == 1) {  scr->ed->view->attrCBlock = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCBlock = HiLit       ; }
  } else if (isAbbrev(whatName, "CBlock")) {
    if (set_attr  == 1) {  scr->ed->view->attrCBlock = attr        ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCBlock = HiLit       ; }
  } else if (isAbbrev(whatName, "CTOfeof")) {
    if (set_attr  == 1) {  scr->ed->view->attrCTofeof = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCTofeof = HiLit      ; }
  } else if (isAbbrev(whatName, "CHIGHlight")) {
    if (set_attr  == 1) {  scr->ed->view->attrCHighLight = attr    ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCHighLight = HiLit   ; }
  } else if (isAbbrev(whatName, "CPRefix")) {
    if (set_attr  == 1) {  scr->ed->view->attrCPrefix = attr       ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitCPrefix = HiLit      ; }
  } else if (isAbbrev(whatName, "EMPTY")) {
    if (set_attr  == 1) {  scr->ed->view->attrEMPTY = attr         ; }
    if (set_HiLit == 1) { scr->ed->view->HiLitEMPTY = HiLit        ; }
  } else {
    sprintf(msg, "Invalid screen object for SET %s",
                   (our_name_is_BE ? "COLOUR" : "COLOR"));
    return _rc_error;
  }
  params =  getCmdParam(params);
  checkNoParams(params, msg);
  return _rc_success;
}

static int CmdRecfm(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  char recfm;
  if (isAbbrev(params, "V")) {
    recfm = 'V';
    params =  getCmdParam(params);
    checkNoParams(params, msg);
  } else if (isAbbrev(params, "F")) {
    recfm = 'F';
    params =  getCmdParam(params);
    checkNoParams(params, msg);
  } else {
    strcpy(msg, "Recfm must be 'V' or 'F'");
    return false;
  }

  setRecfm(scr->ed, recfm);
  return false;
}



static bool tokcmp(char *s1, char *s2) {
  int i  = 0;
  int l1 = getToken(s1, ' ');
  int l2 = getToken(s2, ' ');
  if (l1 != l2) return true;
  for (i = 1; i <= l1; i++) {
    if (*(s1++) != *(s2++)) return true;
  }
  return false;
}


static int CmdLrecl(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int lrecl;
  if (!tokcmp(params, "*")) {
    lrecl = 255;
    params = getCmdParam(params);
  } else   if (tryParseInt(params, &lrecl)) {
    params =  getCmdParam(params);
  } else {
    sprintf(msg, "LRECL operand must be numeric: %s", params);
    return _rc_error;
  }
  if (lrecl < 1 || lrecl > 255) {
    strcpy(msg, "LRECL must be 1 .. 255");
    return _rc_error;
  }
  bool truncated = setLrecl(scr->ed, lrecl);
  /* 2024-12-27 do not show confirmation message if nothing was truncated */
  if (truncated) { sprintf(msg, "LRECL changed to %d%s",
                           lrecl, ", some line(s) were truncated"); }
  checkNoParams(params, msg);
  return false;
}

static int CmdWorkLrecl(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int lrecl;
  if (!tokcmp(params, "*")) {
    lrecl = 255;
    params = getCmdParam(params);
  } else if (tryParseInt(params, &lrecl)) {
    params =  getCmdParam(params);
  } else {
    sprintf(msg, "WORKLRECL operand must be numeric: %s", params);
    return _rc_error;
  }
  if (lrecl < 1 || lrecl > 255) {
    strcpy(msg, "WORKLRECL must be 1 .. 255");
    return _rc_error;
  }
  setWorkLrecl(scr->ed, lrecl); /* 2024-12-27 do not show confirmation message */
  /* sprintf(msg, "Working LRECL changed to %d", getWorkLrecl(scr->ed)); */
  checkNoParams(params, msg);
  return false;
}


static int CmdUnbinary(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  bool b;
  if (b = resetIsBinary(scr->ed)) {
    strcpy(msg,
      "Removed BINARY flag, saving this file will destroy binary content");
  } else {
    strcpy(msg,
      "BINARY flag already removed");
  }
  checkNoParams(params, msg);
  return !b;
}

static int CmdUnHide(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  bool b;
  if (b = resetIsHidden(scr->ed)) {
    strcpy(msg,
      "File is not hidden anymore");
  } else {
    strcpy(msg,
      "File is not hidden");
  }
  checkNoParams(params, msg);
  return !b;
}

static int CmdHide(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  bool b;
  if (b = setIsHidden(scr->ed)) {
    strcpy(msg,
      "File is already hidden");
  } else {
    strcpy(msg,
      "File is now hidden");
  }
  checkNoParams(params, msg);
  return b;
}

static int CmdFtDefaults(ScreenPtr scr, char *params, char *msg) {
  char ft[9];
  char recfm;
  char caseMode;
  int lrecl;
  int workLrecl;

  int tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen == 0) {
    strcpy(msg, "Missing filetype for FTDEFAULTS");
    return false;
  }
  tokLen = minInt(8, tokLen);
  memset(ft, '\0', sizeof(ft));
  strncpy(ft, params, tokLen);

  params = getCmdParam(params);
  tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen != 1) {
    strcpy(msg, "Missing or invalid RECFM for FTDEFAULTS");
    return false;
  }
  recfm = c_upper(*params);
  if (recfm != 'V' && recfm != 'F') {
    strcpy(msg, "Invalid RECFM for FTDEFAULTS (not V or F)");
    return false;
  }

  params = getCmdParam(params);
  if (tryParseInt(params, &lrecl)) {
    if (lrecl < 1 || lrecl > 255) {
      strcpy(msg, "LRECL for FTDEFAULTS must be 1..255");
      return false;
    }
  } else {
    strcpy(msg, "Missing or invalid LRECL for FTDEFAULTS");
    return false;
  }
  workLrecl = lrecl;

  params = getCmdParam(params);
  tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen != 1) {
    strcpy(msg, "Missing or invalid CASEMODE for FTDEFAULTS");
    return false;
  }
  caseMode = c_upper(*params);
  if (caseMode != 'U' && caseMode != 'M' && caseMode != 'R') {
    strcpy(msg, "Invalid CASEMODE for FTDEFAULTS (not U or M or R)");
    return false;
  }

  params = getCmdParam(params);
  if (params && tryParseInt(params, &workLrecl)) {
    if (workLrecl < 1 || workLrecl > 255) {
      strcpy(msg, "WORKLRECL for FTDEFAULTS must be 1..255, usingf LRECL");
      workLrecl = lrecl;
    }
  }

  addFtDefault(ft, lrecl, recfm, caseMode, workLrecl);
  return false;
}

static int CmdGapFill(ScreenPtr scr, char *params, char *msg) {
  char fillChar;
  if (isAbbrev(params, "NONE")) {
    fillChar = (char)0x00;
    params =  getCmdParam(params);
  } else if (isAbbrev(params, "DOT")) {
    fillChar = (char)0xB3;
    params =  getCmdParam(params);
  } else if (isAbbrev(params, "DASH")) {
    fillChar = '-';
    params =  getCmdParam(params);
  } else if (isAbbrev(params, "CROSS")) {
    fillChar = (char)0xBF;
    params =  getCmdParam(params);
  } else {
    strcpy(msg, "Invalid VALUE for GAPFILL (not NONE, DOT, DASH, CROSS)");
    return false;
  }
  checkNoParams(params, msg);
  scr->ed->view->fileToPrefixFiller = fillChar;
  return false;
}

static CmdDef allowedCmsCommands[] = {
  {"IDentify", NULL}, {"MAKEBUF", NULL}, {"DROPBUF", NULL}, {"SENTRIES", NULL},
  {"ACcess", NULL}, {"CLOSE", NULL},
  {"CP", NULL},
  {"DETACH", NULL}, {"ERASE", NULL},    {"EXEC", NULL},
  {"LINK", NULL},   {"Listfile", NULL}, {"PRint", NULL},   {"PUnch", NULL},
  {"Query", NULL},  {"READcard", NULL}, {"RELease", NULL}, {"Rename", NULL},
  {"SET", NULL},    {"STATEw", NULL},   {"TAPE", NULL},    {"Type", NULL}
};

static int CmdCms(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  t_PGMB *PGMB_loc = CMSGetPG();
  if (!params || !*params) {
    int rc = CMScommand("SUBSET", CMS_CONSOLE);
    return false;
  }

  /* no restrictions if EE is loaded in high memory */
  if ((&LoadPoint == 0x020000) && !findCommand(
         params,
         allowedCmsCommands,
         sizeof(allowedCmsCommands) / sizeof(CmdDef))) {
    strcpy(msg,
      "CP/CMS command not allowed inside EE, allowed commands are:\n"
      "  ACcess  CLOSE  CP  DETACH  ERASE  LINK  Listfile  PRint  PUnch\n"
      "  Query  READcard  RELease  Rename  SET  STATEw  TAPE  Type");
    return false;
  }

  /* CMS EXEC ? prepare for SC@ENTRY : see EESUBCOM ASSEMBLE */
  unsigned long R13;
  __asm__("LR %0,13"    : "=d" (R13));
  PGMB_loc->cmscrab = R13;
/* preliminary */  PGMB_loc->saveScreenPtr = scr;
/* preliminary */  PGMB_loc->saveMsgPtr    = msg;

  int rc = CMScommand(params, CMS_CONSOLE);
  int stacked = CMSstackQuery();
  int makebuf_rc = CMScommand("MAKEBUF", CMS_CONSOLE);
                   CMScommand("DROPBUF", CMS_CONSOLE);
  sprintf(msg, "CMS command executed -> RC = %d, stack entries = %d, buffer index = %d ",
                                        rc,      stacked,            makebuf_rc-1 );
  return rc;
}

static bool getEEBufName(char **paramsPtr, char *fn, char *ft, char *fm) {
  char defaultMode[3];
  strcpy(defaultMode, "A1");
  char *defMode = getWritableFilemode(defaultMode);

  char *lastCharRead = NULL;
  int consumed = 0;
  int parseRes = parse_fileid(
        paramsPtr, 0, 1,
        fn, ft, fm, &consumed,
        "PUT", "EE$BUF", defMode,
        &lastCharRead, NULL);
  if (parseRes == PARSEFID_OK) {
    *paramsPtr = lastCharRead;
  } else {
    strcpy(fn, "PUT");
    strcpy(ft, "EE$BUF");
    strcpy(fm, defMode);
  }

  return (strcmp(ft, "EE$BUF") == 0 && strcmp(fm, defMode) == 0);
}

static bool getLineRange(
    ScreenPtr scr,
    int lineCount,
    LinePtr *from,
    LinePtr *to) {
  EditorPtr ed = scr->ed;

  int fileLineCount;
  int currLineNo;
  getLineInfo(ed, &fileLineCount, &currLineNo);
  if (currLineNo == 0) {
    if (lineCount == 1) {
      *from = NULL;
      *to = NULL;
      return false;
    } else {
      moveDown(ed, 1);
      lineCount--;
    }
  }

  LinePtr fromLine = getCurrentLine(ed);
  LinePtr toLine = fromLine;
  LinePtr tmpLine;

  if (lineCount > 0) {
    while(lineCount > 1) {
      tmpLine = getNextLine(ed, toLine);
      if (tmpLine) {
        toLine = tmpLine;
        lineCount--;
      } else {
        lineCount = 0;
      }
    }
  } else {
     while(lineCount < -1) {
      tmpLine = getPrevLine(ed, fromLine);
      if (tmpLine) {
        fromLine = tmpLine;
        lineCount++;
      } else {
        lineCount = 0;
      }
    }
  }

  if (fromLine == NULL && toLine != NULL) {
    fromLine = getNextLine(ed, NULL);
  }

  *from = fromLine;
  *to = toLine;

  return (fromLine != NULL && toLine != NULL);
}



static int CmdPutInner(
    ScreenPtr scr,
    char *params,
    char *msg,
    bool forceOvr,
    bool deleteLines) {
  if (!scr->ed) { return _rc_failure; }

  char fn[9];
  char ft[9];
  char fm[3];
  char target[256];

/* XEDIT expects a target here, not just a line count */
/*
  int lineCount = 1;

  int tokLen = getToken(params, ' ');
  if (params && *params && tokLen > 0) {
    if (tryParseInt(params, &lineCount)) {
      params = getCmdParam(params);
    } else {
      strcpy(msg, "Invalid parameter linecount specified");
      return false;
    }
  }
  if (lineCount == 0) {
    strcpy(msg, "Linecount = 0 specified, no action taken");
    return false;
  }
*/

  EditorPtr ed = scr->ed;

  /* we need to separate 'target' from 'fn ft fm' */
  int val;
  char buffer[2048];
  char *params2 = params;
  char *params1 = params;
  int locType = parseLocation(&params2, &val, buffer);
  /* val,buffer ignored here */
  if (IS_LOC_ERROR(locType)) {
    /* let the 'locate' command display the error message */
    return CmdLocate(scr, params, msg);
  }

  buffer[0] = '\0';
  int i = 0;
  if (params != params2) {
    /* valid 'target', not yet searched for */
    while (params < params2) {
      buffer[i++] = *params++;
    }
    buffer[i] = '\0';
  }

/*
  sprintf(msg,"Debug STOP PUT: params1=%08x params2=%08x params2-params1=%d i=%d locType=%d buffer=%s\n",
                               params1,     params2,     params2-params1,   i,   locType,   buffer );
  return 777;
*/

  int fromLineNo = getCurrLineNo(ed);  /* where are we now ? */
  LinePtr fromLine = getLineAbsNo(ed, fromLineNo);

  int rc = CmdLocate(scr, buffer, msg);
  if ((rc > 1) || (rc < 0)) return rc;

  int   toLineNo = getCurrLineNo(ed);  /* where do we go ? */
  LinePtr   toLine = getLineAbsNo(ed,   toLineNo);


  /* PUT writes lines up to, but not including the target line */
  if ( toLineNo < fromLineNo) {
    /* backward target */
    toLine = moveDown(ed, 1);      /* one line towards EOF */
    toLineNo = getCurrLineNo(ed);
  } else if ( toLineNo > fromLineNo) {
    /* forward target */
    toLine = moveUp(ed, 1);        /* one line towards TOF */
    toLineNo = getCurrLineNo(ed);
  }

  forceOvr |= getEEBufName(&params, fn, ft, fm);

  int outcome = writeFileRange(
        ed,
        fn, ft, fm,
        forceOvr,
        true,     /* selectiveEditing */
        fromLine, toLine,
        msg);

  if (outcome == 0 && deleteLines) {
    deleteLineRange(ed, fromLine, toLine);
  }

  checkNoParams(params, msg);
  return false;
}

static int CmdPut(ScreenPtr scr, char *params, char *msg) {
  return CmdPutInner(scr, params, msg, false, false);
}

static int CmdPPut(ScreenPtr scr, char *params, char *msg) {
  return CmdPutInner(scr, params, msg, true, false);
}

static int CmdPutD(ScreenPtr scr, char *params, char *msg) {
  return CmdPutInner(scr, params, msg, false, true);
}

static int CmdPPutD(ScreenPtr scr, char *params, char *msg) {
  return CmdPutInner(scr, params, msg, true, true);
}

static int CmdGetInner(
    ScreenPtr scr,
    char *params,
    char *msg,
    bool deleteSource) {
  if (!scr->ed) { return false; }
  EditorPtr ed = scr->ed;

  int fileLineCountBefore;
  int currLineNo;
  getLineInfo(ed, &fileLineCountBefore, &currLineNo);

  char fn[9];
  char ft[9];
  char fm[3];

  deleteSource &= getEEBufName(&params, fn, ft, fm);
  int outcome = readFile(ed, fn, ft, fm, msg);

  int fileLineCountAfter;
  getLineInfo(ed, &fileLineCountAfter, &currLineNo);

  if (outcome == 0) {
    sprintf(msg, "Inserted %d lines from file %s %s %s",
            fileLineCountAfter - fileLineCountBefore,
            fn, ft, fm);
    if (deleteSource) {
      char fid[19];
      sprintf(fid, "%-8s%-8s%s", fn, ft, fm);
      outcome = CMSfileErase(fid);
      strcat(msg, "\n");
      msg += strlen(msg);
      if (outcome == 0) {
        sprintf(msg, "File %s %s %s dropped", fn, ft, fm);
      } else {
        sprintf(msg, "Unable to drop file %s %s %s", fn, ft, fm);
      }
    }
  }

  checkNoParams(params, msg);
  return false;
}

static int CmdGet(ScreenPtr scr, char *params, char *msg) {
  return CmdGetInner(scr, params, msg, false);
}

static int CmdGetD(ScreenPtr scr, char *params, char *msg) {
  return CmdGetInner(scr, params, msg, true);
}

static int CmdDelete(ScreenPtr scr, char *params, char *msg) {
  int lineCount = 1;

  int tokLen = getToken(params, ' ');
  if (params && *params && tokLen > 0) {
    if (tryParseInt(params, &lineCount)) {
      params = getCmdParam(params);
    } else {
      strcpy(msg, "Invalid parameter linecount specified");
      return false;
    }
  }
  if (lineCount == 0) {
    strcpy(msg, "Linecount = 0 specified, no action taken");
    return false;
  }

  LinePtr fromLine;
  LinePtr toLine;
  if (!getLineRange(scr, lineCount, &fromLine, &toLine)) {
    strcpy(msg, "Deleting Top of File not possible, no action taken");
    return false;
  }
  deleteLineRange(scr->ed, fromLine, toLine);

  checkNoParams(params, msg);
  return false;
}

static int shiftBy = 2;
static int shiftMode = SHIFTMODE_MIN;

int gshby() {
  return shiftBy;
}

int gshmode() {
  return shiftMode;
}

static bool /*valid?*/ parseShiftMode(
    char *param,
    int *mode,
    char *msg,
    bool required) {
  if (!param || !*param) {
    if (required) {
      strcpy(msg, "Missing shift mode parameter");
      return false;
    }
    return true;
  }
  if (isAbbrev(param, "CHEckall")) {
    *mode = SHIFTMODE_IFALL;
  } else if (isAbbrev(param, "MINimal")) {
    *mode = SHIFTMODE_MIN;
  } else if (isAbbrev(param, "LIMit")) {
    *mode = SHIFTMODE_LIMIT;
  } else if (isAbbrev(param, "TRUNCate")) {
    *mode = SHIFTMODE_TRUNC;
  } else {
    strcpy(msg,
      "Invalid shift mode specified (CHEckall, MINimal, LIMit, TRUNCate)");
    return false;
  }
  return true;
}

/* SHIFTCONFig mode [shiftBy] */
static int CmdShiftConfig(ScreenPtr scr, char *params, char *msg) {
  parseShiftMode(params, &shiftMode, msg, true);
  params = getCmdParam(params);
  int defaultBy = shiftBy;
  if (tryParseInt(params, &defaultBy)) {
    if (defaultBy > 0 && defaultBy < 10) {
      shiftBy = defaultBy;
    } else {
      strcpy(msg, "Shiftconfig: <shiftBy> must be in range 1..9");
    }
    params = getCmdParam(params);
  }
  checkNoParams(params, msg);
  return false;
}

/* SHift [<by>] Left|Right [<count>|:<line>|.<mark>] [mode] */
static int CmdShift(ScreenPtr scr, char *params, char *msg) {
  EditorPtr ed = scr->ed;
  if (!ed) { return false; }
  int by = shiftBy;
  bool toLeft = false;
  LinePtr fromLine = getCurrentLine(ed);
  LinePtr toLine = NULL;
  int mode = shiftMode;

  int number;
  int tokLen = getToken(params, ' ');
  if (params && *params && tokLen > 0) {
    if (tryParseInt(params, &by)) {
      if (by < 0) {
        strcpy(msg, "Shift: <by> must be greater 0");
        return false;
      }
      params = getCmdParam(params);
    }
    if (isAbbrev(params, "Left")) {
      toLeft = true;
      params = getCmdParam(params);
    } else if (isAbbrev(params, "Right")) {
      toLeft = false;
      params = getCmdParam(params);
    } else {
      strcpy(msg, "Shift: direction must be Left or Right.");
      return false;
    }
    if (*params == '.') {
      toLine = getLineMark(ed, &params[1], msg);
      if (!toLine) { return false; }
      params = getCmdParam(params);
    } else if (*params == ':') {
      if (tryParseInt(&params[1], &number)) {
        toLine = getLineAbsNo(ed, number);
        if (!toLine) {
          strcpy(msg, "Shift: invalid absolute line number");
          return false;
        }
      } else {
        strcpy(msg, "Shift: invalid absolute line number");
        return false;
      }
      params = getCmdParam(params);
    } else if (tryParseInt(params, &number)) {
      int currLineNo = getCurrLineNo(ed);
      int otherLineNo = currLineNo + number;
      otherLineNo = minInt(maxInt(1, otherLineNo), getLineCount(ed));
      toLine = getLineAbsNo(ed, otherLineNo);
      params = getCmdParam(params);
    } else {
      toLine = fromLine;
    }
    if (!parseShiftMode(params, &mode, msg, false)) {
      params = getCmdParam(params);
      checkNoParams(params, msg);
      return false;
    }
    params = getCmdParam(params);
  } else {
    strcpy(msg, "Shift: missing parameters");
    return false;
  }

  int outcome;
  if (toLeft) {
    outcome = shiftLeft(ed, fromLine, toLine, by, mode);
  } else {
    outcome = shiftRight(ed, fromLine, toLine, by, mode);
  }
  if (outcome == 1) {
    strcpy(msg,
      "Shift: line(s) would be truncated, use MINimal, LIMit or TRUNCate");
  } else if (outcome == 2) {
    strcpy(msg, "Line(s) truncated");
  }

  checkNoParams(params, msg);
  return false;
}

static int CmdFSList(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }

  char fn[9];
  char ft[9];
  char fm[3];

  char fnOut[9];
  char ftOut[9];
  char fmOut[3];

  getFnFtFm(scr->ed, fnOut, ftOut, fmOut);
  /* printf("getFnFtFm -> %s %s %s\n", fnOut, ftOut, fmOut); */
  int tokLen = getToken(params, ' ');
  int parseRes = PARSEFID_NONE;
  if (params && *params && tokLen > 0) {
    char *lastCharRead = NULL;
    int consumed = 0;
    parseRes = parse_fileid(
          &params, 0, 1,
          fn, ft, fm, &consumed,
          fnOut, ftOut, fmOut,
          &lastCharRead, msg);
    if (parseRes != PARSEFID_OK && parseRes != PARSEFID_NONE) {
      return false;
    }
  }
  if (parseRes == PARSEFID_NONE) {
    strcpy(fn, "*");
    strcpy(ft, "*");
    strcpy(fm, "A");
  }
  /*
  printf("CmdFslist: pattern: %s %s %s , dflt: %s %s %s\n",
    fn, ft, fm, fnOut, ftOut, fmOut);
  */
  int fslistRc = doFSList(fn, ft, fm, fnOut, ftOut, fmOut, msg, 0);
  if (fslistRc == RC_FILESELECTED) {
    int state;
    openFile(scr, fnOut, ftOut, fmOut, &state, msg);
  }
  return false;
}

static int CmdTabBackward(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (scr->cElemType == 2) {
    int oldOffset = scr->cElemOffset;
    int tabs[MAX_TAB_COUNT];
    int tabCount = getTabs(scr->ed, tabs);
    int i;

    scr->cursorPlacement = 2;
    scr->cursorOffset = oldOffset;
    scr->cursorLine = scr->cElem;

    for (i = tabCount - 1; i >= 0; i--) {
      if (tabs[i] < oldOffset) {
        scr->cursorOffset = tabs[i];
        return false;
      }
    }
  }
  scr->cursorOffset = 0;
  return false;
}

static int CmdTabForward(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (scr->cElemType == 2) {
    int oldOffset = scr->cElemOffset;
    int tabs[MAX_TAB_COUNT];
    int tabCount = getTabs(scr->ed, tabs);
    int i;
    /*
    sprintf(msg, "Tab -> cElemOffset: %d, [%d] %d %d %d %d",
        scr->cElemOffset, tabCount, tabs[0], tabs[1], tabs[2], tabs[3]);
    */

    scr->cursorPlacement = 2;
    scr->cursorOffset = oldOffset;
    scr->cursorLine = scr->cElem;

    for (i = 0; i < tabCount; i++) {
      if (tabs[i] > oldOffset) {
        scr->cursorOffset = tabs[i];
        return false;
      }
    }

    /*int newOffset = scr->cElemOffset + 6;
    scr->cursorPlacement = 2;
    scr->cursorOffset = newOffset;
    scr->cursorLine = scr->cElem;*/
  } else if (scr->cElemType == 0) {
    /*strcpy(msg, "trying to move cursor to file area");*/
    scr->cursorPlacement = 2;
    scr->cursorOffset = 0;
    scr->cursorLine = getCurrentLine(scr->ed);
    if (scr->cursorLine == NULL) {
      /*strcat(msg, ", moving to first");*/
      scr->cursorLine = getFirstLine(scr->ed);
      if (scr->cursorLine == NULL) {
        /*strcat(msg, ", STILL NULL !!");*/
      }
    } else if (getCurrLineNo(scr->ed) == 0) {
      /*strcat(msg, ", moving to line after current");*/
      scr->cursorLine = getNextLine(scr->ed, scr->cursorLine);
    }
    if (scr->firstLineVisible != NULL
        && scr->lastLineVisible != NULL
        && scr->ed->clientdata1 != NULL) {
      LinePtr trgLine = (LinePtr)scr->ed->clientdata1;
      int offset = (int)scr->ed->clientdata2;
      /*strcat(msg, "\n->visible area available");*/
      LinePtr curr = scr->firstLineVisible;
      LinePtr guard = getNextLine(scr->ed, scr->lastLineVisible);
      while(curr != guard && curr != NULL) {
        if (curr == trgLine) {
          scr->cursorOffset = offset;
          scr->cursorLine = trgLine;
          /*strcat(msg, "\n->trgLine found...");*/
          break;
        }
        curr = getNextLine(scr->ed, curr);
      }
    }
  }
  return false;
}

static bool parseTabs(char *params, int *tabs, int *count) {
  bool someIgnored = false;

  memset(tabs, '\0', sizeof(int) * MAX_TAB_COUNT);

  int number;
  int currTab = 0;
  while (params && *params && currTab < MAX_TAB_COUNT) {
    if (tryParseInt(params, &number) && number > 0 && number <= MAX_LRECL) {
      tabs[currTab++] = number - 1;
    } else {
     someIgnored = true;
    }
    params = getCmdParam(params);
  }

  *count = currTab;
  return someIgnored;
}

static int CmdTabs(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int tabs[MAX_TAB_COUNT];
  int tabCount;
  bool someIgnored = parseTabs(params, tabs, &tabCount);
  if (someIgnored) {
    strcpy(msg, "Some invalid tab positions were ignored");
    if (tabCount > 0) {
      setTabs(scr->ed, tabs);
    } else {
      strcat(msg, "\nNo valid tab positions defined, command aborted");
    }
  } else {
    setTabs(scr->ed, tabs);
  }
  return false;
}

static int CmdFtTabs(ScreenPtr scr, char *params, char *msg) {
  char ft[9];
  char recfm;
  char caseMode;
  int lrecl;

  int tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen == 0) {
    strcpy(msg, "Missing filetype for FTDEFAULTS");
    return false;
  }

  tokLen = minInt(8, tokLen);
  memset(ft, '\0', sizeof(ft));
  strncpy(ft, params, tokLen);

  params = getCmdParam(params);

  int tabs[MAX_TAB_COUNT];
  int tabCount;
  bool someIgnored = parseTabs(params, tabs, &tabCount);
  if (someIgnored) {
    strcpy(msg, "FTTABS: Some invalid tab positions were ignored");
    if (tabCount == 0) {
      strcat(msg, "\nFTTABS: No valid tab positions defined, command ignored");
    }
  }
  addFtTabs(ft, tabs);
  return false;
}

static int CmdHelp(ScreenPtr scr, char *params, char *msg) {
  checkNoParams(params, msg);
  doHelp("$EE", msg);
  return false;
}

typedef struct _lockblock {
  struct _lockblock *next;
  char dummy[40956];
} LockBlock, *LockBlockPtr;

static LockBlockPtr lockedMem = NULL;

static int CmdMemLock(ScreenPtr scr, char *params, char *msg) {
  int count = 0;
  LockBlockPtr block = (LockBlockPtr)allocMem(sizeof(LockBlock));
  while(block) {
    count++;
    block->next = lockedMem;
    lockedMem = block;
    block = (LockBlockPtr)allocMem(sizeof(LockBlock));
  }
  sprintf(msg, "locked %d blocks ~ %d netto bytes",
          count, count*sizeof(LockBlock));
  return false;
}

static int CmdMemUnLock(ScreenPtr scr, char *params, char *msg) {
  int count = 0;
  while(lockedMem) {
    count++;
    LockBlockPtr next = lockedMem->next;
    freeMem(lockedMem);
    lockedMem = next;
  }
  sprintf(msg, "unlocked %d blocks ~ %d netto bytes",
          count, count*sizeof(LockBlock));
  return false;
}







static void dump_mem(char *msg_temp, unsigned long r)  {
  unsigned long r_local = r;

  unsigned char msg_temp1[4096];
  unsigned char msg_temp2[4096];
  unsigned char msg_temp3[4096];
  msg_temp1[0] = '\0';
  msg_temp2[0] = '\0';
  msg_temp3[0] = '\0';
  char *p1 = &msg_temp1[0];
  char *p2 = &msg_temp2[0];
  char *p3 = &msg_temp3[0];




  int i = 0;
  unsigned char *pc =  /* (char*) */ r_local;
  if ((r >= 0) && (r <= 0x00FFFFF0)) while (++i <= (16)) {
    unsigned char c = *pc++;
      sprintf(p1," %02x",c); p1++; p1++; p1++;
    if ((c >= 0x40) && (c < 0xFF)) {
      *p2++ = c;
    } else {
      *p2++ = '.';
    }
   if (!(i&3))  /* 4,8,12 */ {
       *p1++ = ' ';
       *p2++ = ' ';
   }
  }
       *p1++ = '\0';
       *p2++ = '\0';
    sprintf(msg_temp,"%s\n%08x : %s   %s",msg_temp,r_local,msg_temp1,msg_temp2);
}


static int CmdMemoryDump(ScreenPtr scr, char *params, char *msg) {
  int loc = 0;
  if (!tryParseHex(params, &loc)) return _rc_error;
  sprintf(msg, "Memory dump at %08x:", loc);
  int i=0;
  while (++i <= 8) {
    dump_mem(msg, loc);
    loc = loc +16;
  }
  return _rc_success;
}


static int CmdDebug(ScreenPtr scr, char *params, char *msg) {
  t_PGMB *PGMB_loc = CMSGetPG();
  char msg_temp[2048];
  msg_temp[0] = '\0';

/*
  void *v = &EE_DIRTY;
  void *p = &EE_PLIST;
  unsigned long *l = v;
  unsigned long R0 = *l++;
  unsigned long R1 = *l++;
  unsigned long R2 = *l++;
  unsigned char R1_flag = R1>>24;
  R1 = R1 & 0x00FFFFFF;
  sprintf(msg_temp,"EE_DIRTY:  R1_flag=X'%02x'  R0=X'%08x'  R1=X'%08x'  R2=X'%08x' ", R1_flag, R0, R1, R2);
*/

/*
  unsigned long *R0p = R0;
 if (R0p) {
  unsigned long R0a = *(R0p++);
  unsigned long R0b = *(R0p++);
  unsigned long R0c = *(R0p++);
  unsigned long R0d = *(R0p++);
  sprintf(msg_temp,"%s\nEE_DIRTY:  R0a=X'%08x'  R0b=X'%08x'  R0c=X'%08x'  R0d=X'%08x' ", msg_temp, R0a, R0b, R0c, R0d);
  dump_mem(msg_temp, R0a);
  dump_mem(msg_temp, R0b);
  dump_mem(msg_temp, R0c);
  dump_mem(msg_temp, R0d);
 }
*/



/*
  unsigned long *R1p = R1;
  /* R1p = R0;     * /   /* QUICK AND DIRTY - we want to see the EPLIST * /
  /* R1p = *R1p;   * /   /* QUICK AND DIRTY - we want to see the EPLIST * /
 if (R1p) {
  unsigned long R1a = *(R1p++);
  unsigned long R1b = *(R1p++);
  unsigned long R1c = *(R1p++);
  unsigned long R1d = *(R1p++);
  sprintf(msg_temp,"%s\nEE_DIRTY:  R1a=X'%08x'  R1b=X'%08x'  R1c=X'%08x'  R1d=X'%08x' ", msg_temp, R1a, R1b, R1c, R1d);
  dump_mem(msg_temp, R1a);
  dump_mem(msg_temp, R1b);
  dump_mem(msg_temp, R1c);
  dump_mem(msg_temp, R1d);
 }
*/


/*
  sprintf(msg,"CmdDebug 2022-11-11-0321 : &EE_DIRTY = X'%08x'   &EE_PLIST = X'%08x'\n%s",v,p,msg_temp);
*/

/*
  int i;
  for (i=0;i</ *25* /13;i++) {
    printf("%d global %s\n",i,PGMB_loc->pfCmds[i]) ;
    printf("%d view   %s\n",i,scr->ed->view->pfCmds[i]) ;
  }
*/

/*
  int i;
  for (i=0;i<INFOLINES_MAX;i++) {
    printf("%d %8x %s\n", i, scr->ed->view->infoLines_p[i], scr->ed->view->infoLines[i]) ;
  }
*/

  scr->ed->view->showTofBof = false;

  return false;


}




static int CmdSqmetScope(ScreenPtr scr, char sqmet, char *params, char *msg) {
  params =  getCmdParam(params);   /* skip 'SCOPE' */
  EditorPtr ed = scr->ed;

  if (sqmet == 'Q') {
    sprintf(msg, "SCOPE %s", (getScope(ed) ? "All" : "Display"));
    return _rc_success;
  }

  if (isAbbrev(params, "All")) {
    setScope(ed, true);
    sParadox(ed, false);
  } else if (isAbbrev(params, "Display")) {
    setScope(ed, false);;
    sParadox(ed, false);
  } else if (isAbbrev(params, "Paradox")) {
    setScope(ed, false);
    sParadox(ed, true);
  } else {
    strcpy(msg, "Invalid operand for SET SCOPE: 'All' or 'Display' expected");
    return false;
  }

  params =  getCmdParam(params);   /* skip 'All/Display' */
  checkNoParams(params, msg);

  return false;
}




static int CmdSqmetSelect(ScreenPtr scr, char sqmet, char *params, char *msg) {
/* This implementation only works on the current line.
   The optional target operand is not yet implemented. */

  params =  getCmdParam(params);   /* skip 'SELECT' */
  if (!scr->ed) { return false; }

  /* we need select_old if parameter is relative */
  int curline = getCurrLineNo(scr->ed);
  LinePtr curlinePtr = getLineAbsNo(scr->ed, curline);
  int select_old = curlinePtr->selectionLevel;

  if (sqmet == 'Q') {
    sprintf(msg, "SELECT %d %d", select_old, 0);
    return _rc_success;
  }


  long select = 0;
  long minus1 = -1;
  bool selectRelative = false;

  if (!tokcmp(params, "*")) {
    select = SET_SELECT_MAX;
  } else if (!tryParseInt(params, &select)) {
    sprintf(msg, "SELECT operand must be numeric: %s", params);
    return _rc_error;
  }

  if (*params == '+') {
    long select_new = select_old + select;
    if (select_new <= minus1) /* long integer overflow ? */ {
      select_new = SET_SELECT_MAX-0;  /* DEBUG: -1 */
    } else if (select_new > SET_SELECT_MAX) {
      select_new = SET_SELECT_MAX-0;  /* DEBUG: -2 */  /* z/VM XEDIT does not complain */
    }
    select = select_new;
  } else if (*params == '-') {
    long select_new = select_old + select; /* negative value already inside */
    if (select_new < 0) { select_new = 0; } /* z/VM XEDIT does not complain */
    select = select_new;
  } else if ((select < 0) || (select > SET_SELECT_MAX)) {
    sprintf(msg, "Selection level must be 0 .. %d", SET_SELECT_MAX);
    return _rc_error;
  }

  curlinePtr->selectionLevel = select;

  sprintf(msg, "Line %d SELECT changed from %d to %d", curline, select_old, curlinePtr->selectionLevel);

  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}



static int CmdSqmetDisplay(ScreenPtr scr, char sqmet, char *params, char *msg) {

  params =  getCmdParam(params);   /* skip 'DISPLAY' */
  if (!scr->ed) { return false; }

  EditorPtr ed = scr->ed;
  if (sqmet == 'Q') {
    sprintf(msg, "DISPLAY %d %d", getDisp1(ed), getDisp2(ed));
    return _rc_success;
  }


  long display1 = 0;

  if (!tokcmp(params, "*")) {
    setDisplay(ed, 0, SET_SELECT_MAX);
    params = getCmdParam(params);
    checkNoParams(params, msg);
    return _rc_success;
  } else if (!tryParseInt(params, &display1)) {
    sprintf(msg, "DISPLAY operands must be numeric: %s", params);
    return _rc_error;
  }

  if ((display1 < 0) || (display1 > SET_SELECT_MAX)) {
    sprintf(msg, "DISPLAY operands must be 0 .. %d", SET_SELECT_MAX);
    return _rc_error;
  }

  long display2 = display1;
  params = getCmdParam(params);

  if (!tokcmp(params, "*")) {
    setDisplay(ed, display1, SET_SELECT_MAX);
    params = getCmdParam(params);
    checkNoParams(params, msg);
    return _rc_success;
  } else if (!tokcmp(params, "=")) {
    setDisplay(ed, display1, display1);
    params = getCmdParam(params);
    checkNoParams(params, msg);
    return _rc_success;
  } else if (!tryParseInt(params, &display2)) {
    sprintf(msg, "DISPLAY operands must be numeric: %s", params);
    return _rc_error;
  }

  if ((display2 < 0) || (display2 > SET_SELECT_MAX)) {
    sprintf(msg, "DISPLAY operands must be 0 .. %d", SET_SELECT_MAX);
    return _rc_error;
  }


  if (display1 > display2) {
    sprintf(msg, "DISPLAY operand 1 (%d) must not be larger than operand 2 (%d)", display1, display2);
    return _rc_error;
  }

  setDisplay(ed, display1, display2);
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}



static int CmdSqmetHighlight(ScreenPtr scr, char sqmet, char *params, char *msg) {
  char mode = 'O';
  long select1 = 1;
  long select2 = SET_SELECT_MAX;


  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}

static int CmdSqmetWorklrecl(ScreenPtr scr, char sqmet, char *params, char *msg) {
  t_PGMB *PGMB_loc = CMSGetPG();

  /* physical screen = ONE logical screen */   /* Split screen is NYI */
  char buffer[10];

  if (sqmet == 'E') {
    int rc = ExecCommSet(msg,"WORKLRECL.0", "1", 0);
    if (rc) return rc;
    sprintf(buffer, "%d\0", getWorkLrecl(scr->ed));
    ExecCommSet(msg,"WORKLRECL.1", buffer, 0);
  }

  if (sqmet == 'Q') {
    sprintf(msg, "WORKLRECL %d", getWorkLrecl(scr->ed));
  }

  params = getCmdParam(params);
  checkNoParams(params, msg);
  return _rc_success;
}

static int CmdSqmetFlscreen(ScreenPtr scr, char sqmet, char *params, char *msg) {
  t_PGMB *PGMB_loc = CMSGetPG();

  /* physical screen = ONE logical screen */   /* Split screen is NYI */
  char buffer[10];

  if (sqmet == 'E') {
    int rc = ExecCommSet(msg,"FLSCREEN.0", "2", 0);
    if (rc) return rc;
    sprintf(buffer, "%d\0",scr->ed->view->flscreen1);
    ExecCommSet(msg,"FLSCREEN.1", buffer, 0);
    sprintf(buffer, "%d\0",scr->ed->view->flscreen2);
    ExecCommSet(msg,"FLSCREEN.2", buffer, 0);
  }

  if (sqmet == 'Q') {
    sprintf(msg, "FLSCREEN %d %d", scr->ed->view->flscreen1, scr->ed->view->flscreen2);
  }

  params = getCmdParam(params);
  checkNoParams(params, msg);
  return _rc_success;
}

static int CmdSqmetLscreen(ScreenPtr scr, char sqmet, char *params, char *msg) {
  t_PGMB *PGMB_loc = CMSGetPG();

  /* physical screen = ONE logical screen */   /* Split screen is NYI */
  char buffer[10];

  if (sqmet == 'E') {
    int rc = ExecCommSet(msg,"LSCREEN.0", "6", 0);
    if (rc) return rc;
    ExecCommSet(msg,"LSCREEN.3", "1", 0);
    ExecCommSet(msg,"LSCREEN.4", "1", 0);

    sprintf(buffer, "%d\0",PGMB_loc->rows);
    ExecCommSet(msg,"LSCREEN.1", buffer, 0);
    ExecCommSet(msg,"LSCREEN.5", buffer, 0);

    sprintf(buffer, "%d\0",PGMB_loc->cols);
    ExecCommSet(msg,"LSCREEN.2", buffer, 0);
    ExecCommSet(msg,"LSCREEN.6", buffer, 0);
  }

  if (sqmet == 'Q') {
    sprintf(msg, "LSCREEN %d %d %d %d %d %d", PGMB_loc->rows, PGMB_loc->cols, 1, 1,
                                              PGMB_loc->rows, PGMB_loc->cols);
  }

  params = getCmdParam(params);
  checkNoParams(params, msg);
  return _rc_success;
}


static int CmdSqmetCursor(ScreenPtr scr, char sqmet, char *params, char *msg) {
  char buffer[10];

  if (sqmet == 'E') {

    int rc = ExecCommSet(msg,"CURSOR.0", "9", 0);
    if (rc) return rc;
    ExecCommSet(msg,"CURSOR.1", "_rc_NYI", 0);
    ExecCommSet(msg,"CURSOR.2", "_rc_NYI", 0);
    ExecCommSet(msg,"CURSOR.3", "_rc_NYI", 0);
    ExecCommSet(msg,"CURSOR.4", "_rc_NYI", 0);
    ExecCommSet(msg,"CURSOR.5", "_rc_NYI", 0);
    ExecCommSet(msg,"CURSOR.6", "_rc_NYI", 0);
/*
    ExecCommSet(msg,"CURSOR.7", "_rc_NYI", 0);
    ExecCommSet(msg,"CURSOR.8", "_rc_NYI", 0);
*/
    ExecCommSet(msg,"CURSOR.9", "_rc_NYI", 0);

    if (scr->cElemType == 2) {
      LinePtr line = scr->cElem;
      sprintf(buffer, "%d\0",getlinum(line));
      ExecCommSet(msg,"CURSOR.7", buffer, 0);
      sprintf(buffer, "%d\0",1+scr->cElemOffset);
      ExecCommSet(msg,"CURSOR.8", buffer, 0);

    } else {
      ExecCommSet(msg,"CURSOR.7", "-1", 0);
      ExecCommSet(msg,"CURSOR.8", "-1", 0);
    }
  }

  if (sqmet == 'Q') {
    sprintf(msg, "CURSOR -5 (NYI)");
  }

  params = getCmdParam(params);
  checkNoParams(params, msg);
  return _rc_success;
}


static int CmdSqmetFileid(ScreenPtr scr, char sqmet, char *params, char *msg) {
  EditorPtr ed = scr->ed;
  if (!ed) { return _rc_failure; }

  char fn[9];
  char ft[9];
  char fm[3];
  char buffer[22];
  getFnFtFm(ed, fn, ft, fm);

  if (sqmet == 'E') {
    int rc = ExecCommSet(msg,"FILEID.0", "1", 0);
    if (rc) return rc;
    sprintf(buffer, "%s.%s.%s\0", fn, ft, fm);
    ExecCommSet(msg,"FILEID.1", buffer, 0);

    ExecCommSet(msg,"FNAME.0", "1", 0);
    sprintf(buffer, "%s\0",fn);
    ExecCommSet(msg,"FNAME.1", buffer, 0);

    ExecCommSet(msg,"FTYPE.0", "1", 0);
    sprintf(buffer, "%s\0",ft);
    ExecCommSet(msg,"FTYPE.1", buffer, 0);

    ExecCommSet(msg,"FMODE.0", "1", 0);
    sprintf(buffer, "%s\0",fm);
    ExecCommSet(msg,"FMODE.1", buffer, 0);

  }

  if (sqmet == 'Q') {
    sprintf(msg, "FNAME %s   FTYPE %s   FMODE %s   FILEID %s.%s.%s\0", fn, ft, fm, fn, ft, fm);
  }

  params = getCmdParam(params);
  checkNoParams(params, msg);
  return _rc_success;
}


static int CmdSqmetSize(ScreenPtr scr, char sqmet, char *params, char *msg) {
  char buffer[10];
  int currLineNo = -1;
  int lineCnt = 0;
  getLineInfo(scr->ed, &lineCnt, &currLineNo);

  if (sqmet == 'E') {
    sprintf(buffer, "%d\0",lineCnt);
    int rc = ExecCommSet(msg,"SIZE.0", "1", 0);
    if (rc) return rc;
    ExecCommSet(msg,"SIZE.1", buffer, 0);
  }

  if (sqmet == 'Q') {
    sprintf(msg, "SIZE %d\0",lineCnt);
  }

  params = getCmdParam(params);
  checkNoParams(params, msg);
  return _rc_success;
}




static int CmdSqmetLine(ScreenPtr scr, char sqmet, char *params, char *msg) {
  if (sqmet == 'E') {
    char buffer[10];
    sprintf(buffer, "%d\0",getCurrLineNo(scr->ed));
    int rc = ExecCommSet(msg,"LINE.0", "1", 0);
    if (rc) return rc;
    ExecCommSet(msg,"LINE.1", buffer, 0);
  }

  if (sqmet == 'Q') {
    sprintf(msg, "LINE %d\0",getCurrLineNo(scr->ed));
  }

  params = getCmdParam(params);
  checkNoParams(params, msg);
  return _rc_success;
}




static int CmdSqmetCURLine(ScreenPtr scr, char sqmet, char *params, char *msg) {
  if (sqmet == 'E') {
    LinePtr curLine = getCurrentLine(scr->ed);
    int rc = ExecCommSet(msg,"CURLINE.0", "5", 0);
    if (rc) return rc;
    ExecCommSet(msg,"CURLINE.1", "-1", 0);
    ExecCommSet(msg,"CURLINE.2", "-1", 0);
    ExecCommSet(msg,"CURLINE.3", curLine->text, fileLineLength(scr->ed, curLine));
    ExecCommSet(msg,"CURLINE.4", "-1", 0); /* 2024-12-22 */
    ExecCommSet(msg,"CURLINE.5", "-1", 0); /* was 'lineLength(scr->ed, curLine)' before */
    return rc;
  }


  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}




static int CmdSqmetCase(ScreenPtr scr, char sqmet, char *params, char *msg) {
  char case_um = '?';
  char case_ir = '?';
  char buffer[4096] = "\0";

 if (sqmet == 'S') {
  params =  getCmdParam(params);   /* skip 'CASE' */
  char whatTokenLen = getToken(params, ' ');
  if (whatTokenLen == 0) {
    strcpy(buffer, "\nMissing operand for SET CASE: 'Upper' or 'Mixed' expected\n\0");
    strcat(msg, buffer);
    return false;
  }
  if (isAbbrev(params, "Upper")) {
    case_um = 'U';
  } else if (isAbbrev(params, "Mixed")) {
    case_um = 'M';
  } else if (isAbbrev(params, "Ignore")) {
    case_um = 'M';
    case_ir = 'I';
  } else if (isAbbrev(params, "Respect")) {
    case_um = 'M';
    case_ir = 'R';
  } else {
    strcpy(buffer, "\nInvalid operand for SET CASE: 'Upper' or 'Mixed' expected\n\0");
    strcat(msg, buffer);
    return false;
  }

  params =  getCmdParam(params);   /* skip 'Upper/Mixed' */
  whatTokenLen = getToken(params, ' ');
  if (whatTokenLen == 0) {
    ;
  } else if (isAbbrev(params, "Ignore")) {
    case_ir = 'I';
  } else if (isAbbrev(params, "Respect")) {
    case_ir = 'R';
  } else {
    strcpy(buffer, "\nInvalid operand for SET CASE: 'Ignore' or 'Respect' expected\n\0");
    strcat(msg, buffer);
    return false;
  }


  params =  getCmdParam(params);   /* skip 'Respect/Ignore' */
  whatTokenLen = getToken(params, ' ');
  if (whatTokenLen != 0) {
    sprintf(buffer, "\nKEDIT compatibility not implemented, too many operands for SET CASE: %s\n\0", params);
    strcat(msg, buffer);
    return false;
  }

  if (!scr->ed) { return false; }

  if (case_um == 'U') { setCaseMode(scr->ed, true ); }
  if (case_um == 'M') { setCaseMode(scr->ed, false); }


  if (case_ir == 'R') { setCaseRespect(scr->ed, true ); }
  if (case_ir == 'I') { setCaseRespect(scr->ed, false); }
 }
  case_um = '?';
  case_ir = '?';

  if (edGcase(scr->ed))  { case_um = 'U'; }  else { case_um = 'M'; }
  if (edGcasR(scr->ed))  { case_ir = 'R'; }  else { case_ir = 'I'; }

    if (sqmet == 'Q') {
      sprintf(buffer, "\nCASE %c %c\n\0", case_um, case_ir);
      strcat (msg, buffer);
    }


    if (sqmet == 'T') {
      sprintf(buffer, "%c %c", case_um, case_ir);
      CMSstackLine(buffer, CMS_STACKLIFO);
    }

    if (sqmet == 'M') {
       sprintf(buffer, "SET CASE %c %c", case_um, case_ir);
       scr->cmdLinePrefill = buffer;
       /* sprintf(buffer, ""); */
    }

    if (sqmet == 'E') {
      /*
      strcpy(buffer, "case.0 = 2");
      if (case_um == 'U') sprintf(buffer, "%s\ncase.1 = UPPER", buffer)    ;
      if (case_um == 'M') sprintf(buffer, "%s\ncase.1 = MIXED", buffer)    ;
      if (case_ir == 'I') sprintf(buffer, "%s\ncase.2 = IGNORE", buffer)   ;
      if (case_ir == 'R') sprintf(buffer, "%s\ncase.2 = RESPECT", buffer)  ;
      */

      int rc = ExecCommSet(msg,"CASE.0","2", 0); /* the string "2", not the single char '2' */
      if (rc) return rc;
      if (case_um == 'U') ExecCommSet(msg,"CASE.1","UPPER", 0);
      if (case_um == 'M') ExecCommSet(msg,"CASE.1","MIXED", 0);
      if (case_ir == 'I') ExecCommSet(msg,"CASE.2","IGNORE", 0);
      if (case_ir == 'R') ExecCommSet(msg,"CASE.2","RESPECT", 0);
      return rc;
    } /* if (sqmet == 'E') */
    return _rc_success;
} /* int CmdSqmetCase */


static int CmdSqmetNYI(ScreenPtr scr, char sqmet, char *params, char *msg) {
    sprintf(msg, "%s\nSET/QUERY/MODIFY/EXTRACT/TRANSFER subcommand not yet implemented:  * * * Work In Progress * * *\n%s", msg, params);
    return _rc_failure;
}
static MySqmetDef sqmetCmds[] = {
  {"AAaa"                    , "SQMET" , &CmdSqmetNYI               },
  {"="                       , "sqmet" , &CmdSqmetNYI               },
  {"ACTion"                  , "sqmet" , &CmdSqmetNYI               },
  {"ALT"                     , "sqmet" , &CmdSqmetNYI               },
  {"APL"                     , "sqmet" , &CmdSqmetNYI               },
  {"ARBchar"                 , "sqmet" , &CmdSqmetNYI               },
  {"ATTRibute"               , "SQMET" , &CmdSqmetColor             },
  {"AUtosave"                , "sqmet" , &CmdSqmetNYI               },
  {"BASEft"                  , "sqmet" , &CmdSqmetNYI               },
  {"BRKkey"                  , "sqmet" , &CmdSqmetNYI               },
  {"CASe"                    , "SQMET" , &CmdSqmetCase              },
  {"CMDline"                 , "sqmet" , &CmdSqmetNYI               },
  {"COLOr"                   , "SQMET" , &CmdSqmetColor             },
  {"COLOur"                  , "SQMET" , &CmdSqmetColor             },
  {"COLPtr"                  , "sqmet" , &CmdSqmetNYI               },
  {"COLumn"                  , "sqmet" , &CmdSqmetNYI               },
  {"CTLchar"                 , "sqmet" , &CmdSqmetNYI               },
  {"CURLine"                 , "SQMET" , &CmdSqmetCURLine           },
  {"CURSor"                  , "-Q-Et" , &CmdSqmetCursor            },
  {"DISPlay"                 , "SQMET" , &CmdSqmetDisplay           },
  {"EFMode"                  , "sqmet" , &CmdSqmetNYI               },
  {"EFName"                  , "sqmet" , &CmdSqmetNYI               },
  {"EFType"                  , "sqmet" , &CmdSqmetNYI               },
  {"ENTer"                   , "sqmet" , &CmdSqmetNYI               },
  {"EOF"                     , "sqmet" , &CmdSqmetNYI               },
  {"EOL"                     , "sqmet" , &CmdSqmetNYI               },
  {"ESCape"                  , "sqmet" , &CmdSqmetNYI               },
  {"ETARBCH"                 , "sqmet" , &CmdSqmetNYI               },
  {"ETMODE"                  , "sqmet" , &CmdSqmetNYI               },
  {"FILEid"                  , "sQmEt" , &CmdSqmetFileid            },
  {"FILler"                  , "sqmet" , &CmdSqmetNYI               },
  {"FLscreen"                , "-Q-Et" , &CmdSqmetFlscreen          },
  {"FMode"                   , "sQmEt" , &CmdSqmetFileid            },
  {"FName"                   , "sQmEt" , &CmdSqmetFileid            },
  {"FType"                   , "sQmEt" , &CmdSqmetFileid            },
  {"FULLread"                , "sqmet" , &CmdSqmetNYI               },
  {"HEX"                     , "sqmet" , &CmdSqmetNYI               },
  {"HIGHlight"               , "SQMET" , &CmdSqmetHighlight         },
  {"IMage"                   , "sqmet" , &CmdSqmetNYI               },
  {"IMPcmscp"                , "sqmet" , &CmdSqmetNYI               },
  {"INPmode"                 , "sqmet" , &CmdSqmetNYI               },
  {"LASTLorc"                , "sqmet" , &CmdSqmetNYI               },
  {"LASTmsg"                 , "sqmet" , &CmdSqmetNYI               },
  {"LENgth"                  , "sqmet" , &CmdSqmetNYI               },
  {"LIBName"                 , "sqmet" , &CmdSqmetNYI               },
  {"LIBType"                 , "sqmet" , &CmdSqmetNYI               },
  {"LIne"                    , "SQMET" , &CmdSqmetLine              },
  {"LINENd"                  , "sqmet" , &CmdSqmetNYI               },
  {"LRecl"                   , "sqmet" , &CmdSqmetNYI               },
  {"LScreen"                 , "-Q-Et" , &CmdSqmetLscreen           },
  {"MACRO"                   , "sqmet" , &CmdSqmetNYI               },
  {"MASK"                    , "sqmet" , &CmdSqmetNYI               },
  {"MEMber"                  , "sqmet" , &CmdSqmetNYI               },
  {"MSGLine"                 , "sqmet" , &CmdSqmetNYI               },
  {"MSGMode"                 , "sqmet" , &CmdSqmetNYI               },
  {"NBFile"                  , "SQMET" , &CmdSqmetNYI               },
  {"NBScope"                 , "sqmet" , &CmdSqmetNYI               },
  {"NONDisp"                 , "sqmet" , &CmdSqmetNYI               },
  {"NULls"                   , "sqmet" , &CmdSqmetNYI               },
  {"NUMber"                  , "sqmet" , &CmdSqmetNYI               },
  {"PA"                      , "sqmet" , &CmdSqmetNYI               },
  {"PACK"                    , "sqmet" , &CmdSqmetNYI               },
  {"PENDing"                 , "sqmet" , &CmdSqmetNYI               },
  {"PF"                      , "sqmet" , &CmdSqmetNYI               },
  {"Point"                   , "sqmet" , &CmdSqmetNYI               },
  {"PREfix"                  , "sqmet" , &CmdSqmetNYI               },
  {"RANge"                   , "sqmet" , &CmdSqmetNYI               },
  {"RECFm"                   , "sqmet" , &CmdSqmetNYI               },
  {"REMOte"                  , "*****" , &CmdSqmetNYI               },
  {"RESERved"                , "sqmet" , &CmdSqmetNYI               },
  {"RING"                    , "sqmet" , &CmdSqmetNYI               },
  {"SCALe"                   , "sqmet" , &CmdSqmetNYI               },
  {"SCOPE"                   , "SQMET" , &CmdSqmetScope             },
  {"SCReen"                  , "sqmet" , &CmdSqmetNYI               },
  {"SELect"                  , "SQMET" , &CmdSqmetSelect            },
  {"Seq8"                    , "sqmet" , &CmdSqmetNYI               },
  {"SERial"                  , "sqmet" , &CmdSqmetNYI               },
  {"SHADow"                  , "sqmet" , &CmdSqmetNYI               },
  {"SIDcode"                 , "sqmet" , &CmdSqmetNYI               },
  {"SIZe"                    , "-Q-Et" , &CmdSqmetSize              },
  {"SPAN"                    , "sqmet" , &CmdSqmetNYI               },
  {"SPILL"                   , "sqmet" , &CmdSqmetNYI               },
  {"STAY"                    , "sqmet" , &CmdSqmetNYI               },
  {"STReam"                  , "sqmet" , &CmdSqmetNYI               },
  {"SUBCOM"                  , "SQMET" , &CmdSqmetSubcom            },
  {"SYNonym"                 , "sqmet" , &CmdSqmetNYI               },
  {"TABLine"                 , "sqmet" , &CmdSqmetNYI               },
  {"TABS"                    , "sqmet" , &CmdSqmetNYI               },
  {"TARGet"                  , "sqmet" , &CmdSqmetNYI               },
  {"TERMinal"                , "sqmet" , &CmdSqmetNYI               },
  {"TEXT"                    , "sqmet" , &CmdSqmetNYI               },
  {"TOF"                     , "sqmet" , &CmdSqmetNYI               },
  {"TOFEOF"                  , "sqmet" , &CmdSqmetNYI               },
  {"TOL"                     , "sqmet" , &CmdSqmetNYI               },
  {"TRANSLat"                , "sqmet" , &CmdSqmetNYI               },
  {"TRunc"                   , "sqmet" , &CmdSqmetNYI               },
  {"UNIQueid"                , "sqmet" , &CmdSqmetNYI               },
  {"UNTil"                   , "sqmet" , &CmdSqmetNYI               },
  {"UPDate"                  , "sqmet" , &CmdSqmetNYI               },
  {"VARblank"                , "sqmet" , &CmdSqmetNYI               },
  {"Verify"                  , "sqmet" , &CmdSqmetNYI               },
  {"VERShift"                , "sqmet" , &CmdSqmetNYI               },
  {"VERSIon"                 , "SQMEt" , &CmdSqmetVersion           },
  {"Width"                   , "sqmet" , &CmdSqmetNYI               },
  {"WINdow"                  , "sqmet" , &CmdSqmetNYI               },
  {"WORKLrecl"               , "-Q-Et" , &CmdSqmetWorklrecl         },
  {"WRap"                    , "sqmet" , &CmdSqmetNYI               },
  {"Zone"                    , "sqmet" , &CmdSqmetNYI               },
  {"ZZzz"                    , "-Q-et" , &CmdSqmetNYI               }
};

MySqmetDef* fndsqmet(char *cand, MySqmetDef *cmdList, unsigned int cmdCount) {
  unsigned int i;
  for (i = 0; i < cmdCount; i++) {
    if (isAbbrev(cand, cmdList->sqmetName)) { return cmdList; }
    cmdList++;
  }

  return NULL;
}

static int CmdCallstack(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }

  char buffer[512];
  unsigned long R13, *p;
  long i = 1;

  __asm__("LR %0,13"    : "=d" (R13));
  p = R13;

  while (true) {
    sprintf (buffer, "R13=%08x  D=%08x B=%08x F=%08x  R14=%08x R15=%08x  R0=%08x R1=%08x R2=%08x  GCCcrab=%08x", p,
                    *p, *(p+1), *(p+2), *(p+3),  *(p+4),  *(p+5),  *(p+6) , *(p+7) , *(p+18) );
    insertLine(scr->ed, buffer);
    if (!(p=*(p+1)))  { break; }
    i++;
  }

  return i;
}


static int CmdSortfile(ScreenPtr scr, char *params, char *msg) {
  t_PGMB *PGMB_loc = CMSGetPG();
  if (!scr->ed) { return false; }

  int c1,c2 = 0;
  bool sortDescending = false;
  tryParseInt(params, &c1);
  params =  getCmdParam(params);
  tryParseInt(params, &c2);

  if (c1 < 0) {
    c1 = -c1;
    sortDescending = true;
  }
  PGMB_loc->sortSpecCount = 1;
  PGMB_loc->sortSpecs[0].sortDescending = sortDescending;
  PGMB_loc->sortSpecs[0].offset = c1 - 1;
  PGMB_loc->sortSpecs[0].length = c2 - c1 +1;
  PGMB_loc->sortSpecs[1].sortDescending = false;
  PGMB_loc->sortSpecs[1].offset = 0;
  PGMB_loc->sortSpecs[1].length = 0;
  sort(scr->ed,PGMB_loc->sortSpecs);
  return 0 /* (1000*(c2-c1))+(c1-1) */ ;
}



static int CmdPull(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }

  bool conflicting   = false;
  bool reversed      = false;
  bool single_buffer = false;
  bool dump_all      = false;
  char buffer[512];
  char buffer2[512];
  int stacked = CMSstackQuery();
  int count = stacked;
  int makebuf_rc;
  int makebuf_rc1;
  int makebuf_rc2;

  bool keyword_found = false;

  while (true) /* outer loop */
  {
    keyword_found = false;
    while (true) /* inner loop */
    {
      if (isAbbrev(params, "DUMPALL")) {
        dump_all = true;
        if (single_buffer) { conflicting = true; }
        params = getCmdParam(params);
        keyword_found = true;
        break; /* inner -> outer */
      }
      if (isAbbrev(params, "REVersed")) {
        reversed = true;
        params = getCmdParam(params);
        keyword_found = true;
        break; /* inner -> outer */
      }
      if (isAbbrev(params, "BUFfer")) {
        single_buffer = true;
        if (dump_all) { conflicting = true; }
        params = getCmdParam(params);
        keyword_found = true;
        break; /* inner -> outer */
      }
      if (tryParseInt(params, &count)) {
        params =  getCmdParam(params);
        keyword_found = true;
        break; /* inner -> outer */
      }

      /* ToDo: invalid/unrecognized keyword check */

      break; /* inner -> outer : keyword_found = false */
    }
    if (!keyword_found) { break; /* escape from outer loop */  }
  }

  if (conflicting) {
    sprintf(msg, "Conflicting keywords");
    return 30001;
  }

  if (dump_all) {
     makebuf_rc = CMScommand("MAKEBUF", CMS_CONSOLE);
                  CMScommand("DROPBUF", CMS_CONSOLE);
     sprintf (buffer2, " *** Buffer %d *** ", makebuf_rc-1);
     insertLine(scr->ed, buffer2);
     if (reversed) { moveUp(scr->ed, 1);  }
     while (true) {
       stacked = CMSstackQuery();
       makebuf_rc1 = CMScommand("MAKEBUF", CMS_CONSOLE);
                     CMScommand("DROPBUF", CMS_CONSOLE);
       if (!stacked)  { break; /* stack is empty */  }
       CMSconsoleRead(buffer);
       makebuf_rc2 = CMScommand("MAKEBUF", CMS_CONSOLE);
                     CMScommand("DROPBUF", CMS_CONSOLE);
       if (makebuf_rc2 != makebuf_rc1) {
         sprintf (buffer2, " *** Buffer %d *** ", makebuf_rc2-1);
         insertLine(scr->ed, buffer2);
         if (reversed) { moveUp(scr->ed, 1);  }
       }
       sprintf (buffer2, "%7d %5d %5d : %s", stacked, makebuf_rc1-1, makebuf_rc2-1, buffer);
       insertLine(scr->ed, buffer2);
       if (reversed) { moveUp(scr->ed, 1);  }

     }
  } else if (single_buffer) { /* NYI (not yet implemented) */
      ;
  } else { /* standard case, faster */
      if (count < 0)       { count = -count; reversed = true; }
      if (count > stacked) { count = stacked; }
      while (count--) {
        CMSconsoleRead(buffer);
        insertLine(scr->ed, buffer);
        if (reversed) { moveUp(scr->ed, 1);  }
      }
  }

  stacked = CMSstackQuery();
  makebuf_rc = CMScommand("MAKEBUF", CMS_CONSOLE);
               CMScommand("DROPBUF", CMS_CONSOLE);
  sprintf(msg, "Stack entries = %d, buffer index = %d ",
                stacked,            makebuf_rc-1 );
  return stacked+((makebuf_rc-1)*1000);
}

static int CmdCmsg(ScreenPtr scr, char *params, char *msg) {
  scr->cmdLinePrefill = params;
  return _rc_success;
}

static int CmdMsg(ScreenPtr scr, char *params, char *msg) {
/*
  char c = *msg;
  int  i = c;
*/
  char buffer[4096];

  sprintf(buffer, "\n%s\n\0", params);
  strcat(msg, buffer);
/*
  if (c != 255) { / * *msg not empty, then append * /
    sprintf(buffer, "%d ",i);
    strcat(buffer, msg);
    strcat(buffer, "\n");
    strcat(buffer, params);
    strcat(buffer, "\n");
    strcpy(msg, buffer);
  } else {
    strcpy(buffer, params);
    strcpy(msg, buffer);
  }
*/
  return _rc_success;
}

static int CmdSqmetDispatch(ScreenPtr scr, char sqmet, char *params, char *msg) {
  int  c_index = 0;
  int  s_index = 0;
  int  rc = 0;
  char c;
  char *extract_option_end;
  char c_temp = '?';
  char s_temp = '?';
  bool extract_multiple  = false;
  char extract_delimiter = '\0';
  char extract_option[255] = "\0";
  char *p_s_temp = "SsQqMmEeTt*-=+ ";
  char *p_c_temp = &c_temp;
  void *p_v_temp = p_c_temp;
  int  *p_i_temp = p_v_temp;
  /* int *params_debug = params; */
/*
  if (sqmet == 'S')  { c_index = 0;    s_index = 0; }
  if (sqmet == 'Q')  { c_index = 2;    s_index = 1; }
  if (sqmet == 'M')  { c_index = 4;    s_index = 2; }
  if (sqmet == 'E')  { c_index = 6;    s_index = 3; }
  if (sqmet == 'T')  { c_index = 8;    s_index = 4; }
*/

  switch (sqmet) {
     case 'E' : c_index = 6; s_index = 3;  /* fast path to EXTRACT /ABC/DEF/ */
                c = *params;

                if (c == '/') { extract_delimiter = c; extract_multiple = true; }
                else if (isalnum(c)) /* alphanumeric, no delimiter assumed */ {;}
                else { extract_delimiter = c; extract_multiple = true; }

                if (extract_multiple) {
                  while (true) { /* scan for multiple EXTRACT options */
                    /* Example:   EXTRACT   /  size  /  line/ftype/   color  *  /case    */
                    while (*(++params) == ' ') {;} /* skip leading blanks */
                    extract_option_end = params;
                    while (true) {
                      c = *(++extract_option_end);
                      if ((c == extract_delimiter) || (c == '\0')) {
                        *extract_option_end = '\0';
                        break;
                      }
                    }
                    if (*params == '\0'             ) {return _rc_success;} /* end   ? */
                    if (*params == extract_delimiter) {return _rc_invalid;} /* empty ? */

                    MySqmetDef *sqmetDef = (MySqmetDef*)fndsqmet(
                      params,
                      (MySqmetDef*)sqmetCmds,
                      sizeof(sqmetCmds) / sizeof(MySqmetDef));

                    if (sqmetDef) {
                      p_c_temp = sqmetDef->sqmetFlag;
                      c_temp = p_c_temp[s_index];
                      if (c_temp == sqmet) {
                        rc = (*(sqmetDef->sqmetImpl))(scr, sqmet, params, msg);
                        if (rc) {return rc;}
                      }
                    } else {return _rc_invalid;}
                    if (!c) return; /* end of command string reached */

                    params = extract_option_end;
                  }
                }
                break;
     case 'S' : c_index = 0; s_index = 0;
                break;
     case 'Q' : c_index = 2; s_index = 1;
                break;
     case 'M' : c_index = 4; s_index = 2;
                break;
     case 'T' : c_index = 8; s_index = 4;
                break;
     default: return _rc_failure; /* this should not happen */
  };

  MySqmetDef *sqmetDef = (MySqmetDef*)fndsqmet(
    params,
    (MySqmetDef*)sqmetCmds,
    sizeof(sqmetCmds) / sizeof(MySqmetDef));

    if (sqmetDef) {
      p_c_temp = sqmetDef->sqmetFlag;
      p_v_temp = p_c_temp;
      p_i_temp = p_v_temp;

      c_temp = p_c_temp[s_index];
      if (c_temp == sqmet)  return (*(sqmetDef->sqmetImpl))(scr, sqmet, params, msg);

      if (c_upper(c_temp) == sqmet)
      /* if (c_temp == 's') */ /* p_s_temp[1]  */  {
        sprintf(msg, "%c sqmet subcommand not yet implemented: '%s'", sqmet, sqmetDef->sqmetName);
        return _rc_failure;
      }
      if (c_temp == '*' /* p_s_temp[10] */ ) {
        sprintf(msg, "VM/SP XEDIT feature not implemented: 'SET %s'", sqmetDef->sqmetName);
        return _rc_failure;
      }
      if (c_temp == 'S' /* p_s_temp[0]  */ ) {
        sprintf(msg, "SET subcommand found: '%s - %s'", sqmetDef->sqmetName, sqmetDef->sqmetFlag);
        return (*(sqmetDef->sqmetImpl))(scr, 'S', params, msg);
      }
    }
    sprintf(msg, "SET subcommand not found: '%s - %x - %x'", params, *p_i_temp, c_temp);
    return _rc_failure;
}
static int CmdSet(ScreenPtr scr, char *params, char *msg) {
  return CmdSqmetDispatch(scr, 'S', params, msg);
}
static int CmdImpSet(ScreenPtr scr, char *params, char *msg) {
  return CmdSet(scr, params, msg);
}


static int CmdMacro(ScreenPtr scr, char *params, char *msg) {
    strcpy(msg, "MACRO subcommand not yet implemented:  * * * Work In Progress * * *");
    return false;
}


static int CmdTransfer(ScreenPtr scr, char *params, char *msg) {
  return CmdSqmetDispatch(scr, 'T', params, msg);
}
static int CmdExtract(ScreenPtr scr, char *params, char *msg) {
  return CmdSqmetDispatch(scr, 'E', params, msg);
}
static int CmdModify(ScreenPtr scr, char *params, char *msg) {
  return CmdSqmetDispatch(scr, 'M', params, msg);
}
static int CmdQuery(ScreenPtr scr, char *params, char *msg) {
  return CmdSqmetDispatch(scr, 'Q', params, msg);
}

static int CmdRingList(ScreenPtr scr, char *params, char *msg) {
  t_PGMB *PGMB_loc = CMSGetPG();
  bool show_all      = false;
  bool show_hidden   = false;
  bool show_unhidden = true;
  bool show_internal = false;
  bool show_normal   = false;
  bool show_binary   = false;
  bool show_modified = false;
  bool call_ringnext = true;


  if (getToken(params, ' ')) {
    if (isAbbrev(params, "Hidden")) {
      show_hidden = true;
      show_unhidden = false;
      params = getCmdParam(params);
      call_ringnext = false;
    } else if (isAbbrev(params, "Unhidden")) {
      show_unhidden = true;
      show_hidden   = false;
      params = getCmdParam(params);
      call_ringnext = false;
    } else if (isAbbrev(params, "Internal")) {
      show_internal = true;
      show_unhidden = false;
      params = getCmdParam(params);
      call_ringnext = false;
    } else if (isAbbrev(params, "Normal")) {
      show_normal   = true;
      show_unhidden = false;
      params = getCmdParam(params);
      call_ringnext = false;
    } else if (isAbbrev(params, "Binary")) {
      show_binary   = true;
      show_unhidden = false;
      params = getCmdParam(params);
      call_ringnext = false;
    } else if (isAbbrev(params, "Modified")) {
      show_modified = true;
      show_unhidden = false;
      params = getCmdParam(params);
      call_ringnext = false;
    } else if (isAbbrev(params, "All")) {
      show_all = true;
      params = getCmdParam(params);
      call_ringnext = false;
    }
    if (call_ringnext) { CmdRingNext(scr, params, msg); }
  }

  EditorPtr ed = scr->ed;

  if (ed == NULL) {
    printf("No open files in EE, terminating...\n");
    return true;
  }

  EditorPtr guardEd = ed;
  char fn[9];
  char ft[9];
  char fm[3];
  char *currMarker = "12345";
  int  counter = 0;
  unsigned int *lineCount;
  unsigned int *currLineNo;

  if (*msg) sprintf(msg, "%s\n", msg);

  /* checkNoParams(params, msg); */
  sprintf(currMarker, "====>");
  sprintf(msg, "%s%5d FileName FileType FM Format   Size  Line Col         %d file(s) in ring ", msg, PGMB_loc->fileCount, PGMB_loc->fileCount);
  while(true) {
    getFnFtFm(ed, fn, ft, fm);
    getLineInfo(ed, &lineCount, &currLineNo);
    if ( show_all || (show_hidden   &&  isHidden(ed))
                  || (show_unhidden && !isHidden(ed))
                  || (show_normal   && !isInternalEE(ed))
                  || (show_internal &&  isInternalEE(ed))
                  || (show_binary   &&  isBinary(ed))
                  || (show_modified &&  getModified(ed) && !isInternalEE(ed))
       )
    { sprintf(msg, "%s\n%s %-8s %-8s %-2s %c %4d %6d%6d   0   %s%s%s%s",
      msg,
      currMarker,
      fn, ft, fm,
      getRecfm(ed),
      getFileLrecl(ed),
      lineCount,
      currLineNo,
      (isInternalEE(ed)) ? "*INTERNAL*, " : "",
      (getModified(ed)) ? "* * * Modified * * * " : "Unchanged",
      (isBinary(ed)) ? ", Binary" : "",
      (isHidden(ed)) ? ", *HIDDEN*" : "");
    }
    ed = getNextEd(ed);
    if (ed == guardEd) { break; }
    sprintf(currMarker, "%5d", ++counter);
  }
  return false;
}


static int CmdCancel(ScreenPtr scr, char *params, char *msg) {
  EditorPtr ed = scr->ed;

  if (ed == NULL) {
    printf("No open files in EE, terminating...\n");
    return true;
  }

  EditorPtr guardEd = ed;
  EditorPtr modified = NULL;

  checkNoParams(params, msg);
  while(true) {
    if (getModified(ed) && !isInternalEE(ed)) {
      if (modified == NULL) { modified = ed; }

      if (ed != guardEd) {
        switchToEditor(scr, ed);
        return CmdRingList(scr, params, msg);
      }
    }
    ed = getNextEd(ed);
    if (ed == guardEd) {
      if (modified == NULL) {
        return closeAllFiles(scr, true, msg);
      } else {
        return CmdRingList(scr, params, msg);
      }
    }
  }
  return false;
}


static int CmdAbort(ScreenPtr scr, char *params, char *msg) {
  return _rc_ABORT;
}

static int CmdSetReturnCode(ScreenPtr scr, char *params, char *msg) {
  int rc = 0;
  tryParseInt(params, &rc);
  return rc;
}

typedef struct _mycmddef {
  char *commandName;
  CmdImpl impl;
} MyCmdDef;

static MyCmdDef eeCmds[] = {
  {"ABORT"                   , &CmdAbort                            },
  {"ACTion"                  , &CmdImpSet                           },
  {"ALT"                     , &CmdImpSet                           },
  {"ALL"                     , &CmdAll                              },
  {"APL"                     , &CmdImpSet                           },
  {"ARBchar"                 , &CmdImpSet                           },
  {"ATTRibute"               , &CmdImpSet                           },
  {"AUtosave"                , &CmdImpSet                           },
  {"BASEft"                  , &CmdImpSet                           },
  {"BOTtom"                  , &CmdBottom                           },
  {"BRKkey"                  , &CmdImpSet                           },
  {"CALLSTack"               , &CmdCallstack                        },
  {"CANcel"                  , &CmdCancel                           },
  {"CASe"                    , &CmdImpSet                           },
  {"CASEOLD"                 , &CmdCaseold                          },
  {"CC"                      , &CmdCancel                           },
  {"CCCC"                    , &CmdExit                             },
  {"Change"                  , &CmdChange                           },
  {"CMDLine"                 , &CmdCmdline                          },
/*{"CMDline"                 , &CmdImpSet                           },*/
  {"CMS"                     , &CmdCms                              },
  {"CMSG"                    , &CmdCmsg                             },
  {"COLOr"                   , &CmdImpSet                           },
  {"COLOur"                  , &CmdImpSet                           },
  {"COLPtr"                  , &CmdImpSet                           },
  {"COLumn"                  , &CmdImpSet                           },
  {"CTLchar"                 , &CmdImpSet                           },
  {"CURLine"                 , &CmdImpSet                           },
  {"CURRLine"                , &CmdCurrline                         },
  {"CURSor"                  , &CmdImpSet                           },
  {"DELete"                  , &CmdDelete                           },
  {"DISPlay"                 , &CmdImpSet                           },
  {"DEBUG"                   , &CmdDebug                            },
  {"DEFAULTS"                , &CmdDefaults                         },
  {"Eedit"                   , &CmdEditFile                         },
  {"EFMode"                  , &CmdImpSet                           },
  {"EFName"                  , &CmdImpSet                           },
  {"EFType"                  , &CmdImpSet                           },
  {"ENTer"                   , &CmdImpSet                           },
  {"EOF"                     , &CmdImpSet                           },
  {"EOL"                     , &CmdImpSet                           },
  {"ESCape"                  , &CmdImpSet                           },
  {"ETARBCH"                 , &CmdImpSet                           },
  {"ETMODE"                  , &CmdImpSet                           },
  {"EXIt"                    , &CmdExit                             },
  {"EXTract"                 , &CmdExtract                          },
  {"FFile"                   , &CmdFFile                            },
  {"FILe"                    , &CmdFile                             },
  {"FILler"                  , &CmdImpSet                           },
  {"FLscreen"                , &CmdImpSet                           },
  {"FMode"                   , &CmdImpSet                           },
  {"FName"                   , &CmdImpSet                           },
  {"FSLIst"                  , &CmdFSList                           },
  {"FTDEFaults"              , &CmdFtDefaults                       },
  {"FTTABDEFaults"           , &CmdFtTabs                           },
  {"FType"                   , &CmdImpSet                           },
  {"FULLread"                , &CmdImpSet                           },
  {"GAPFill"                 , &CmdGapFill                          },
  {"GET"                     , &CmdGet                              },
  {"GETD"                    , &CmdGetD                             },
  {"Help"                    , &CmdHelp                             },
  {"HEX"                     , &CmdImpSet                           },
  {"HIDe"                    , &CmdHide                             },
  {"HIGHlight"               , &CmdImpSet                           },
  {"IMage"                   , &CmdImpSet                           },
  {"IMPcmscp"                , &CmdImpSet                           },
  {"INFOLines"               , &CmdInfolines                        },
  {"INPmode"                 , &CmdImpSet                           },
  {"Input"                   , &CmdInput                            },
  {"INPUTB"                  , &CmdInput                            },
  {"Kedit"                   , &CmdEditFile                         },
  {"LASTLorc"                , &CmdImpSet                           },
  {"LASTmsg"                 , &CmdImpSet                           },
  {"LENgth"                  , &CmdImpSet                           },
  {"LIBName"                 , &CmdImpSet                           },
  {"LIBType"                 , &CmdImpSet                           },
  {"LIne"                    , &CmdImpSet                           },
  {"LINENd"                  , &CmdImpSet                           },
  {"Locate"                  , &CmdLocate                           },
/*{"LRecl"                   , &CmdImpSet                           },*/
  {"LRECL"                   , &CmdLrecl                            },
  {"LScreen"                 , &CmdImpSet                           },
  {"MACRO"                   , &CmdMacro                            },
  {"MARK"                    , &CmdMark                             },
  {"MASK"                    , &CmdImpSet                           },
  {"MDump"                   , &CmdMemoryDump                       },
  {"MDisplay"                , &CmdMemoryDump                       },
  {"MEMOrydump"              , &CmdMemoryDump                       },
  {"MEMOrydisplay"           , &CmdMemoryDump                       },
  {"MEMber"                  , &CmdImpSet                           },
#if 0
  {"MEMLOCK"                 , &CmdMemLock                          }, /* consume all memory to test EE's behaviour */
  {"MEMUNLOCK"               , &CmdMemUnLock                        }, /* release the memory again */
#endif
  {"MODify"                  , &CmdModify                           },
  {"MOVEHere"                , &CmdMoveHere                         },
  {"MSG"                     , &CmdMsg                              },
/*{"MSGLine"                 , &CmdImpSet                           },*/
  {"MSGLines"                , &CmdMsglines                         },
  {"MSGMode"                 , &CmdImpSet                           },
  {"NBFile"                  , &CmdImpSet                           },
  {"NBScope"                 , &CmdImpSet                           },
  {"Next"                    , &CmdNext                             },
  {"NONDisp"                 , &CmdImpSet                           },
/*{"NULls"                   , &CmdImpSet                           },*/
  {"NULls"                   , &CmdNulls                            },
/*{"NUMber"                  , &CmdImpSet                           },*/
  {"NUMbers"                 , &CmdNumbers                          },
  {"PA"                      , &CmdImpSet                           },
  {"PACK"                    , &CmdImpSet                           },
  {"PENDing"                 , &CmdImpSet                           },
/*{"PF"                      , &CmdImpSet                           },*/
  {"PF"                      , &CmdPf                               },
  {"PGDOwn"                  , &CmdPgDown                           },
  {"PGUP"                    , &CmdPgUp                             },
  {"PInput"                  , &CmdProgrammersInput                 },
  {"Point"                   , &CmdImpSet                           },
  {"PPUT"                    , &CmdPPut                             },
  {"PPUTD"                   , &CmdPPutD                            },
/*{"PREfix"                  , &CmdImpSet                           },*/
  {"PREFIX"                  , &CmdPrefix                           },
  {"Previous"                , &CmdPrevious                         },
  {"PULL"                    , &CmdPull                             },
  {"PUT"                     , &CmdPut                              },
  {"PUTD"                    , &CmdPutD                             },
  {"QQuit"                   , &CmdQQuit                            },
  {"Query"                   , &CmdQuery                            },
  {"QUIt"                    , &CmdQuit                             },
  {"RANge"                   , &CmdImpSet                           },
  {"RC"                      , &CmdSetReturnCode                    },
/*{"RECFm"                   , &CmdImpSet                           },*/
  {"RECFM"                   , &CmdRecfm                            },
  {"REMOte"                  , &CmdImpSet                           },
  {"REPLace"                 , &CmdInput                            },
  {"REPLACEB"                , &CmdInput                            },
  {"RESERved"                , &CmdImpSet                           },
  {"RESet"                   , &CmdReset                            },
  {"RETURNCode"              , &CmdSetReturnCode                    },
  {"REVSEArchnext"           , &CmdReverseSearchNext                },
  {"RING"                    , &CmdImpSet                           },
  {"RINGList"                , &CmdRingList                         },
  {"RINGNext"                , &CmdRingNext                         },
  {"RINGPrev"                , &CmdRingPrev                         },
  {"RList"                   , &CmdRingList                         },
  {"RN"                      , &CmdRingNext                         },
  {"RP"                      , &CmdRingPrev                         },
  {"Rr"                      , &CmdRingList                         },
  {"RSEArchnext"             , &CmdReverseSearchNext                },
  {"SAVe"                    , &CmdSave                             },
/*{"SCALe"                   , &CmdImpSet                           },*/
  {"SCALe"                   , &CmdScale                            },
  {"SCOPE"                   , &CmdImpSet                           },
  {"SCReen"                  , &CmdImpSet                           },
  {"SEArchnext"              , &CmdSearchNext                       },
  {"SELect"                  , &CmdImpSet                           },
  {"Seq8"                    , &CmdImpSet                           },
  {"SERial"                  , &CmdImpSet                           },
  {"SET"                     , &CmdSet                              },
  {"SETRC"                   , &CmdSetReturnCode                    },
  {"SETRETURNCode"           , &CmdSetReturnCode                    },
  {"SHADow"                  , &CmdImpSet                           },
  {"SHIFT"                   , &CmdShift                            },
  {"SHIFTCONFig"             , &CmdShiftConfig                      },
  {"SIDcode"                 , &CmdImpSet                           },
  {"SIZe"                    , &CmdImpSet                           },
  {"SORTFILE"                , &CmdSortfile                         },
  {"SPAN"                    , &CmdImpSet                           },
  {"SPILL"                   , &CmdImpSet                           },
  {"SPLTJoin"                , &CmdSplitjoin                        },
  {"SSave"                   , &CmdSSave                            },
  {"STAY"                    , &CmdImpSet                           },
  {"STReam"                  , &CmdImpSet                           },
  {"SUBCOM"                  , &CmdImpSet                           },
  {"SYNonym"                 , &CmdImpSet                           },
  {"TABBackward"             , &CmdTabBackward                      },
  {"TABforward"              , &CmdTabForward                       },
  {"TABLine"                 , &CmdImpSet                           },
/*{"TABS"                    , &CmdImpSet                           },*/
  {"TABSet"                  , &CmdTabs                             },
  {"TARGet"                  , &CmdImpSet                           },
  {"TERMinal"                , &CmdImpSet                           },
  {"TEXT"                    , &CmdImpSet                           },
  {"Thedit"                  , &CmdEditFile                         },
  {"TOF"                     , &CmdImpSet                           },
  {"TOFEOF"                  , &CmdTofeof                           },
  {"TOL"                     , &CmdImpSet                           },
  {"TOp"                     , &CmdTop                              },
  {"TRAnsfer"                , &CmdTransfer                         },
  {"TRANSLat"                , &CmdImpSet                           },
  {"TRunc"                   , &CmdImpSet                           },
  {"UNBINARY"                , &CmdUnbinary                         },
  {"UNHIDe"                  , &CmdUnHide                           },
  {"UNIQueid"                , &CmdImpSet                           },
  {"UNTil"                   , &CmdImpSet                           },
  {"UPDate"                  , &CmdImpSet                           },
  {"VARblank"                , &CmdImpSet                           },
  {"Verify"                  , &CmdImpSet                           },
  {"VERShift"                , &CmdImpSet                           },
  {"VERSIon"                 , &CmdImpSet                           },
  {"Width"                   , &CmdImpSet                           },
  {"WINdow"                  , &CmdImpSet                           },
  {"WORKLrecl"               , &CmdWorkLrecl                        },
  {"WRap"                    , &CmdImpSet                           },
  {"Xedit"                   , &CmdEditFile                         },
  {"Zone"                    , &CmdImpSet                           }
};


/*
extern EditorPtr mkEdFil(
    EditorPtr prevEd,
    char *fn,
    char *ft,
    char *fm,
    int defaultLrecl,
    char defaultRecfm,
    int *state, / * 0=OK, 1=FileNotFound, 2=ReadError, 3=OtherError * /
    char *msg);
*#define createEditorForFile( \
    prevEd, fn, ft, fm, defLrecl, defRecfm, state, msg) \
  mkEdFil(prevEd, fn, ft, fm, defLrecl, defRecfm, state, msg)
*/
EditorPtr initCmds() {
  t_PGMB *PGMB_loc = CMSGetPG();
  char *_dummy_msg[4096];
  int  _dummy_state;
  memset(PGMB_loc->pfCmds, '\0', sizeof(PGMB_loc->pfCmds));

  PGMB_loc->commandHistory   = createEditorForFile(NULL,                        "HISTORY ", "EE$INTRN", "A0", CMDLINELENGTH + 2, 'V', &_dummy_state, _dummy_msg);
  PGMB_loc->fileCount++;
  PGMB_loc->filetypeDefaults = createEditorForFile(getPrevEd(PGMB_loc->commandHistory),   "DEFAULTS", "EE$INTRN", "A0", 24,                'F', &_dummy_state, _dummy_msg);
  PGMB_loc->fileCount++;
  PGMB_loc->filetypeTabs     = createEditorForFile(getPrevEd(PGMB_loc->filetypeDefaults), "TABS    ", "EE$INTRN", "A0", 80,                'F', &_dummy_state, _dummy_msg);
  PGMB_loc->fileCount++;
  PGMB_loc->macroLibrary     = createEditorForFile(getPrevEd(PGMB_loc->filetypeTabs),     "MACROS  ", "EE$INTRN", "A0", 255,               'V', &_dummy_state, _dummy_msg);
  PGMB_loc->fileCount++;

  setIsHidden(PGMB_loc->commandHistory  );
  setIsHidden(PGMB_loc->filetypeDefaults);
  setIsHidden(PGMB_loc->filetypeTabs    );
  setIsHidden(PGMB_loc->macroLibrary    );

/*
  PGMB_loc->commandHistory = createEditor(NULL, CMDLINELENGTH + 2, 'V');
  PGMB_loc->filetypeDefaults = createEditor(NULL, 24, 'F');
  PGMB_loc->filetypeTabs = createEditor(NULL, 80, 'F');
*/

  PGMB_loc->searchPattern[0] = '\0';
  PGMB_loc->searchUp = false;
  return PGMB_loc->macroLibrary;

}


bool isInternalEE(EditorPtr ed) {
  t_PGMB *PGMB_loc = CMSGetPG();
  if (ed == PGMB_loc->commandHistory    ) return true;
  if (ed == PGMB_loc->filetypeDefaults  ) return true;
  if (ed == PGMB_loc->filetypeTabs      ) return true;
  if (ed == PGMB_loc->macroLibrary      ) return true;
  return false;
}

void deinCmds() {
  t_PGMB *PGMB_loc = CMSGetPG();
  freeEditor(PGMB_loc->commandHistory);
  freeEditor(PGMB_loc->filetypeDefaults);
  freeEditor(PGMB_loc->filetypeTabs);
  freeEditor(PGMB_loc->macroLibrary);
}

void setPF(ScreenPtr scr, int pfScope, int pfMode, int pfNo, char *cmdline) {
  if (pfNo < 0 || pfNo > 24) { return; }
  t_PGMB *PGMB_loc = CMSGetPG();

  int *store_pfMode = &PGMB_loc->pfMode[pfNo];
  char *pfCmd = PGMB_loc->pfCmds[pfNo]; /* 2024-12-27 old global location */
  /* printf ("%d %x %s \n", pfNo, pfCmd, pfCmd);  */ /* 2024-12-28-2200 DEBUG */

  if (pfScope == PFSCOPE_VIEW) {
    if (scr) {
      if (scr->ed) {
        if (scr->ed->view) {
          store_pfMode = &scr->ed->view->pfMode[pfNo];
          pfCmd  = scr->ed->view->pfCmds[pfNo];  /* 2024-12-27 set PF key per view */
          /* printf ("%d %x %s \n", pfNo, pfCmd, pfCmd); */  /* 2024-12-28-2200 DEBUG */
        }
      }
    }
  }
  *store_pfMode = pfMode;
  memset(pfCmd, '\0', CMDLINELENGTH+1);
  if (cmdline && *cmdline) {
    strncpy(pfCmd, cmdline, CMDLINELENGTH);   /* 2024-12-26 ToDo: check CMDLINELENGTH */
    /* printf ("%d %x %s \n", pfNo, pfCmd, pfCmd); */  /* 2024-12-28-2200 DEBUG */
 }
}

extern int execCmd(
    ScreenPtr scr,
    char *cmd,
    char *msg,
    bool addToHistory) {

  /* char buffer[4096] = "\0"; */
  t_PGMB *PGMB_loc = CMSGetPG();

  if (!cmd) { cmd = scr->cmdLine; }
  while(*cmd == ' ') { cmd++; }
  if (!*cmd) { return false; }

/* preliminary */  PGMB_loc->saveScreenPtr = scr;
/* preliminary */  PGMB_loc->saveMsgPtr    = msg;

  while (addToHistory) {
    moveToBOF(PGMB_loc->commandHistory);
    int i = getLineCount(PGMB_loc->commandHistory);
    int scanLines = (i < CMD_HISTORY_DUPE_CHECK)
                   ? i : CMD_HISTORY_DUPE_CHECK;

    for (i = 1; i <= scanLines; i++) {
      LinePtr dupCmd = moveDown(PGMB_loc->commandHistory,1);
      if (!strcmp(cmd, dupCmd->text)) {
        deleteLine(PGMB_loc->commandHistory, dupCmd);
        break; /* we do not expect more than one duplicate */
      }
    }

    moveToBOF(PGMB_loc->commandHistory);
    insertLine(PGMB_loc->commandHistory, cmd);
    if (getLineCount(PGMB_loc->commandHistory) > CMD_HISTORY_LEN) {
      LinePtr oldestCmd = moveToLastLine(PGMB_loc->commandHistory);
      deleteLine(PGMB_loc->commandHistory, oldestCmd);
    }
    moveToBOF(PGMB_loc->commandHistory); /* 2025-01-05 should this be moved to here ? */
    break;
  }
  /* moveToBOF(PGMB_loc->commandHistory); */ /* 2025-01-05 should this be moved up ? */

  /* check if we shall search for macros first : XEDIT - SET MACRO ON */

  MyCmdDef *cmdDef = (MyCmdDef*)findCommand(
    cmd,
    (CmdDef*)eeCmds,
    sizeof(eeCmds) / sizeof(MyCmdDef));

  int dummyInt;
  if (!cmdDef) {
    if (cmd[0] == '/' && cmd[1] == '\0') {
      return CmdSearchNext(scr, cmd, msg);
    } else if (cmd[0] == '-' && cmd[1] == '/' && cmd[2] == '\0') {
      return CmdReverseSearchNext(scr, cmd, msg);
    } else if (  cmd[0] == '.'
              || cmd[0] == ':'
              || cmd[0] == '/'
              || cmd[0] == '-'
              || cmd[0] == '+'
              || tryParseInt(cmd, &dummyInt)) {
      return CmdLocate(scr, cmd, msg);
    }
    /* check if we shall search for macros : XEDIT - SET MACRO OFF */
    char cmsexeccmd[255];
    unsigned long R13;
    sprintf(cmsexeccmd,"EXEC EE$MACRO %s",cmd);
    __asm__("LR %0,13"    : "=d" (R13));
    PGMB_loc->cmscrab = R13;
    /* preliminary */  PGMB_loc->saveScreenPtr = scr;
    /* preliminary */  PGMB_loc->saveMsgPtr    = msg;
    int rc = CMScommand(cmsexeccmd, CMS_CONSOLE);
    if (rc != -3) {
      if (rc != 0) {
        if (!*msg) strcpy(msg, "Non-zero return code from macro");
        sprintf(msg,"%s\n(RC=%d)", msg, rc);
      }
      return rc;
    }
    sprintf(msg, "Unknown command '%s'", cmd);
    return -3;
  }

  /* msg = &msg[strlen(msg)]; */      /* why ??????? */

  CmdImpl impl = cmdDef->impl;
  char *params = cmd;
  char *cmdName = cmdDef->commandName;
  /* check for implicit SET command */
  if (impl != &CmdImpSet) {
    while(c_upper(*params) == c_upper(*cmdName) && *cmdName) {
      params++;
      cmdName++;
    }
  };
  while(*params && *params == ' ') { params++; }

  int rc;
  _try {
    rc = (*impl)(scr, params, msg);
  } _catchall() {
    rc = false;
  } _endtry;

  if (rc != 0) {
    if (!*msg) strcat(msg, "\nNon-zero return code issued");
    sprintf(msg,"%s\n(RC=%d)", msg, rc);
  }
  return rc;
}
/**********************************************************************/
/* is this used anywhere ? See EEPREFIX ... */
/* char* gPfCmd(char aidCode) { */
char* gPfCmd(ScreenPtr scr, char aidCode, int *store_pfMode) {
  int idx = aidPfIndex(aidCode);
  if (idx < 0 || idx > 24) { return NULL; }
  t_PGMB *PGMB_loc = CMSGetPG();

  char *pfCmd = PGMB_loc->pfCmds[idx];
  *store_pfMode = PGMB_loc->pfMode[idx];

  if (scr) {
    if (scr->ed) {
      if (scr->ed->view) {
        if (scr->ed->view->pfCmds[idx][0]) {   /* 2024-12-28 PF key defined at view level ? */
          pfCmd = scr->ed->view->pfCmds[idx];  /* 2024-12-27 set PF key per view */
          *store_pfMode = scr->ed->view->pfMode[idx];
        }
      }
    }
  }
  /* return PGMB_loc->pfCmds[idx]; */
  if (*pfCmd) { return pfCmd; } else { return NULL; }
}

/**********************************************************************/
/* was before 2025-01-05 int tryExPf(ScreenPtr scr, char aidCode, char *msg) { */
  bool tryRecPf(char *pfCmd) {
  t_PGMB *PGMB_loc = CMSGetPG();

  if  (!pfCmd) return false;
  if (!*pfCmd) return false;

  if ( (sncmp(pfCmd, "RECALL")   == 0) || (sncmp(pfCmd, "RECALL-")   == 0) ||
       (sncmp(pfCmd, "RETRIEVE") == 0) || (sncmp(pfCmd, "RETRIEVE-") == 0) ||
       (sncmp(pfCmd, "?")        == 0) || (sncmp(pfCmd, "?-")        == 0) ) {
    LinePtr currCmd = getCurrentLine(PGMB_loc->commandHistory);
    LinePtr nextCmd = moveDown(PGMB_loc->commandHistory, 1);
    if (currCmd == nextCmd) { moveToBOF(PGMB_loc->commandHistory); }
    return true; /* 2024-01-05 tell caller that we have executed a recall command */
  } else if ( (sncmp(pfCmd, "RECALL+")   == 0) ||
              (sncmp(pfCmd, "RETRIEVE+") == 0) ||
              (sncmp(pfCmd, "?+")        == 0) ) {
    LinePtr currCmd = getCurrentLine(PGMB_loc->commandHistory);
    LinePtr prevCmd = moveUp(PGMB_loc->commandHistory, 1);
    if (currCmd == prevCmd) { moveToBOF(PGMB_loc->commandHistory); }
    return true; /* 2024-01-05 tell caller that we have executed a recall command */
  } else if ( (sncmp(pfCmd, "RECALL=") == 0) || (sncmp(pfCmd, "RETRIEVE=") == 0) ||
              (sncmp(pfCmd, "CLRCMD")  == 0) || (sncmp(pfCmd, "?=")        == 0) ) {
    moveToBOF(PGMB_loc->commandHistory);
    return true; /* 2024-01-05 tell caller that we have executed a recall command */
  } else {
      /* NOP */ ;
  }
  return false;  /* 2024-01-05 tell caller that we did not execute any recall command
                    execCommand(scr, pfCmd, msg, false) is done elsewhere */
}

char* grccmd() {
  t_PGMB *PGMB_loc = CMSGetPG();
  LinePtr recalledCommand = getCurrentLine(PGMB_loc->commandHistory);
  if (!recalledCommand) { return NULL; }
  return recalledCommand->text;
}

void unrHist() {
  t_PGMB *PGMB_loc = CMSGetPG();
  moveToBOF(PGMB_loc->commandHistory);
}

static int handleProfileLine(void *userdata, char *cmdline, char *msg) {
  ScreenPtr scr = (ScreenPtr)userdata;
  return execCmd(scr, cmdline, msg, false);
}

bool exCmdFil(ScreenPtr scr, char *fn, int *rc) {
  return doCmdFil(&handleProfileLine, scr, fn, rc);
}

#if 0
int exCmdFil(ScreenPtr scr, char *fn, int *rc) {
  char buffer[256];
  char msg[512];
  char fspec[32];

  char mergedLines[513];
  int mergedRest = 0;
  char *merged = NULL;

  memcpy(buffer, '\0', sizeof(buffer));

  memcpy(buffer, '\0', sizeof(buffer));
  strncpy(buffer, fn, 8);
  sprintf(fspec, "%s EE * V 255", buffer);

  *rc = 1; /* file not found */
  if (!f_exists(buffer, "EE", "*")) { return false; }
  FILE *cmdfile = fopen(fspec, "r");
  if (!cmdfile) { return false; }
  *rc = 0; /* ok */

  bool doneWithFile = false;
  char *line = fgets(buffer, sizeof(buffer), cmdfile);
  while(!feof(cmdfile)) {
    int len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == ' ')) {
      line[len-1] = '\0';
      len--;
    }
    while(*line == ' ' || *line == '\t') { line++; len--; }

    if (len > 0 && line[len-1] == '\\') {
      if (!merged) {
        merged = mergedLines;
        mergedRest = sizeof(mergedLines) - 1; /* keep last null char */
      }
      len = minInt(len - 1, mergedRest);
      if (len <= 0) { continue; }
      memcpy(merged, line, len);
      merged += len;
      mergedRest -= len;

      line = fgets(buffer, sizeof(buffer), cmdfile);
      continue;
    }

    if (merged) {
      len = minInt(len, mergedRest);
      if (len > 0) {
        memcpy(merged, line, len);
      }
      line = mergedLines;
      len = 1;
    }

    if (len > 0 && *line != '*') {
      msg[0] = '\0';
      doneWithFile |= execCmd(scr, line, msg, false);
      if (msg[0]) {
        printf("%s\n", msg);
        *rc = 2; /* some message issued */
      }
    }
    memset(mergedLines, '\0', sizeof(mergedLines));
    merged = NULL;
    line = fgets(buffer, sizeof(buffer), cmdfile);
  }
  fclose(cmdfile);

  return doneWithFile;
}
#endif

/*
** rescue line mode
*/

static int RescueRingList(ScreenPtr scr, char *params, char *msg) {
  EditorPtr ed = scr->ed;

  if (ed == NULL) {
    printf("No open files in EE, terminating...\n");
    return true;
  }

  EditorPtr guardEd = ed;
  char fn[9];
  char ft[9];
  char fm[3];
  char *currMarker = "**";

  checkNoParams(params, msg);

  printf("Open files in EE ( ** -> current file ) :\n");
  while(true) {
    getFnFtFm(ed, fn, ft, fm);
    printf("%s %-8s %-8s %-2s   :   %s%s\n",
      currMarker,
      fn, ft, fm,
      (getModified(ed)) ? "Modified" : "Unchanged",
      (isBinary(ed)) ? ", Binary" : "");
    currMarker = "  ";
    ed = getNextEd(ed);
    if (ed == guardEd) { break; }
  }
  return false;
}

static MyCmdDef rescueCmds[] = {
  {"EXIt"                    , &CmdExit                             },
  {"FFILe"                   , &CmdFFile                            },
  {"FILe"                    , &CmdFile                             },
  {"QQuit"                   , &CmdQQuit                            },
  {"Quit"                    , &CmdQuit                             },
  {"RINGList"                , &RescueRingList                      },
  {"RINGNext"                , &CmdRingNext                         },
  {"RINGPrev"                , &CmdRingPrev                         },
  {"RL"                      , &RescueRingList                      },
  {"RN"                      , &CmdRingNext                         },
  {"RP"                      , &CmdRingPrev                         }
};

void _rloop(ScreenPtr scr, char *messages) {
  char cmdline[135];
  bool done = false;
  CMSconsoleWrite("\nEE Rescue command loop entered\n", CMS_NOEDIT);
  while(!done && scr->ed) {
    CMSconsoleWrite("Enter EE Rescue command\n", CMS_NOEDIT);
    CMSconsoleRead(cmdline);

    char *cmd = cmdline;
    while(*cmd == ' ' || *cmd == '\t') { cmd++; }
    if (!*cmd) { continue; }

    MyCmdDef *cmdDef = (MyCmdDef*)findCommand(
        cmd,
        (CmdDef*)rescueCmds,
        sizeof(rescueCmds) / sizeof(MyCmdDef));
    CmdImpl impl = cmdDef->impl;
    char *params = params =  getCmdParam(cmd);

    *messages = '\0';
    _try {
      done = (*impl)(scr, params, messages);
    } _catchall() {
      CMSconsoleWrite("** caught exception form command", CMS_EDIT);
    } _endtry;
    if (*messages) {
      CMSconsoleWrite(messages, CMS_NOEDIT);
      CMSconsoleWrite("\n", CMS_NOEDIT);
    }
  }
  CMSconsoleWrite(
    "\nAll files closed, leaving EE Rescue command loop\n",
    CMS_NOEDIT);
}
