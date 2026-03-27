/*
** EESCRN.C    - MECAFF EE editor screen-handler implementation file
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements the EE screen displaying and reading used by the
** fullscreen tools.
**
** The screen structure is defined by the ScreenPtr structure filled by the
** calling client, configuring the setup of the screen and the data to display.
**
** The module implements the routines to manage (allocate, free) screens and
** to perform the roundtrip with a given screen.
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
#include <string.h>

#include "ee_first.h"
#define _eescrn_implementation
#include "eescrn.h"
#include "eeutil.h"
#include "fs3270.h"
#include "fsio.h"
#include "ee_pgm.h"   /* Process Global Memory */

#include "glblpost.h"

/* set the filename for error messages from memory protection in EEUTIL */
static const char *_FILE_NAME_ = "eescrn.c";

/* constants defining the basic limits of EE screens */
#define MAX_ED_LINES 256                                   /* 2025-01-07 DEBUG: was 64, now 256 */
#define MAX_MSG_LINES 13

/* each displayed line is recorded in the following structure before the
   roundtrip to recognize it in the input stream.
*/
typedef struct _edline_place {
  LinePtr edLine;
  unsigned int edLineNo;
  unsigned int txtRow;
  unsigned int txtCol;
  unsigned int prefixRow;
  unsigned int prefixCol;
  char prefixFill[PREFIXLENGTH + 1];
} EdLinePlace;

/* the additional private data in ScreenPtr needed by the implementation */
typedef struct _eescreen_private {
  bool cursorIsPlaced;
  unsigned int cmdRow;
  unsigned int cmdCol;
  unsigned int hShiftEffective;
  unsigned int edLinesUsed;
  EdLinePlace edLinePlaces[MAX_ED_LINES];                  /* 2025-01-07 DEBUG: was 64, now 256 */
} ScreenPrivate;

/* the complete ScreenPtr structure seen by the implementation */
typedef struct _screen {
  ScreenPublic screenPublic;
  ScreenPrivate screenPrivate;
} Screen;

typedef ScreenPrivate *ScreenPrivPtr;
typedef ScreenPublic *ScreenPublPtr;

#define SCREENPUBL(p) (&(p->screenPublic))
#define SCREENPRIV(p) (&(p->screenPrivate))

/* screen characteristics */
/*
static char termName[TERM_NAME_LENGTH + 1];
static int numAltRows = -1;
static int numAltCols= -1;
static bool canAltScreenSize = false;
static bool canExtHighLight = false;
static bool canColors = false;
static int sessionId = 0;
static int sessionMode = 0;

static unsigned int rows = 24;
static unsigned int cols = 80;

static unsigned int lastRow = 23;
static unsigned int lastCol = 79;
*/

/* mapping of EESCRN-attributes to FS3270-colors */
/*
static char colorsFor3270[16] = {
  Color_Default,
  Color_Default,
  Color_Blue,
  Color_Blue,
  Color_Red,
  Color_Red,
  Color_Pink,
  Color_Pink,
  Color_Green,
  Color_Green,
  Color_Turquoise,
  Color_Turquoise,
  Color_Yellow,
  Color_Yellow,
  Color_White,
  Color_White
};
*/

/* some UI text constants */
/*
static char *cmdArrow = "====>";
static char *topOfFileText    = "* * * Top of File * * *";
static char *bottomOfFileText = "* * * End of File * * *";
static char *prefixLocked = ".....";
*/

#ifdef _NOCMS
void simu3270(int simuRows, int simuCols) {
  numAltRows = simuRows;
  numAltCols = simuCols;
  rows = simuRows;
  cols = simuCols;
  lastRow = rows - 1;
  lastCol = cols - 1;
  canAltScreenSize = 1;
  canColors = 1;
}
#endif

/* forward declarations */

static bool initScreenInfo(char *msgBuffer);

static int countMsgLines(
    ScreenPublPtr scr,
    char *lineStarts[MAX_MSG_LINES],
    int lineLengths[MAX_MSG_LINES]);

static void addWidenedLine(ScreenPublPtr scr, char *line);

static void startField(unsigned char pubAttr, bool readonly, bool autoSkip);
static void startField2(unsigned char pubAttr, unsigned char pubHiLit, bool readonly, bool autoSkip);

static void writeScale(ScreenPublPtr scr);

static char* getCurrPrefixMark(ScreenPublPtr scr, LinePtr line);

static void writeFileLine(
    ScreenPublPtr pub,
    ScreenPrivPtr priv,
    LinePtr line,
    unsigned int lineNo,
    short scrLinesPerEdLine,
    bool isCurrentLine,
    char *prefixPrefill);

static void writeTextAsFileMarker(
    ScreenPublPtr pub,
    ScreenPrivPtr priv,
    char *fileMarker,
    unsigned int lineNo,
    short scrLinesPerEdLine,
    bool isCurrentLine,
    bool allowPrefix);

/*
** public methods
*/

#define doIC() { IC(); priv->cursorIsPlaced = true; }

ScreenPtr _scrmk(char *msgBuffer) {
  t_PGMB *PGMB_loc = CMSGetPG();
  if (PGMB_loc->numAltRows < 0 && !initScreenInfo(msgBuffer)) {
    return NULL;
  }
  ScreenPtr screen = allocMem(sizeof(Screen));
  if (screen == NULL) {
    sprintf(
      msgBuffer,
      "Unable to allocate screen-object (size=%d)", sizeof(Screen));
    return NULL;
  }
  ScreenPublPtr pub = SCREENPUBL(screen);
  ScreenPrivPtr priv = SCREENPRIV(screen);

  pub->view     = (ViewPtr) allocMem(sizeof(struct _publicView)); /* 2025-02-05 */
  if (!pub->view) { /* OUT OF MEMORY */
    sprintf(
      msgBuffer,
      "Unable to allocate screen sub-structure VIEW (size=%d)", sizeof(struct _publicView));
    return NULL;
  }
  memset(pub->view, '\0', sizeof(struct _publicView));

  /* all screen data is zeroed out, so set only non-zero default values */
  pub->yyy_cmdLinePos = 1;
/*
  pub->prefixLen = 5;
  pub->prefixChar = '=';
*/
  pub->fillChar = ' ';
  pub->yyy_showTofBof = true;
  if (!PGMB_loc->canColors) {
    pub->yyy_attrMsg = DA_MonoIntens;
    pub->yyy_attrCurLine = DA_MonoIntens;
    pub->screenCanColors = false;
  } else {
    pub->yyy_attrFilearea = DA_Green;
    pub->yyy_attrPrefix = DA_Green;
    pub->yyy_attrFileToPrefix = DA_Blue;
    pub->yyy_attrCmd = DA_Turquoise;
    pub->yyy_attrArrow = DA_Green;
    pub->yyy_attrMsg = DA_Red;
    pub->yyy_attrHeadLine = DA_Blue;
    pub->yyy_attrFootLine = DA_Blue;
    pub->yyy_attrInfoLines = DA_Blue;
    pub->yyy_attrScaleLine = DA_Blue;
    pub->yyy_attrSelectedLine = DA_WhiteIntens;
    pub->yyy_attrCurLine = DA_WhiteIntens;
    pub->yyy_attrHighLight = DA_Yellow;
    pub->yyy_attrShadow = DA_Pink;
    pub->screenCanColors = true;
  }
  pub->screenRows = PGMB_loc->rows;
  pub->screenColumns = PGMB_loc->cols;

  pub->visibleEdLines = 8;

  pub->inputLinesAvail = 0;
  pub->cmdPrefixesAvail = 0;
  pub->cmdLine[0] = '\0';
  pub->aidCode = Aid_NoAID;

  return screen;
}

