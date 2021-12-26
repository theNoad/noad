#ifndef __NOAD_H__
#define __NOAD_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#include <signal.h>
#endif
#include <inttypes.h>


#include "vdr_cl.h"
#include "noaddata.h"
#include "ccontrol.h"

#define DEMUX_PAYLOAD_START 1

#define FRAMESPERSEC 25
#define FRAMESPERMIN 1500
#define BIGSTEP (15 * FRAMESPERSEC)
#define SMALLSTEP 1
#define LOGOSTABLETIME 40*FRAMESPERSEC

extern noadData *data;
extern CControl* cctrl;


bool detectBlacklines(int _index, int iFramesToCheck, cFileName *cfn, int& iTopLines, int& iBottomLines);
bool detectBlacklines(cMarks *marks, cFileName *cfn, int& iTopLines, int& iBottomLines);
void setCB_Func( cbfunc f );
cbfunc getCB_Func(void);
void decode_mpeg2 (uint8_t * current, uint8_t * end);
int demux (uint8_t * buf, uint8_t * end, int flags);
bool StdCallBack(void *buffer, int width, int height, void *yufbuf );
int drawCallback( void *buffer, int width, int height, void *yufbuf );
int BlacklineCallback( void *buffer, int width, int height, void *yufbuf );
int BlackframeCallback( void *buffer, int width, int height, void *yufbuf );
int checkCallback( void *buffer, int width, int height, void *yufbuf );
bool checkLogo(cFileName *cfn);
bool doLogoDetection(cFileName *cfn, int curIndex);
void reInitNoad(int top, int bottom );
bool detectLogo( cFileName *cfn, char* logoname );
int checkLogoState(cFileName *cfn, int iState, int iCurrentFrame, int FramesToSkip, int FramesToCheck);
int findLogoChange(cFileName *cfn, int iState, int& iCurrentFrame,
                   int FramesToSkip, int repeatCheckframes=0);
void MarkToggle(cMarks *marks, int index);
void moveMark( cMarks *marks, cMark *m, int iNewPos);
void listMarks(cMarks *marks);
#define MINBLACKLINES 15
#define MAXBLACKLINEDIFF 10
#define BACKFRAMES (FRAMESPERSEC*90)
void checkOnMarkBlacklines( cMarks *marks, cMark *m, cFileName *cfn);
bool checkOnMark( cMarks *marks, cMark *m, cFileName *cfn, bool bForward, int iCheckTime);
int displayIFrame(int iFrameIndex);
bool checkBlacklineOnMark( cMarks *marks, cMark *m, cFileName *cfn, bool bForward, int iCheckTime, int iTopLines, int iBottomLines);
bool checkBlackFrameOnMark( cMarks *marks, cMark *m, cFileName *cfn, bool bForward, int iCheckTime );
void checkOnMarks(cMarks *marks, cFileName *cfn);
bool checkBlackFramesOnMarks(cMarks *marks, cFileName *cfn);
bool detectBlacklines(cMarks *marks, cFileName *cfn, int& iTopLines, int& iBottomLines);
bool detectBlacklines(cMarks *marks, cFileName *cfn, int& iTopLines, int& iBottomLines);
bool detectBlacklines(int _index, int iFramesToCheck, cFileName *cfn, int& iTopLines, int& iBottomLines);
bool checkBlacklineOnMarks(cMarks *marks, cFileName *cfn);
void MarkCleanup(cMarks *marks, cFileName *cfn);
const char *getVersion();

int doX11Scan(noadData *thedata, char *fName, int iNumFrames );
const char *myTime(time_t tim);
#endif

