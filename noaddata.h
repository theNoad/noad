/***************************************************************************
                          noaddata.h  -  description
                             -------------------
    begin                : Sun Mar 10 2002
    copyright            : (C) 2002 by theNoad
    email                : theNoad@SoftHome.net
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef NOADDATA_H
#define NOADDATA_H
#include <stdio.h>
#include <linux/videodev.h>
#include "cchecklogo.h"
// defines for positions, etc.
#define TOP_LEFT  0
#define TOP_RIGHT 1
#define BOT_RIGHT 2
#define BOT_LEFT	3
#define UNKNOWN   4
#define ALL			5

#define STAT_LEARN	0
#define STAT_LOGO    1
#define STAT_NOLOGO	2
#define STAT_UNKNOWN 3
#define STAT_STOP    4

#define SHOW_LOGO
#define USE_RGB32
#define USE_LIBVO
#ifdef USE_RGB32
#define BYTES_PER_PIXEL 4
#else
#define BYTES_PER_PIXEL 3
#endif

#ifdef VNOAD
#define NUMPICS 20
#endif
struct testlines {
  int line;
  struct testlines* next;
  struct testpair* pair;
};
typedef struct testlines testlines;

struct testpair {
  int x_pos;
  int x_neg;
  struct testpair* next;
};
typedef struct testpair testpair;

/* struct and typedefs for remote control */
struct Hist
{
  long Value;
  unsigned long Count;
  struct Hist *Next;
};
typedef struct Hist Hist;

struct r_len
{
  struct r_len *previous;
  unsigned count;
  unsigned pos;
  struct r_len *next;
};
typedef struct r_len r_len;

struct endpoints
{
  unsigned start;
  unsigned end;
  unsigned next_start;
  struct endpoints *next;
};
typedef struct endpoints endpoints;
typedef int (* cbfunc)(void *rgb_buf, int width, int height, void *yufbuf);

struct pattern
{
  unsigned bit_length;
  unsigned int length;
  char r_flag;
  unsigned char *bytes;
  struct pattern *next;
};
typedef struct pattern pattern;

class noadData
{
  bool bYUVSource;
public:
  noadData();
  ~noadData();
public:
  /** init value of main filter */
  int m_nFilterInit;
  /** num of frames before data analyse */
  int m_nFilterFrames;
  /** minimum dif between 2 following pictures [%] */
  int m_nDif2Pictures;
  /** filter only up to this average */
  int m_nMaxAverage;
  /** data of running grey corner bot left */
  char* m_chGreyCorner3;
  /** data of running grey corner bot right */
  char* m_chGreyCorner2;
  /** data of running grey corner top right */
  char* m_chGreyCorner1;
  /** data of runnig grey corner top left */
  char* m_chGreyCorner0;
  /** data of running color corner bot left */
  char* m_chColorCorner3;
  /** data of running color corner bot right */
  char* m_chColorCorner2;
  /** data of running color corner top right */
  char* m_chColorCorner1;
  /** data of running color corner top left */
  char* m_chColorCorner0;
  /** running feedback filter coef. */
  float m_fRunFilter;
  /** main feedback filter coef. */
  float m_fMainFilter;
  /** time in msec between 2 ref pictures */
  int m_nTimeInterval;
  /** size of corner square X */
  int m_nSizeX;
  /** size of corner square Y */
  int m_nSizeY;
  /** size of border arround picture */
  int m_nBorderX;	// left and right
  int m_nBorderYTop; // top and bottom
  int m_nBorderYBot; // top and bottom
  /**  height of grabbing frame*/
  int m_nGrabHeight;
  /**  width of grabbing frame*/
  int m_nGrabWidth;
  /** char pointer to grabbed memory */
  char* video_buffer_mem;

  #ifdef VNOAD
  char* video_buffer_mem2[NUMPICS];
  char* video_buffer_mem3[NUMPICS];
  int iCompPics;
  #endif
  
  /** set test corner only if average is greater than
    * this value */
  int m_nMinAverage;
  /** number of frames the testlines have to live */
  int m_nCheckFrames;
  /** value of grabbing channel
    * standard: 0->tuner, 1->video1, 2->video2 */
  int m_nGrabberChannel;
  /** is true if the v4l interface has init successful */
  bool m_bGrabberReady;
  /** contains the frequency for tuner */
  int m_nGrabberFreq;
  /** pointer to the v4l picture struct */
  struct video_picture* pv_picture;

  // called to clean the testline list
  void deleteTestlines( testlines** tl );
  // called to init the buffer when video told the document the maximum size
  void initBuffer();

  // set source-format
  void setYUVSource() { bYUVSource = true; }
  void setRGBSource() { bYUVSource = false; }
  bool isYUVSource() { return bYUVSource == true; }
  // copy RGB-data into color corner buffer
  void setColorCorners();
  // set the grey corners from color corners
  // use values for color components
  void setGreyCorners( float fRed, float fGreen, float fBlue);
  // set the grey corners from YUV-Plane
  void setYUVGreyCorners();
  // sets the running corners
  void setCorners();
  /** detected logo corner */
  int m_nLogoCorner;
  /**  is set to true if logo has found */
  bool m_bFound;
  /** pointer to the logo check object */
  CCheckLogo* m_pCheckLogo;

  char *videoDir;

  int m_nTopLinesToIgnore;
  int m_nBottomLinesToIgnore;
  int m_nBlackLinesTop;
  int m_nBlackLinesBottom;
  int m_nBlackLinesTop2;
  int m_nBlackLinesBottom2;
  int m_nMonoFrameValue;
  int m_nBlackLinesLeft;
  int m_nBlackLinesRight;
  void detectBlackLines( unsigned char *buf = NULL );
  void checkMonoFrame( int framenum, unsigned char **buf );
  void saveCheckData( const char *name, bool bFullnameGiven = false );
  bool loadCheckData( const char *name, bool bFullnameGiven = false );
  void setGrabSize( int width, int height );
  char infoLine[256];

  #ifdef VNOAD
  void storePic(int n);
  void storeCompPic();
  void clearPics();
  void modifyPic(int framenum,unsigned char **dest);
  void testFunc(int framenum,unsigned char **_src);
  int checkPics();
  #endif
};

#endif