void _scrfr(ScreenPtr screen) {
  ScreenPublPtr pub = SCREENPUBL(screen);
  ScreenPrivPtr priv = SCREENPRIV(screen);
  if (pub->view) freeMem(pub->view);
  freeMem(screen);
}

/* internal screen build-up and roundtrip routine, possibly called twice
   by the public routine if the terminal connection was lost (disconnected).
*/
static int _scrio_inner(ScreenPtr screen) {
  t_PGMB *PGMB_loc = CMSGetPG();
  ScreenPublPtr pub = SCREENPUBL(screen);
  ScreenPrivPtr priv = SCREENPRIV(screen);
  int i;

  /* reset data input fields */
  pub->inputLinesAvail = 0;
  pub->cmdPrefixesAvail = 0;
  pub->cmdLine[0] = '\0';
  pub->cElemType = 99;
  pub->cElem = NULL;
  pub->cElemLineNo = 0;
  pub->cElemOffset = 0;
  pub->aidCode = Aid_NoAID;
  priv->edLinesUsed = 0;
  priv->cursorIsPlaced = false;

  /* is there anything to display? */
  if (pub->ed == NULL) {
    return -1;
  }
  ViewPtr view = pub->ed->view ;

  /* analyze message text for 'screen' */
  char *lineStarts[MAX_MSG_LINES];
  int lineLengths[MAX_MSG_LINES];
  int msgLineCount = countMsgLines(pub, lineStarts, lineLengths);

  /* gather information about infolines */
  char *infoLines[INFOLINES_MAX] ;
  { int i; for (i=0; i<INFOLINES_MAX; i++) { /* BUG: view->infoLines_p */ infoLines[i] = NULL; } }
  short infoLineCount     = 0;
  short infoLineCountTop  = 0;
  short infoLineCountFoot = 0;

  { char *p;
    int i;
    for (i=0; i<INFOLINES_MAX; i++) {
      if (p = view->infoLines_p[i])
      {
        infoLines[infoLineCount++] = p;
        char c0 = p[0];
        char c1 = p[1];
        if (/* DEBUG true || */ c0) {
          if (/* DEBUG true || */ (c0 != '.') || (c1 != '\0')) {
            if (i < INFOLINES_SPLIT)   { infoLineCountTop++;  }
                                  else { infoLineCountFoot++; }
          }
        }
      }
    }
  }

/* OLD CODE
  if (pub->infoLines[0]) {
    infoLines[infoLineCount++] = pub->infoLines[0];
    infoLineCountTop++;
  }
  if (pub->infoLines[1]) {
    infoLines[infoLineCount++] = pub->infoLines[1];
    infoLineCountTop++;
  }
  if (pub->infoLines[2]) {
    infoLines[infoLineCount++] = pub->infoLines[2];
    infoLineCountFoot++;
  }
  if (pub->infoLines[3]) {
    infoLines[infoLineCount++] = pub->infoLines[3];
    infoLineCountFoot++;
  }
/*

  /* compute screen characteristics */
  pub->ed->view->prefixLen = maxShort(1,minShort(5,pub->ed->view->prefixLen));
  short lineOverhead
    = (pub->ed->view->prefixMode == 0)
    ? 1 /* field start */
    : pub->ed->view->prefixLen + 2; /* prefix + 2xfieldstarts */
  pub->hShift
    = maxShort(
        0,
        minShort(
            pub->hShift,
            getWorkLrecl(pub->ed) + lineOverhead - pub->screenColumns));
  priv->hShiftEffective = (pub->readOnly && !pub->wrapOverflow)
    ? pub->hShift
    : 0;
  short reqLineCols
    = getWorkLrecl(pub->ed) + lineOverhead;
  short nominalTop
    = 1 /* headline */
    + ((pub->infoLinesPos < 0) ? infoLineCountTop : 0+infoLineCountTop)
    + ((pub->ed->view->cmdLinePos < 1) ? 1 : 0)
    + ((pub->msgLinePos < 1) ? 1 : 0);
  short nominalFoot
    = 1 /* footline */
    + ((pub->infoLinesPos > 0) ? infoLineCountFoot : 0+infoLineCountFoot)
    + ((pub->ed->view->cmdLinePos > 0) ? 1 : 0)
    + ((pub->msgLinePos > 0) ? 1 : 0);
  short reservedTop
    = nominalTop
    + ((pub->msgLinePos < 1) ? (msgLineCount-1) : 0);
  short reservedFoot
    = nominalFoot
    + ((pub->msgLinePos > 0) ? (msgLineCount-1) : 0);
  short scrFirstFootLine
    = PGMB_loc->rows - reservedFoot;
  short scrLinesPerEdLine
    = (pub->readOnly && !pub->wrapOverflow)
      ? 1
      : ((reqLineCols + PGMB_loc->cols - 1) / PGMB_loc->cols);
  short maxEdLinesOnScreen
    = (PGMB_loc->rows - nominalTop - nominalFoot) / scrLinesPerEdLine;
  short scrLineForCurr;
  short edLinesAboveCurr = 0;
  short edLinesBelowCurr = 0;
  short scrLineForScale = -1; /* ... no place for scale */
  short scrLineForFirstAboveCurr = -1;
  short scrLineForFirstBelowCurr = -1;
  short scrLineForTof = -1;
  short scrLineForBof = -1;
  if (pub->ed->view->currLinePos < 1) {
    /* curr-line on top below reserved, except if scale is above */
    if (pub->ed->view->scaleLinePos < 0 || pub->ed->view->scaleLinePos == 1) {
      scrLineForScale = reservedTop;
      scrLineForCurr = scrLineForScale + scrLinesPerEdLine;
      scrLineForFirstBelowCurr = scrLineForCurr + scrLinesPerEdLine;
    } else {
      scrLineForCurr = reservedTop;
      scrLineForFirstBelowCurr = scrLineForCurr + scrLinesPerEdLine;
      if (pub->ed->view->scaleLinePos > 1) {
        scrLineForScale = scrLineForCurr + scrLinesPerEdLine;
        scrLineForFirstBelowCurr = scrLineForScale + scrLinesPerEdLine;
      }
    }
  } else {
    /* curr-line in the middle */
    scrLineForCurr
      = nominalTop + ((maxEdLinesOnScreen / 2) * scrLinesPerEdLine);
    scrLineForFirstBelowCurr = scrLineForCurr + scrLinesPerEdLine;
    scrLineForFirstAboveCurr = scrLineForCurr;
    while((scrLineForFirstAboveCurr - scrLinesPerEdLine) >= reservedTop) {
      scrLineForFirstAboveCurr -= scrLinesPerEdLine;
      edLinesAboveCurr++;
    }
    if (pub->ed->view->scaleLinePos < 0) {
      /* scale on top */
      scrLineForScale = scrLineForFirstAboveCurr;
      scrLineForFirstAboveCurr += scrLinesPerEdLine;
      edLinesAboveCurr--;
    } else if (pub->ed->view->scaleLinePos == 1) {
      /* scale above curr-line */
      scrLineForScale = scrLineForCurr - scrLinesPerEdLine;
      if (scrLineForScale < reservedTop) {
        scrLineForScale = -1; /* no place */
      } else {
        edLinesAboveCurr--;
      }
    } else if (pub->ed->view->scaleLinePos > 1) {
      /* scale below curr-line */
      scrLineForScale = scrLineForCurr + scrLinesPerEdLine;
      if (scrLineForScale >= scrFirstFootLine) {
        scrLineForScale = -1; /* no space */
      } else {
        scrLineForFirstBelowCurr = scrLineForScale + scrLinesPerEdLine;
      }
      if (scrLineForFirstBelowCurr >= scrFirstFootLine) {
        scrLineForFirstBelowCurr = -1; /* no space */
      }
    }
  }
  if (scrLineForFirstBelowCurr > 0) {
    int cumulHeights = scrLinesPerEdLine;
    while((scrLineForFirstBelowCurr + cumulHeights) <= scrFirstFootLine) {
      cumulHeights += scrLinesPerEdLine;
      edLinesBelowCurr++;
    }
  }

  /* compute visible "page" height */
  pub->visibleEdLines = edLinesAboveCurr + 1 + edLinesBelowCurr;
  pub->visibleEdLinesBeforeCurrent = edLinesAboveCurr;
  pub->visibleEdLinesAfterCurrent = edLinesBelowCurr;

#ifdef _NOCMS
  printf("---------- _scrio() results:\n");
  printf(" <- lrecl = %d\n", getLrecl(pub->ed));
  printf(" <- cols = %d\n", cols);
  printf(" <- rows = %d\n", rows);
  printf("\n");
  printf(" <= prefixMode = %s\n",
    (pub->prefixMode==0)? "off" : (pub->prefixMode<0)? "left" : "right");
  printf(" <= prefixLen = %d\n", pub->prefixLen);
  printf(" <= readOnly = %s\n", (pub->readOnly)? "yes" : "no");
  printf(" <= wrapOverflow = %s\n", (pub->wrapOverflow)? "yes" : "no");
  printf(" <= currLinePos = %s\n", (pub->currLinePos<1)? "top" : "middle");
  printf(" <= scaleLinePos = %s\n",
    (pub->scaleLinePos == 0)? "off"
    : (pub->scaleLinePos < 0)? "top"
    : (pub->scaleLinePos == 1)? "before curr" : "below curr");
  printf(" <= cmdLinePos = %s\n", (pub->cmdLinePos<1)? "top" : "bottom");
  printf(" <= msgLinePos = %s\n", (pub->msgLinePos<1)? "top" : "bottom");
  printf("\n");
  printf("  -> msgLineCount = %d\n", msgLineCount);
  printf("\n");
  printf("  -> nominalTop   = %d\n", nominalTop);
  printf("  -> nominalFoot  = %d\n", nominalFoot);
  printf("  -> reservedTop  = %d\n", reservedTop);
  printf("  -> reservedFoot = %d\n", reservedFoot);
  printf("\n");
  printf("  -> reqLineCols        = %d\n", reqLineCols);
  printf("  -> scrLinesPerEdLine  = %d\n", scrLinesPerEdLine);
  printf("  -> maxEdLinesOnScreen = %d\n", maxEdLinesOnScreen);
  printf("\n");
  printf("  -> scrLineForCurr = %d\n", scrLineForCurr);
  printf("\n");
  printf("  -> edLinesAboveCurr = %d\n", edLinesAboveCurr);
  printf("  -> edLinesBelowCurr = %d\n", edLinesBelowCurr);
  printf("\n");
  printf("  -> scrLineForScale  = %d\n", scrLineForScale);
  printf("  -> scrFirstFootLine = %d\n", scrFirstFootLine);
  printf("  -> scrLineForFirstAboveCurr = %d\n", scrLineForFirstAboveCurr);
  printf("  -> scrLineForFirstBelowCurr = %d\n", scrLineForFirstBelowCurr);
  printf("\n");

  pub->inputLinesAvail = 0;
  pub->cmdPrefixesAvail = 0;
  pub->cmdLine[0] = '\0';
  pub->aidCode = Aid_NoAID;
  /*return -2;*/

#endif

  /* fetch the lines to be displayed from the editor */
  LinePtr uplines[MAX_ED_LINES];                           /* 2025-01-07 DEBUG: was 64, now 256 */
  unsigned int uplinesCount = 0;
  LinePtr downlines[MAX_ED_LINES];                         /* 2025-01-07 DEBUG: was 64, now 256 */
  unsigned int downlinesCount = 0;
  LinePtr currLine;
  unsigned int currLineNo = 0;
  int firstUplineNo = -1;

  Printf1(" -- preparing for getLineFrame, sizeof(LinePtr) = %d\n",
    sizeof(LinePtr));

  memset(uplines, '\0', MAX_ED_LINES * sizeof(LinePtr));   /* 2025-01-07 DEBUG: was 64, now 256 */
  memset(downlines, '\0', MAX_ED_LINES * sizeof(LinePtr)); /* 2025-01-07 DEBUG: was 64, now 256 */
  currLine = NULL;

  Printf2(" -- doing getLineFrame(ed, %d, ..., %d, ...)\n",
    edLinesAboveCurr, edLinesBelowCurr);

  edLinesBelowCurr++; /* 2025-01-18 dirty hack for debugging */
  getLineFrame(pub->ed,
    edLinesAboveCurr, uplines, &uplinesCount,
    &currLine, &currLineNo,
    edLinesBelowCurr, downlines, &downlinesCount);
  if (uplinesCount > 0) {
    firstUplineNo = currLineNo - uplinesCount;
  }
  if ((pub->cursorPlacement == 1 || pub->cursorPlacement == 2)
      && pub->cursorLine == NULL) {
    pub->cursorLine = currLine;
  }

  Printf3(" -- uplinesCount = %d, currLineNo = %d, downlinesCount = %d\n",
    uplinesCount, currLineNo, downlinesCount);

  if (uplinesCount < edLinesAboveCurr) {
    scrLineForFirstAboveCurr
      += scrLinesPerEdLine * (edLinesAboveCurr - uplinesCount);
    if (pub->ed->view->showTofBof && currLine != NULL) {
      scrLineForTof = scrLineForFirstAboveCurr - scrLinesPerEdLine;
    }
  }

  if (downlinesCount < edLinesBelowCurr && pub->ed->view->showTofBof) {
    scrLineForBof
      = scrLineForFirstBelowCurr + (scrLinesPerEdLine * downlinesCount);
  }

  /* build up the 3270 screen */
  bool cmdPrefilled = (pub->cmdLinePrefill && *(pub->cmdLinePrefill));
  char cmdLineModifier = (cmdPrefilled) ? 64 : 0;
  int maxCmdLen = minInt(PGMB_loc->lastCol - strlen(PGMB_loc->cmdArrow) - 1, CMDLINELENGTH);

  /*char wccFlags = WCC_KbdRestore | WCC_ResetMDT;*/
  char wccFlags = WCC_KbdRestore | WCC_Reset;
  if (pub->doBeep) { wccFlags |= WCC_SoundAlarm; }
  if (PGMB_loc->canAltScreenSize) {
    strtEWA(wccFlags, PGMB_loc->rows, PGMB_loc->cols);
  } else {
    strtEW(wccFlags);
  }

  Printf0(" -- done strtEW(A)\n");

  /* headline */
  SBA(PGMB_loc->lastRow, PGMB_loc->lastCol);
  startField2(pub->ed->view->attrHeadLine, pub->ed->view->HiLitHeadLine, true, false);
  addWidenedLine(pub, pub->headLine);
  startField2(pub->ed->view->attrEMPTY, pub->ed->view->HiLitEMPTY, true, false);
  Printf0(" -- headline written\n");

  int currRow = 0;
  SBA(currRow++, PGMB_loc->lastCol);

/*
  / * infolines on top ? * /
  if (pub->infoLinesPos < 0) {
    for (i = 0; i < infoLineCount; i++) {
      startField2(pub->attrInfoLines, pub->HiLitInfoLines, true, false);
      appendStringWithLength(
        infoLines[i], maxInt(strlen(infoLines[i]), PGMB_loc->lastCol), (char)0x00);
      SBA(currRow++, PGMB_loc->lastCol);
    }
  }
  startField2(pub->attrEMPTY, pub->HiLitEMPTY, true, false);
*/
  /* commandline on top ? */
  if (pub->ed->view->cmdLinePos <= 0) {
    startField2(pub->ed->view->attrArrow, pub->ed->view->HiLitArrow, true, false);

    SA_BGC(Color_Pink);
    SA_C(Color_Red);
    appendString("=");
    SA_C(Color_Green);
    appendString("=");
    SA_C(Color_Blue);
    appendString("=");
    SA_C(Color_Yellow);
    appendString("=");

    SA_C(Color_Turquoise);
    SA_H(HiLit_Blink);
    appendString(">");
    SA_C(Color_Default);
    SA_BGC(Color_Default);
    SA_H(HiLit_Default);


    /* appendString(PGMB_loc->cmdArrow); */
    startField2(pub->ed->view->attrCmd + cmdLineModifier, pub->ed->view->HiLitCmd, pub->cmdLineReadOnly, false);
    GBA(&priv->cmdRow, &priv->cmdCol); /* remember position of command field */
    if (pub->cursorOffset == 0
        && (pub->cursorPlacement < 1 || pub->cursorPlacement > 2)) {
        doIC();
    }
    if (cmdPrefilled) {
      appendStringWithLength(pub->cmdLinePrefill, maxCmdLen, (char)0x00);
      startField(DA_Mono, true, false);
    }
    if (pub->cursorOffset > 0
        && (pub->cursorPlacement < 1 || pub->cursorPlacement > 2)) {
      SBA(
        priv->cmdRow,
        priv->cmdCol + maxInt(minInt(pub->cursorOffset, maxCmdLen), 0));
      doIC();
    }
    SBA(currRow++, PGMB_loc->lastCol);
    /* BUG? startField2(pub->attrEMPTY, pub->HiLitEMPTY, true, false); */
  }

  /* infolines on top ? */
  if (true && infoLineCountTop > 0) /* was 'pub->infoLinesPos < 0' */ {
    for (i = 0; i < 0+infoLineCountTop; i++) {
      startField2(pub->ed->view->attrInfoLines, pub->ed->view->HiLitInfoLines, true, false);
      appendStringWithLength(
        infoLines[i], maxInt(strlen(infoLines[i]), PGMB_loc->lastCol), (char)0x00);
      SBA(currRow++, PGMB_loc->lastCol);
    }
  }
  startField2(pub->ed->view->attrEMPTY, pub->ed->view->HiLitEMPTY, true, false);

  /* message line(s) on top ? */
  if (pub->msgLinePos <= 0) {
    for(i = 0; i < msgLineCount; i++) {
      startField2(pub->ed->view->attrMsg, pub->ed->view->HiLitMsg, true, false);
      appendStringWithLength(lineStarts[i], lineLengths[i], (char)0x00);
      startField2(pub->ed->view->attrEMPTY, pub->ed->view->HiLitEMPTY, true, false);
      SBA(currRow++, PGMB_loc->lastCol);
    }
  }

  /* ------------   file & [scale] incl. filling priv parts ---------- */
  /* scale on top ? */
  if (pub->ed->view->scaleLinePos < 0 && scrLineForScale > 0)  {
    SBA(scrLineForScale - 1, PGMB_loc->lastCol);
    writeScale(pub);
    startField2(pub->ed->view->attrEMPTY, pub->ed->view->HiLitEMPTY, true, false);
  }

  /* top of file marker visible above curr line ? */
  pub->ed->view->flscreen1 = -1;
  pub->ed->view->flscreen2 = -1;
  if (scrLineForTof > 0) {
    pub->ed->view->flscreen1 = pub->ed->view->flscreen2 = 0;  /* ToDo: Top of Range */
    SBA(scrLineForTof - 1, PGMB_loc->lastCol);
    writeTextAsFileMarker(
       pub,
       priv,
       PGMB_loc->topOfFileText,
       0,
       scrLinesPerEdLine,
       false,
       true);
  }

  /* file lines above curr line */
  if (scrLineForFirstAboveCurr > 0 && uplinesCount > 0) {
    /***** int upCurrLineNo = firstUplineNo; *****/
    currRow = scrLineForFirstAboveCurr - 1;
    SBA(currRow, PGMB_loc->lastCol);
    currRow +=  scrLinesPerEdLine;
    char *prefixPrefill = getCurrPrefixMark(pub, uplines[0]);
    if (uplinesCount > 0) {
      /* ToDo: check for TOF/EOF */
      if (pub->ed->view->flscreen1 < 0)
        pub->ed->view->flscreen1 = getLineNumber(uplines[0]);
      pub->ed->view->flscreen2   = getLineNumber(uplines[uplinesCount-1]);
    }
    for (i = 0; i < uplinesCount; i++) {
      if (uplines[i] == pub->prefixMarks[0].forLine) {
        prefixPrefill = pub->prefixMarks[0].prefixPrefill;
      } else if (uplines[i] == pub->prefixMarks[1].forLine) {
        prefixPrefill = pub->prefixMarks[1].prefixPrefill;
      }
      writeFileLine(
       pub,
       priv,
       uplines[i],
       getLineNumber(uplines[i]),  /***** was "upCurrLineNo++" *****/
       scrLinesPerEdLine,
       false,
       prefixPrefill);
      SBA(currRow, PGMB_loc->lastCol);
      currRow +=  scrLinesPerEdLine;
      if (uplines[i] == pub->prefixMarks[1].forLine) {
        prefixPrefill = NULL;
      } else if (uplines[i] == pub->prefixMarks[0].forLine) {
        prefixPrefill = PGMB_loc->prefixLocked;
      }
    }
  }

  /* scale above curr line ? */
  if (pub->ed->view->scaleLinePos == 1 && scrLineForScale > 0)  {
    SBA(scrLineForScale - 1, PGMB_loc->lastCol);
    writeScale(pub);
  }

  /* curr file line */
  int skipCurlineTOF = 0; /* assume that curline is displayed */
  SBA(scrLineForCurr - 1, PGMB_loc->lastCol);
  if (currLine) {
    pub->ed->view->flscreen2 = getLineNumber(currLine);
    if (pub->ed->view->flscreen1 < 0) {
      pub->ed->view->flscreen1 = pub->ed->view->flscreen2;
    }
    writeFileLine(
       pub,
       priv,
       currLine,
       getLineNumber(currLine),
       scrLinesPerEdLine,
       true,
       getCurrPrefixMark(pub, currLine));
  } else if (pub->ed->view->showTofBof) {
    /* top of file */
    pub->ed->view->flscreen1 = pub->ed->view->flscreen2 = 0;
    writeTextAsFileMarker(
       pub,
       priv,
       PGMB_loc->topOfFileText,
       0,
       scrLinesPerEdLine,
       true,
       true);
  } else {
    /* if not showTofBof, the editor should be moved to line 1 instead of above first line !! */
    skipCurlineTOF = 1; /* we know that curline is not displayed */
  }


  /* scale below curr line ? */
  if (pub->ed->view->scaleLinePos == 2 && scrLineForScale > 0)  {
    SBA(scrLineForScale - 1 - skipCurlineTOF, PGMB_loc->lastCol);
    writeScale(pub);
  }

  /* file lines below curr line */
  if /*ToDo*/ (scrLineForFirstBelowCurr > 0 && downlinesCount > 0) {
    /***** int downCurrLineNo = currLineNo + 1; *****/
    downlinesCount = downlinesCount + skipCurlineTOF;  /* 2025-01-18 new: skipCurlineTOF */
    currRow = scrLineForFirstBelowCurr - 1 - skipCurlineTOF;
    SBA(currRow, PGMB_loc->lastCol);
    currRow +=  scrLinesPerEdLine;
    char *prefixPrefill = getCurrPrefixMark(pub, downlines[0]);
    if (downlinesCount > 0) {
      /* ToDo: check for TOF/EOF */
      pub->ed->view->flscreen2 = getLineNumber(downlines[downlinesCount-1]);
    }
    for (i = 0; i < downlinesCount; i++) {
      if (downlines[i] == pub->prefixMarks[0].forLine) {
        prefixPrefill = pub->prefixMarks[0].prefixPrefill;
      } else if (downlines[i] == pub->prefixMarks[1].forLine) {
        prefixPrefill = pub->prefixMarks[1].prefixPrefill;
      }
      writeFileLine(
       pub,
       priv,
       downlines[i],
       getLineNumber(downlines[i]),  /***** was "downCurrLineNo++" *****/
       scrLinesPerEdLine,
       false,
       prefixPrefill);
      SBA(currRow, PGMB_loc->lastCol);
      currRow +=  scrLinesPerEdLine;
      if (downlines[i] == pub->prefixMarks[1].forLine) {
        prefixPrefill = NULL;
      } else if (downlines[i] == pub->prefixMarks[0].forLine) {
        prefixPrefill = PGMB_loc->prefixLocked;
      }
    }
  }

  /* bottom of file visible below curr line ? */
  if (scrLineForBof > 0) {
    SBA(scrLineForBof - 1, PGMB_loc->lastCol);
    pub->ed->view->flscreen2 = getLineCount(pub->ed)+1;
    writeTextAsFileMarker(
       pub,
       priv,
       PGMB_loc->bottomOfFileText,
       getLineCount(pub->ed)+1,
       scrLinesPerEdLine,
       false,
       true /* was "false" */);
  }

  /* footlines */
  currRow = scrFirstFootLine - 1;
  SBA(currRow++,PGMB_loc->lastCol);

  /* message line(s) at bottom ? */
  if (pub->msgLinePos > 0) {
    for(i = 0; i < msgLineCount; i++) {
      startField2(pub->ed->view->attrMsg, pub->ed->view->HiLitMsg, true, false);
      appendStringWithLength(lineStarts[i], lineLengths[i], (char)0x00);
      SBA(currRow++, PGMB_loc->lastCol);
    }
  }

  /* infolines on bottom ? */
  if (true && infoLineCountFoot > 0) /* was 'pub->infoLinesPos > 0' */ {
    for (i = 0+INFOLINES_SPLIT; i < INFOLINES_SPLIT+infoLineCountFoot; i++) {
      startField2(pub->ed->view->attrInfoLines, pub->ed->view->HiLitInfoLines, true, false);
      appendStringWithLength(
        infoLines[i], maxInt(strlen(infoLines[i]), PGMB_loc->lastCol), (char)0x00);
      SBA(currRow++, PGMB_loc->lastCol);
    }
  }

  /* commandline at bottom ? */
  if (pub->ed->view->cmdLinePos > 0) {
    startField2(pub->ed->view->attrArrow, pub->ed->view->HiLitArrow, true, false);
    appendString(PGMB_loc->cmdArrow);
    startField2(pub->ed->view->attrCmd + cmdLineModifier, pub->ed->view->HiLitCmd, pub->cmdLineReadOnly, false);
    GBA(&priv->cmdRow, &priv->cmdCol); /* remember position of command field */
    if (pub->cursorOffset == 0
        && (pub->cursorPlacement < 1 || pub->cursorPlacement > 2)) {
        doIC();
    }
    if (cmdPrefilled) {
      appendStringWithLength(pub->cmdLinePrefill, maxCmdLen, (char)0x00);
      startField(DA_Mono, true, false);
    }
    if (pub->cursorPlacement < 1 || pub->cursorPlacement > 2) {
      SBA(
        priv->cmdRow,
        priv->cmdCol + maxInt(minInt(pub->cursorOffset, maxCmdLen), 0));
      doIC();
    }
    SBA(currRow++, PGMB_loc->lastCol);
  }

  /* final footline */
  startField2(pub->ed->view->attrFootLine, pub->ed->view->HiLitFootLine, true, false);
  addWidenedLine(pub, pub->footLine);

  /* force cursor place in command line if targeted place was not found */
  if (!priv->cursorIsPlaced) {
    SBA(priv->cmdRow, priv->cmdCol);
    doIC();
  }

  /*printf(" -- stopping before sending screen\n");
  return 99;*/

  /* remember the line range displayed */
  pub->firstLineVisible
    = (uplinesCount > 0) ? uplines[0] : getFirstLine(pub->ed);
  pub->lastLineVisible
    = (downlinesCount > 0) ? downlines[downlinesCount-1] : currLine;

  /* send fullscreen */
  int rc = fs_tsnd();
  if (rc != 0) { return rc; }

  /* read fullscreen input */
  char aidCode;
  unsigned int cursorRow;
  unsigned int cursorCol;
  rc = fs_trcv(&aidCode, &cursorRow, &cursorCol);
  if (rc != 0) { return rc; }

  /* interpret data from screen and fill data for caller */
    /* AID and cursor position */
  pub->aidCode = aidCode;
  pub->cRowAbs = cursorRow;
  pub->cColAbs = cursorCol;
  int lineOffset = scrLinesPerEdLine - 1;
  int prefixLen = pub->ed->view->prefixLen;
  if (cursorRow == priv->cmdRow
      && (cursorCol >= priv->cmdCol && cursorCol <= priv->cmdCol + maxCmdLen)) {
      pub->cElemType = 0; /* cmd */
      pub->cElem = NULL;
      pub->cElemLineNo = 0;
      pub->cElemOffset = cursorCol - priv->cmdCol;
  } else if (cursorRow < reservedTop
             || cursorRow >= scrFirstFootLine
             || (cursorRow >= scrLineForScale
                 && cursorRow < (scrLineForScale + scrLinesPerEdLine))) {
    pub->cElemType = 99; /* not file/prefix area, not cmdline */
  } else {
    for (i = 0; i < priv->edLinesUsed; i++) {
      EdLinePlace *li = &priv->edLinePlaces[i];
      if (cursorRow == li->prefixRow
          && (cursorCol >= li->prefixCol
              && cursorCol < (li->prefixCol + prefixLen))
          && pub->ed->view->prefixMode != 0) {
        pub->cElemType = 1; /* prefix */
        pub->cElem = li->edLine;
        pub->cElemLineNo = li->edLineNo;
        pub->cElemOffset = cursorCol - li->prefixCol;
        break;
      } else if ((cursorRow >= li->txtRow && cursorCol >= li->txtCol)
          && (cursorRow < li->txtRow + scrLinesPerEdLine)) {
        pub->cElemType = 2; /* file content */
        pub->cElem = li->edLine;
        pub->cElemLineNo = li->edLineNo;
        pub->cElemOffset
          = (cursorCol - li->txtCol)
          + ((cursorRow - li->txtRow) * PGMB_loc->cols)
          + priv->hShiftEffective;
        break;
      }
    }
  }

#if 0
  if (pub->aidCode == Aid_PA02) {
    printf("PA02 => edLinesUsed: %d\n", priv->edLinesUsed);
    for(i = 0; i < priv->edLinesUsed; i++) {
      EdLinePlace *edp = &priv->edLinePlaces[i];
      printf(" place[%d] = line[%d,%d] prefix[%d,%d]\n",
        i, edp->txtRow, edp->txtCol, edp->prefixRow, edp->prefixCol);
    }
  }
#endif

    /* modifications in input fields */
  int fldLen;
  char *fldStart;
  unsigned int fldRow;
  unsigned int fldCol;
  while(fs_nxtf(&fldRow, &fldCol, &fldStart, &fldLen)) {
    if (fldRow == priv->cmdRow && fldCol == priv->cmdCol) {
      /* text in command line */
      memset(pub->cmdLine, '\0', CMDLINELENGTH + 1);
      memcpy(pub->cmdLine, fldStart, minInt(fldLen, maxCmdLen));
    } else {
      for (i = 0; i < priv->edLinesUsed; i++) {
        EdLinePlace *edp = &priv->edLinePlaces[i];
        if (fldRow == edp->txtRow && fldCol == edp->txtCol) {
          /* text in file area for an ed line */
          LineInput *li = &pub->inputLines[pub->inputLinesAvail++];
          li->line = edp->edLine;
          li->lineNo = edp->edLineNo;
          li->newText = fldStart;
          char *fldEnd = fldStart + fldLen - 1;
          while(fldLen > 0 && *fldEnd == ' ') {
            fldLen--;
            fldEnd--;
          }
          li->newTextLength = fldLen;
          break;
        } else if (pub->ed->view->prefixMode > 0
                   && fldRow == edp->prefixRow && fldCol == edp->prefixCol) {
          /* text in prefix area for an ed line */
          PrefixInput *pi = &pub->cmdPrefixes[pub->cmdPrefixesAvail++];
          pi->line = edp->edLine;
          pi->lineNo = edp->edLineNo;

          int plen = 0;
          char *ref = edp->prefixFill;
          char *dst = &pi->prefixCmd[0];
          char *src = fldStart;
          char *guard = dst;
          memset(dst, '\0', PREFIXLENGTH + 1);
          while(plen < fldLen) {
            if (*src != *ref /* && *src != ' ' */) {
              *dst++ = *src;
            }
            src++;
            ref++;
            plen++;
          }

          plen = dst - pi->prefixCmd;
          while(plen > 0 && pi->prefixCmd[plen - 1] == ' ') {
            pi->prefixCmd[plen - 1] = '\0';
            plen--;
          }

          if (dst == guard) {
            /* prefix zone left unchanged (i.e. edited back to original) ? */
            pub->cmdPrefixesAvail--; /* -> forget this prefix input */
          }
          break;
        }
      }
    }
  }

  /* that's it */
  return 0;
}

/* the public screen-i/o routine
*/
int _scrio(ScreenPtr screen) {
  ScreenPublPtr pub = SCREENPUBL(screen);
  int result = _scrio_inner(screen);
  while (result == 2 || result == 3 || pub->aidCode == Aid_PA03) {
    printf("++++++\n");
    printf("++++++ re-querying screen informations\n");
    printf("++++++\n");
    char messages[512];
    if (!initScreenInfo(messages)) {
      if (pub->aidCode != Aid_PA03) {
        printf(
          "** Unable to re-establish a fullscreen session after disconnect\n");
      } else {
        printf("** Unable to re-query screen characteristics\n");
      }
      printf("** Error message:\n");
      printf("%s\n", messages);
      return FS_SESSION_LOST;
    }
    result = _scrio_inner(screen);
  }
  return result;
}

/*
** internal methods
*/

static bool initScreenInfo(char *msgBuffer) {
  t_PGMB *PGMB_loc = CMSGetPG();
  int rc = __qtrm(
      PGMB_loc->termName,
      sizeof(PGMB_loc->termName),
      &PGMB_loc->numAltRows,
      &PGMB_loc->numAltCols,
      &PGMB_loc->canAltScreenSize,
      &PGMB_loc->canExtHighLight,
      &PGMB_loc->canColors,
      &PGMB_loc->sessionId,
      &PGMB_loc->sessionMode);
  if (rc != 0) {
    sprintf(
       msgBuffer,
       "No fullscreen support present (MECAFF::__qtrm() -> rc = %d)\n",
       rc);
    return false;
  }
  if (PGMB_loc->canAltScreenSize) {
    PGMB_loc->rows = PGMB_loc->numAltRows;
    PGMB_loc->cols = PGMB_loc->numAltCols;
  } else {
    PGMB_loc->rows = 24;
    PGMB_loc->cols = 80;
  }
  if (PGMB_loc->rows == 24 && PGMB_loc->cols == 80) { PGMB_loc->canAltScreenSize = 0; }
  PGMB_loc->lastRow = PGMB_loc->rows - 1;
  PGMB_loc->lastCol = PGMB_loc->cols - 1;

  Printf0("_scrmk -- initialized 3270 subsystem\n");
  Printf4("  rows = %d, cols = %d, canAltScreenSize = %d, canColor = %d\n",
      rows, cols, canAltScreenSize, canColors);
  Printf2("  sessionId = %d, sessionMode = %d\n", sessionId, sessionMode);
  Printf2("  sessionId = %d, sessionMode = %d\n", sessionId, sessionMode);

  return true;
}

static int countMsgLines(
    ScreenPublPtr scr,
    char *lineStarts[MAX_MSG_LINES],
    int lineLengths[MAX_MSG_LINES]) {
  t_PGMB *PGMB_loc = CMSGetPG();
  int lineCount;
  int len = 0;
  int i;
  char *msg = scr->msgText;

  /* initialize out data */
  lineCount = 1;
  for(i = 0; i < MAX_MSG_LINES; i++) {
    lineStarts[i] = NULL;
    lineLengths[i] = 0;
  }

  /* if no msg => no empty line : assume SET MSGLINE ... OVERLAY */
  if (!msg || !*msg) { return 0; }
  /* if no msg => one empty line */
  if (!msg || !*msg) { return lineCount; }

  /* parse the message for line ends and collect max. MAX_MSG_LINES lines */
  lineStarts[lineCount - 1] = msg;
  while(*msg && (lineCount <= MAX_MSG_LINES)) {
    if (*msg == '\n') {
      lineLengths[lineCount - 1] = minInt(PGMB_loc->lastCol, len);
      if (lineCount == MAX_MSG_LINES) { return lineCount; }
      lineStarts[lineCount] = msg + 1;
      lineCount++;
      len = 0;
    } else {
      len++;
    }
    msg++;
  }
  lineLengths[lineCount - 1] = minInt(PGMB_loc->lastCol, len);

  /* that's it */
  return lineCount;
}

/* expand embedded tab-characters separating left/middle/right
*/
static void addWidenedLine(ScreenPublPtr scr, char *line) {
  t_PGMB *PGMB_loc = CMSGetPG();
  if (!line || !*line) { return; }

  Printf1("++ addWidenedLine, line:\n>>%s<<\n", line);

  int lineLen = 0;
  int tabCnt = 0;
  char *p = line;
  while(*p) {
    lineLen++;
    if (*p == '\t') { tabCnt++; }
    p++;
  }

  Printf2("++ linelen = %d, tabCnt = %d\n", lineLen, tabCnt);
  if (tabCnt == 0) {
    appendStringWithLength(line, minInt(lineLen,PGMB_loc->lastCol), (char)0x00);
    return;
  }

  char fillChar = scr->fillChar;
  int fillCnt = maxInt(0, PGMB_loc->lastCol - lineLen);
  int fillForTab = maxInt(0, (fillCnt - tabCnt) / tabCnt);
  p = line;
  lineLen = minInt(PGMB_loc->lastCol, lineLen);
  Printf4("++ fillChar: %c, fillCnt = %d, fillForTab= %d, lineLen = %d\n",
    fillChar, fillCnt, fillForTab, lineLen);

  int i;
  while(*p && lineLen) {
    if (*p == '\t') {
      appendChar(fillChar);
      tabCnt--;
      i = (tabCnt) ? fillForTab : fillCnt;
      while(i) {
        appendChar(fillChar);
        i--;
        fillCnt--;
      }
    } else {
      appendChar(*p);
    }
    p++;
    lineLen--;
  }
}

static void startField2(unsigned char pubAttr, unsigned char pubHiLit, bool readonly, bool autoSkip) {
  t_PGMB *PGMB_loc = CMSGetPG();
  char attr3270 = (!readonly && pubAttr >= 64)
         ? FldAttr_Modified
         : FldAttr_None;

  pubAttr = pubAttr & 0x0F;

  if (pubAttr & 0x01) { attr3270 |= FldAttr_Intensified; }
  if (readonly) {
    attr3270 |= FldAttr_Protected;
    if (autoSkip) { attr3270 |= FldAttr_Numeric; }
  }

  if (PGMB_loc->canColors) {
    SFE(attr3270, pubHiLit, PGMB_loc->colorsFor3270[pubAttr]);
  } else {
    SF(attr3270);
  }
}
static void startField(unsigned char pubAttr, bool readonly, bool autoSkip) {
  /* DEBUG: HiLit_None -> HiLit_Underscore, HiLit_Blink */
  startField2(pubAttr, HiLit_Blink, readonly, autoSkip);
}


static char *digits = "0123456789";

static void writeScale(ScreenPublPtr scr) {
  t_PGMB *PGMB_loc = CMSGetPG();
  int i;
  int inset
    = (scr->ed->view->prefixMode) ? scr->ed->view->prefixLen + 1 : 0;
  int lrecl = getWorkLrecl(scr->ed);
  int scaleWidth
    = (!scr->readOnly || scr->wrapOverflow)
    ? lrecl
    : minInt(PGMB_loc->lastCol - inset - 1, lrecl);
  int firstMarked = -1;
  int lastMarked = -1;

  if (scr->scaleMark && scr->scaleMarkStart >= 0 && scr->scaleMarkLength > 0) {
    firstMarked = scr->scaleMarkStart + 1;
    lastMarked = firstMarked + scr->scaleMarkLength - 1;
    scr->scaleMark = false;
    scr->scaleMarkStart = -1;
    scr->scaleMarkLength = -1;
  }

  startField2(scr->ed->view->attrScaleLine, scr->ed->view->HiLitScaleLine, true, false);

  if (scr->ed->view->prefixMode == 1) {
    for (i = 0; i < inset; i++) {
      appendChar(' ');
    }
  }

  int tabs[MAX_TAB_COUNT];
  int tabCount = getTabs(scr->ed, tabs);
  for (i = 0; i < tabCount; i++) {
    tabs[i]++;
  }
  int currTab= 0;

  for (i = 1; i <= scaleWidth; i++) {
    if (i >= firstMarked && i <= lastMarked) {
      appendChar('#');
    } else if (i == tabs[currTab]) {
      appendChar('|');
      currTab++;
      if (currTab == MAX_TAB_COUNT) { currTab = 0; }
    } else if ((i % 10) == 0) {
      appendChar(digits[(i / 10) % 10]);
    } else if ((i %5) == 0) {
      appendChar('+');
    } else {
      appendChar('.');
    }
  }
}

static char* getCurrPrefixMark(ScreenPublPtr scr, LinePtr line) {
  t_PGMB *PGMB_loc = CMSGetPG();
  if (!line) {
    /*printf("getCurrPrefixMark() -> line is NULL\n");*/
    return NULL;
  }

  if (line == scr->prefixMarks[0].forLine) {
    /*printf("getCurrPrefixMark() -> is scr->prefixMarks[0]\n");*/
    return scr->prefixMarks[0].prefixPrefill;
  } else if (line == scr->prefixMarks[1].forLine) {
    /* printf("getCurrPrefixMark() -> is scr->prefixMarks[1]\n");*/
    return scr->prefixMarks[1].prefixPrefill;
  } else if (isInLineRange(scr->ed,
                           line,
                           scr->prefixMarks[0].forLine,
                           scr->prefixMarks[1].forLine)) {
    /*printf("getCurrPrefixMark() -> is in line range!\n");*/
    return PGMB_loc->prefixLocked;
  }
  /*printf("getCurrPrefixMark() -> not marked\n");*/
  return NULL;
}

static void writePrefix(
    ScreenPublPtr pub,
    ScreenPrivPtr priv,
    EdLinePlace *lineInfo,
    unsigned int lineNo,
    char *prefixPrefill) {
  t_PGMB *PGMB_loc = CMSGetPG();
  char tmp[6];
  if (prefixPrefill && *prefixPrefill) {
    strncpy(tmp, PGMB_loc->prefixLocked, 5);
    tmp[5] = '\0';
    memcpy(tmp, prefixPrefill, minInt(strlen(prefixPrefill),5));
  } else if (pub->ed->view->prefixNumbered) {     /* assume "(SET) PREfix Nulls" */
    sprintf(tmp, "%5d", lineNo);        /* was "sprintf(tmp, "%05d", lineNo);" */
  } else {
    memset(tmp, pub->ed->view->prefixChar, 5);
    tmp[5] = '\0';
  }
  memset(lineInfo->prefixFill, '\0', sizeof(lineInfo->prefixFill));
  strncpy(lineInfo->prefixFill, tmp + 5 - pub->ed->view->prefixLen, pub->ed->view->prefixLen);
  GBA(&lineInfo->prefixRow, &lineInfo->prefixCol);
  if (pub->cursorLine == lineInfo->edLine
      && pub->cursorPlacement == 1
      && pub->cursorOffset <= 0) {
    doIC();
  }
  appendString(lineInfo->prefixFill);
  if (pub->cursorLine == lineInfo->edLine
      && pub->cursorPlacement == 1
      && pub->cursorOffset > 0) {
    unsigned int tmpRow;
    unsigned int tmpCol;
    GBA(&tmpRow, &tmpCol);
    SBA(
      lineInfo->prefixRow,
      lineInfo->prefixCol
      + maxInt(minInt(pub->cursorOffset, pub->ed->view->prefixLen), 0));
    doIC();
    SBA(tmpRow, tmpCol);
  }
}

static void writeFileLine(
    ScreenPublPtr pub,
    ScreenPrivPtr priv,
    LinePtr line,
    unsigned int lineNo,
    short scrLinesPerEdLine,
    bool isCurrentLine,
    char *prefixPrefill) {
  t_PGMB *PGMB_loc = CMSGetPG();
  int lrecl = getWorkLrecl(pub->ed);
  EdLinePlace *lineInfo = &priv->edLinePlaces[priv->edLinesUsed++];
  int isLocked = (prefixPrefill && *prefixPrefill);
  bool isSelected = (pub->selectionColumn
                    && line->text[pub->selectionColumn] == pub->selectionMark);
  char *pfixPrefill = (isSelected) ? "»»»»»" : prefixPrefill;
  bool isExcluded = !(isInDisplayRange(line));

  lineInfo->edLine = line;
  lineInfo->edLineNo = lineNo;
  unsigned char attr  = (isCurrentLine) ?  pub->ed->view->attrCPrefix :  pub->ed->view->attrPrefix;
  unsigned char HiLit = (isCurrentLine) ? pub->ed->view->HiLitCPrefix : pub->ed->view->HiLitPrefix;
  if (isExcluded) { attr = pub->ed->view->attrShadow; HiLit = pub->ed->view->HiLitShadow; }

  /* prefix before file line text ? */
  if (pub->ed->view->prefixMode == 1) {
    startField2(attr, HiLit, pub->prefixReadOnly || isLocked, false);
    writePrefix(pub, priv, lineInfo, lineNo, pfixPrefill);
  }

  /* start the file line text and remember position of input field */
  attr = (isCurrentLine) ? pub->ed->view->attrCurLine : pub->ed->view->attrFilearea;
  HiLit = (isCurrentLine) ? pub->ed->view->HiLitCurLine : pub->ed->view->HiLitFilearea;
  if (line->selectionLevel > 0) /* assume "SET HIGHLIGHT SELECT 1 *" */                       {
    attr  = (isCurrentLine) ? pub->ed->view->attrCHighLight : pub->ed->view->attrHighLight;
    HiLit = (isCurrentLine) ? pub->ed->view->HiLitCHighLight : pub->ed->view->HiLitHighLight;
  }
  if (isExcluded) { attr = pub->ed->view->attrShadow; HiLit = pub->ed->view->HiLitShadow; }
  if (isSelected) { attr = pub->ed->view->attrSelectedLine; HiLit = pub->ed->view->HiLitSelectedLine; }
  startField2(
    attr,
    HiLit,
    pub->readOnly || isLocked,
    pub->readOnly);
  GBA(&lineInfo->txtRow, &lineInfo->txtCol);

  /* place cursor at start of this line ? */
  if (pub->cursorLine == line
      && pub->cursorOffset == 0
      && pub->cursorPlacement == 2) {
    doIC();
  }

  /* compute end coordinates before writing the file line text out */
  int lastLineCol
    = PGMB_loc->lastCol
    - ((pub->ed->view->prefixMode > 1) ? pub->ed->view->prefixLen + 1 : 0);
  int endRow = lineInfo->txtRow + scrLinesPerEdLine - 1;
  if (pub->readOnly && !pub->wrapOverflow) {
    /* r/o truncated to a single line */
    appendStringWithLength(
      &line->text[priv->hShiftEffective],
      minInt(lastLineCol, lrecl - priv->hShiftEffective),
      (char)0x00);
  } else {
    /* r/w or r/o , possibly multiline */
    char eolFill = (!pub->readOnly && pub->lineEndBlankFill) ? ' ' : (char)0x00;
    /* write out whole line, 3270 wraps at line end */
    if (pub->cursorLine == line && pub->cursorPlacement == 2) {
      int linelen = lineLength(pub->ed, line);
      int lastpos = 0;
      if (linelen > 0) {
        appendStringWithLength(line->text, linelen, eolFill);
        lastpos = linelen;
      }
      if (pub->cursorOffset > linelen && pub->cursorOffset < lrecl) {
        appendStringWithLength("", pub->cursorOffset - linelen, ' ');
        lastpos = pub->cursorOffset;
      }
      if (lrecl > lastpos) {
        appendStringWithLength("", lrecl - lastpos, eolFill);
      }
      /*
      int linelen = lineLength(pub->ed, line);
      if (linelen > 0) {
        appendStringWithLength(line->text, linelen, (char)0x00);
      }
      if (pub->cursorOffset > linelen && pub->cursorOffset < lrecl) {
        appendStringWithLength("", pub->cursorOffset - linelen, ' ');
      }
      if (lrecl > pub->cursorOffset) {
        appendStringWithLength("", lrecl - pub->cursorOffset, (char)0x00);
      }
      */
    } else {
      appendStringWithLength(line->text, lrecl, eolFill);
    }

    /* compute end position of the nominal file line text */
    int fileLineEndCol
      = lrecl
      - ((scrLinesPerEdLine - 1) * PGMB_loc->cols)
      + ((pub->ed->view->prefixMode == 1) ? pub->ed->view->prefixLen + 1 : 0);
    /* possibly write ending field, if not closed by lineend-prefix */
    if (fileLineEndCol < lastLineCol) {
      int lengthBetween = lastLineCol - fileLineEndCol;
      SBA(endRow, fileLineEndCol);
      startField2(pub->ed->view->attrFileToPrefix, pub->ed->view->HiLitFileToPrefix, true, false);
      /* show filler char if requested */
      if (pub->ed->view->fileToPrefixFiller) {
        while (lengthBetween > 0) {
          appendChar(pub->ed->view->fileToPrefixFiller);
          lengthBetween--;
        }
      }
    }
  }

  /* place cursor somewhere inside the line? */
  if (pub->cursorLine == line && pub->cursorPlacement == 2) {
    SBA(
        lineInfo->txtRow,
        lineInfo->txtCol
          + maxInt(minInt(pub->cursorOffset, lrecl) - priv->hShiftEffective,
                   0));
    doIC();
  }

  /* prefix after file line text ? */
  if (pub->ed->view->prefixMode > 1) {
    attr  = (isCurrentLine) ?  pub->ed->view->attrCurLine :  pub->ed->view->attrPrefix;
    HiLit = (isCurrentLine) ? pub->ed->view->HiLitCurLine : pub->ed->view->HiLitPrefix;
    if (isExcluded) { attr = pub->ed->view->attrShadow; HiLit = pub->ed->view->HiLitShadow; }
    SBA(endRow, lastLineCol);
    startField2(attr, HiLit, pub->prefixReadOnly || isLocked, false);
    writePrefix(pub, priv, lineInfo, lineNo, pfixPrefill);
  }
}

static void writeTextAsFileMarker(
    ScreenPublPtr pub,
    ScreenPrivPtr priv,
    char *fileMarker,
    unsigned int lineNo,
    short scrLinesPerEdLine,
    bool isCurrentLine,
    bool allowPrefix) {
  t_PGMB *PGMB_loc = CMSGetPG();
  int lrecl = getWorkLrecl(pub->ed);
  EdLinePlace *lineInfo = &priv->edLinePlaces[priv->edLinesUsed++];
  int hadPrefix = false;

  lineInfo->edLine = NULL;
  lineInfo->edLineNo = lineNo;
  unsigned char  attrPrefix   = ((isCurrentLine) ?  pub->ed->view->attrCPrefix :  pub->ed->view->attrPrefix) ;
  unsigned char HiLitPrefix   = ((isCurrentLine) ? pub->ed->view->HiLitCPrefix : pub->ed->view->HiLitPrefix) ;
  unsigned char  attrFilearea = ((isCurrentLine) ?  pub->ed->view->attrCTofeof :  pub->ed->view->attrTofeof) ;
  unsigned char HiLitFilearea = ((isCurrentLine) ? pub->ed->view->HiLitCTofeof : pub->ed->view->HiLitTofeof) ;


  if (pub->ed->view->prefixMode == 1) {
    if (allowPrefix) {
      startField2(attrPrefix, HiLitPrefix, pub->prefixReadOnly, false);
      writePrefix(pub, priv, lineInfo, lineNo, NULL);
      hadPrefix = true;
    } else {
      appendStringWithLength(" ", pub->ed->view->prefixLen + 1, ' ');
    }
  }

  startField2(attrFilearea, HiLitFilearea, true, false);
  GBA(&lineInfo->txtRow, &lineInfo->txtCol);

  int lastLineCol
    = PGMB_loc->lastCol
    - ((pub->ed->view->prefixMode > 1) ? pub->ed->view->prefixLen + 1 : 0);
  int endRow = lineInfo->txtRow + scrLinesPerEdLine - 1;

  appendStringWithLength(
    fileMarker,
    PGMB_loc->lastCol - pub->ed->view->prefixLen - 1,
    (char)0x00);

  if (pub->ed->view->prefixMode > 1 && allowPrefix) {
    SBA(endRow, lastLineCol);
    startField2(attrPrefix, HiLitPrefix, pub->prefixReadOnly, false);
    writePrefix(pub, priv, lineInfo, lineNo, NULL);
    hadPrefix = true;
  }

  startField2(pub->ed->view->attrEMPTY, pub->ed->view->HiLitEMPTY, true, false);

  if (!hadPrefix) {
    priv->edLinesUsed--; /* no input-able field => forget field infos */
  }
}

bool ismfcons() {
  t_PGMB *PGMB_loc = CMSGetPG();
  return (PGMB_loc->sessionMode == 3270 || PGMB_loc->sessionMode == 3215);
}
