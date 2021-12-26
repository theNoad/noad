
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef VNOAD
#define ONMARKS_ONLY
//bool bOnMarksOnly = false;
bool bPlayAudio = false;
int ossfd = 0;

#define noCHECK_ONMARKS
#define DO_PASS2
#define AUTO_CLEANUP
#else
#define DO_PASS2
#endif

//#include "config.h"
#include "noad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#include <signal.h>
#endif
#include <inttypes.h>

extern "C"
{
//#include "config.h"
#include "mpeg2.h"
#ifdef VNOAD
//  #include "mpeg2_internal.h" // only for debug!!
#include "convert.h"
//#include "mm_accel.h"
#include "video_out.h"
#endif

static const char *VERSION = "0.3.0";

}

#ifdef VNOAD
#include <qobject.h>
#include <qwidget.h>
QWidget *widget = NULL;
#include "vdr_decoder.h"
#endif

#define BUFFER_SIZE 4096
//static uint8_t buffer[BUFFER_SIZE];
//static FILE * in_file;
int demux_track = 0;
int audio_demux_track = 0x80;
static int demux_pid = 0;
mpeg2dec_t *mpeg2dec;



#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include "vdr_cl.h"
#include "tools.h"
#include "videodir.h"
#include "noaddata.h"
#include "ccontrol.h"
#ifdef VNOAD
#include "ctoolbox.h"
#include <stest.h>
CTVScreen *tv = NULL;
fcbfunc fCallBack = NULL;
#endif

// The maximum time to wait before giving up while catching up on an index file:
#define MAXINDEXCATCHUP   2 // seconds

typedef unsigned char uchar;
#define INDEXFILESUFFIX     "/index.vdr"
#define RECORDFILESUFFIX    "/%03d.vdr"
#define RECORDFILESUFFIXLEN 20 // some additional bytes for safety...
#define RESUMEFILESUFFIX  "/resume.vdr"
#define MARKSFILESUFFIX   "/marks.vdr"

#define MAXFILESPERRECORDING 255

#define MINFREEDISKSPACE    (512) // MB
#define DISKCHECKINTERVAL   100 // seconds

// Picture types:
#define NO_PICTURE 0
#define I_FRAME    1
#define P_FRAME    2
#define B_FRAME    3

#define KILOBYTE(n) ((n) * 1024)
// The maximum size of a single frame:
#define MAXFRAMESIZE  KILOBYTE(192)


extern int SysLogLevel;
noadData *data = NULL;
CControl* cctrl = NULL;
char *filename = NULL;
bool bUseYUV = false;
bool bReUseLogo = true;

cbfunc current_cbf = NULL;
void setCB_Func( cbfunc f )
{
  current_cbf = f;
}
cbfunc getCB_Func(void)
{
  return current_cbf;
}

cNoadIndexFile *cIF = NULL;
cFileName *cfn = NULL;
int iCurrentDecodedFrame = 0;
bool bMarkChanged = false;
int bFrameDisplayed = 0;

//statistic data
int totalDecodedFrames = 0;
int decodedFramesForLogoDetection = 0;
int decodedFramesForLogoCheck = 0;
int secsForLogoDetection = 0;
int secsForLogoCheck = 0;
int secsForScan = 0;

// from mpeg2dec.cpp
void decode_mpeg2 (uint8_t * current, uint8_t * end)
{
  const mpeg2_info_t * info;
  int state;

  mpeg2_buffer (mpeg2dec, current, end);

  info = mpeg2_info (mpeg2dec);
  while (1)
  {
    state = mpeg2_parse (mpeg2dec);
/*
    if( state >= 0)
      dsyslog(LOG_INFO, "mpeg2state is %d(%s) pictype is %s",state, (state >= 0 && state <= 9) ? states[state] : states[0],(mpeg2dec->decoder.coding_type >= 0 && mpeg2dec->decoder.coding_type <= 4) ? pictype[mpeg2dec->decoder.coding_type] : pictype[0]);
*/
    switch (state)
    {
      case -1:
        return;
      case STATE_SEQUENCE:
      //if(info->current_picture)
      //dsyslog(LOG_INFO, "STATE_SEQUENCE temporal_reference is %d",info->current_picture->temporal_reference);
        //if(!bUseYUV )
        //  mpeg2_convert (mpeg2dec, convert_rgb32, NULL);
      break;
      case STATE_SLICE:
      case STATE_END:
      //if(info->display_picture)
      //dsyslog(LOG_INFO, "STATE_END temporal_reference is %d",info->display_picture->temporal_reference);
        if (info->display_fbuf)
        {
          bFrameDisplayed++;
          if (current_cbf)
          {
            current_cbf ((void *)info->display_fbuf->buf[0],info->sequence->width, info->sequence->height, (void *)info->display_fbuf->buf);
          }
        }
      break;
    }
  }
}


// from mpeg2dec.cpp
int demux (uint8_t * buf, uint8_t * end, int flags)
{
  static int mpeg1_skip_table[16] =
  {
    0, 0, 4, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };

  /*
   * the demuxer keeps some state between calls:
   * if "state" = DEMUX_HEADER, then "head_buf" contains the first
   *     "bytes" bytes from some header.
   * if "state" == DEMUX_DATA, then we need to copy "bytes" bytes
   *     of ES data before the next header.
   * if "state" == DEMUX_SKIP, then we need to skip "bytes" bytes
   *     of data before the next header.
   *
   * NEEDBYTES makes sure we have the requested number of bytes for a
   * header. If we dont, it copies what we have into head_buf and returns,
   * so that when we come back with more data we finish decoding this header.
   *
   * DONEBYTES updates "buf" to point after the header we just parsed.
   */

#define DEMUX_HEADER 0
#define DEMUX_DATA 1
#define DEMUX_SKIP 2

  static int state = DEMUX_SKIP;
  static int state_bytes = 0;
  static uint8_t head_buf[264];

  uint8_t * header;
  int bytes;
  int len;
  
#define NEEDBYTES(x)						\
  do {							\
    int missing;						\
    \
    missing = (x) - bytes;					\
    if (missing > 0) {					\
      if (header == head_buf) {				\
        if (missing <= end - buf) {			\
          memcpy (header + bytes, buf, missing);	\
          buf += missing;				\
          bytes = (x);				\
        } else {					\
          memcpy (header + bytes, buf, end - buf);	\
          state_bytes = bytes + end - buf;		\
          return 0;					\
        }						\
      } else {						\
        memcpy (head_buf, header, bytes);		\
        state = DEMUX_HEADER;				\
        state_bytes = bytes;				\
        return 0;					\
      }							\
    }							\
  } while (0)

#define DONEBYTES(x)		\
  do {			\
    if (header != head_buf)	\
      buf = header + (x);	\
  } while (0)

  if (flags & DEMUX_PAYLOAD_START)
  {
    state = DEMUX_SKIP;
    goto payload_start;
  }
  switch (state)
  {
    case DEMUX_HEADER:
      if (state_bytes > 0)
      {
        header = head_buf;
        bytes = state_bytes;
        goto continue_header;
      }
    break;

    case DEMUX_DATA:
      if (demux_pid || (state_bytes > end - buf))
      {
        decode_mpeg2 (buf, end);
        state_bytes -= end - buf;
        return 0;
      }
      decode_mpeg2 (buf, buf + state_bytes);
      buf += state_bytes;
    break;

    case DEMUX_SKIP:
    if (demux_pid || (state_bytes > end - buf))
    {
      state_bytes -= end - buf;
      return 0;
    }
    buf += state_bytes;
    break;
  }

  while (1)
  {
    if (demux_pid)
    {
        state = DEMUX_SKIP;
        return 0;
    }
    
    payload_start:
    header = buf;
    bytes = end - buf;
    
    continue_header:
    NEEDBYTES (4);
    if (header[0] || header[1] || (header[2] != 1))
    {
      if (demux_pid)
      {
      state = DEMUX_SKIP;
      return 0;
      }
      else if (header != head_buf)
      {
      buf++;
      goto payload_start;
      }
      else
      {
        header[0] = header[1];
        header[1] = header[2];
        header[2] = header[3];
        bytes = 3;
        goto continue_header;
      }
    }
    if (demux_pid)
    {
      if ((header[3] >= 0xe0) && (header[3] <= 0xef))
        goto pes;
      fprintf (stderr, "bad stream id %x\n", header[3]);
      exit (1);
    }
    switch (header[3])
    {
      case 0xb9:	/* program end code */
        /* DONEBYTES (4); */
        /* break;         */
        return 1;
        
      case 0xba:	/* pack header */
        NEEDBYTES (12);
      if ((header[4] & 0xc0) == 0x40)
      {	/* mpeg2 */
          NEEDBYTES (14);
          len = 14 + (header[13] & 7);
          NEEDBYTES (len);
          DONEBYTES (len);
          /* header points to the mpeg2 pack header */
      }
      else if ((header[4] & 0xf0) == 0x20)
      {	/* mpeg1 */
          DONEBYTES (12);
          /* header points to the mpeg1 pack header */
      }
      else
      {
          fprintf (stderr, "weird pack header\n");
          //exit (1);
          return -1;
        }
      break;

#ifdef VNOAD
/*
    case 0xbd:	// private stream 1 
      NEEDBYTES (7);
      if ((header[6] & 0xc0) == 0x80)
      { // mpeg2 
        NEEDBYTES (9);
        len = 10 + header[8];
        NEEDBYTES (len);
        // header points to the mpeg2 pes header 
      }
      else
      {	// mpeg1 
        len = 7;
        while ((header-1)[len] == 0xff)
        {
          len++;
          NEEDBYTES (len);
          if (len == 23)
          {
            fprintf (stderr, "too much stuffing\n");
            break;
          }
        }
        if (((header-1)[len] & 0xc0) == 0x40)
        {
          len += 2;
          NEEDBYTES (len);
        }
        len += mpeg1_skip_table[(header - 1)[len] >> 4] + 1;
        NEEDBYTES (len);
        // header points to the mpeg1 pes header 
      }
      if ((!demux_pid) && ((header-1)[len] != audio_demux_track))
      {
        DONEBYTES (len);
        bytes = 6 + (header[4] << 8) + header[5] - len;
        if (bytes <= 0)
          continue;
        goto skip;
      }
      len += 3;
      NEEDBYTES (len);
      DONEBYTES (len);
      bytes = 6 + (header[4] << 8) + header[5] - len;
      if (demux_pid || (bytes > end - buf))
      {
        a52_decode_data (buf, end);
        state = DEMUX_DATA;
        state_bytes = bytes - (end - buf);
        return 0;
      }
      else
        if (bytes <= 0)
          continue;
      a52_decode_data (buf, buf + bytes);
      buf += bytes;
    break;
*/

/*
    case AUDIO_STREAM_S:
//    case PRIVATE_STREAM1:
          NEEDBYTES (7);
          if ((header[6] & 0xc0) == 0x80)
          {	// mpeg2 
            NEEDBYTES (9);
            len = 9 + header[8];
            NEEDBYTES (len);
            // header points to the mpeg2 pes header 
            if (header[7] & 0x80)
            {
              uint32_t pts;

              pts = (((header[9] >> 1) << 30) |
              (header[10] << 22) | ((header[11] >> 1) << 15) |
              (header[12] << 7) | (header[13] >> 1));
              mpeg2_pts (mpeg2dec, pts);
              fprintf(stderr, "%s pes PTS = %u\n",header[3] == AUDIO_STREAM_S ? "audio" : "ac3" ,pts/900);
            }
          }
          if( bPlayAudio )
          {
            //if( ossfd <= 0)
            //  ossfd = oss_open();
            mad_decode(ossfd, buf, end-buf);
          }
          // fall-through
*/
  #endif          
    default:
      if (header[3] == demux_track)
      {
      pes:
        NEEDBYTES (7);
        if ((header[6] & 0xc0) == 0x80)
        {	/* mpeg2 */
          NEEDBYTES (9);
          len = 9 + header[8];
          NEEDBYTES (len);
          /* header points to the mpeg2 pes header */
          if (header[7] & 0x80)
          {
            uint32_t pts;

            pts = (((header[9] >> 1) << 30) |
                   (header[10] << 22) | ((header[11] >> 1) << 15) |
                   (header[12] << 7) | (header[13] >> 1));
            mpeg2_pts (mpeg2dec, pts);
            //fprintf(stderr, "mpeg pes PTS = %u\n",pts/900);
          }
        }
        else
        {	/* mpeg1 */
          int len_skip;
          uint8_t * ptsbuf;

          len = 7;
          while (header[len - 1] == 0xff)
          {
            len++;
            NEEDBYTES (len);
            if (len == 23)
            {
              fprintf (stderr, "too much stuffing\n");
              break;
            }
          }
          if ((header[len - 1] & 0xc0) == 0x40)
          {
            len += 2;
            NEEDBYTES (len);
          }
          len_skip = len;
          len += mpeg1_skip_table[header[len - 1] >> 4];
          NEEDBYTES (len);
          /* header points to the mpeg1 pes header */
          ptsbuf = header + len_skip;
          if (ptsbuf[-1] & 0x20)
          {
            uint32_t pts;

            pts = (((ptsbuf[-1] >> 1) << 30) |
                   (ptsbuf[0] << 22) | ((ptsbuf[1] >> 1) << 15) |
                   (ptsbuf[2] << 7) | (ptsbuf[3] >> 1));
            mpeg2_pts (mpeg2dec, pts);
          }
        }
        DONEBYTES (len);
        bytes = 6 + (header[4] << 8) + header[5] - len;
        if (demux_pid || (bytes > end - buf))
        {
          decode_mpeg2 (buf, end);
          state = DEMUX_DATA;
          state_bytes = bytes - (end - buf);
          return 0;
        }
        else if (bytes > 0)
        {
          decode_mpeg2 (buf, buf + bytes);
          buf += bytes;
        }
      }
      else if (header[3] < 0xb9)
      {
        fprintf (stderr,
                 "looks like a video stream, not system stream\n");
        //exit (1);
        return -1;
      }
      else
      {
        NEEDBYTES (6);
        DONEBYTES (6);
        bytes = (header[4] << 8) + header[5];
        //skip:
        if (bytes > end - buf)
        {
          state = DEMUX_SKIP;
          state_bytes = bytes - (end - buf);
          return 0;
        }
        buf += bytes;
      }
    }
  }
}

int iLastIFrame = 0;
int iIgnoreFrames = 0;
unsigned long ulTopBlackLines;
unsigned long ulBotBlackLines;
int checkedFrames;

#define RGB24_INCOMING
// Standard-Callback-Funktion
// parameter:
// buffer: zeiger auf rgb24/rgb32 image (wenn RGB-image verwendet wird)
//          oder y-buffer (wenn yuv verwendet wird)
// width:  aktuelle bildbreite
// width:  aktuelle bildhöhe
// yufbuf: array von pointern auf die y/u/v-daten
bool StdCallBack(void *buffer, int width, int height, void */*yufbuf*/ )
{
  totalDecodedFrames++;
  // soll der Frame ignoriert werden
  if( iIgnoreFrames )
  {
    //dsyslog(LOG_INFO, "drawCallback frame ignored");
    iIgnoreFrames--;
    return false;
  }
  if( data && buffer )
  {
    // bei Änderung der Bildgrösse daten zurücksetzen
    if( width != data->m_nGrabWidth || height != data->m_nGrabHeight )
    {
      delete cctrl;
      cctrl = NULL;
      data->setGrabSize( width, height );
      delete [] data->video_buffer_mem;
      data->video_buffer_mem = NULL;
    }
    // buffer-größe ermitteln (ausreichend für RGB32-Bilder)
    int size = width * height * 4;
    // buffer anlegen
    if( data->video_buffer_mem == NULL )
      data->video_buffer_mem = new char[size];

    char *src, *dst;
    src = (char *)buffer;
    dst = data->video_buffer_mem;
#ifndef RGB24_INCOMING
    size = width * height;
    for (int i = 0; i < size; i++)
    {
    	//RGB 24
    	*dst++ = src[0];	// rot
    	*dst++ = src[1]; // grün
    	*dst++ = src[2]; // blau
    	src += 3;
/*
		// RGB 32
    	*dst++ = src[2];	// rot
    	*dst++ = src[1]; // grün
    	*dst++ = src[0]; // blau
    	src += 4;
*/
/*
    	*dst++ = *src++;
    	*dst++ = *src++;
    	*dst++ = *src++;
    	src++;
*/
    }
#else
    if( data->isYUVSource() )
      size = width * height;
    else
    {
      #ifdef USE_RGB32
      size = width * height * 4;
      #else
      size = width * height * 3;
      #endif
    }
    memcpy(data->video_buffer_mem, buffer, size );
#endif
    return true;
  }
  return false;
}

int drawCallback( void *buffer, int width, int height, void *yufbuf )
{
  if( StdCallBack( buffer, width, height, yufbuf) )
  {
    if(cctrl == NULL )
    {
      #ifdef VNOAD
      cctrl = new CControl(data, widget);
      cctrl->show();
      if( tv )
        tv->reset();
      #else
      cctrl = new CControl(data);
      #endif
    }
    data->detectBlackLines((unsigned char *)yufbuf);
    ulTopBlackLines += data->m_nBlackLinesTop;
    ulBotBlackLines += data->m_nBlackLinesBottom;
    checkedFrames++;
    data->setCorners();
    if( data->m_bFound == false )
    {
      cctrl->newData();
      #ifdef VNOAD
      if( tv )
        tv->drawImage();
      #endif
    }
    else
    {
      bool bOldState = data->m_pCheckLogo->isLogo;
      data->m_pCheckLogo->newData();
      if( bOldState != data->m_pCheckLogo->isLogo )
      {
        dsyslog(LOG_INFO, "LogoDetection: logo %s at %d", data->m_pCheckLogo->isLogo ? "appears" : "disappears", iLastIFrame);
      }
    }
    #ifdef VNOAD
    if( fCallBack )
      fCallBack(iCurrentDecodedFrame,buffer,width,height, yufbuf);
    #endif
  }
  return 0;
}

int BlacklineCallback( void *buffer, int width, int height, void *yufbuf )
{
  if( StdCallBack( buffer, width, height, yufbuf) )
  {
    #ifdef VNOAD
    if( tv )
      tv->drawImage();
    #endif
    data->detectBlackLines((unsigned char *)yufbuf);
    ulTopBlackLines += data->m_nBlackLinesTop;
    ulBotBlackLines += data->m_nBlackLinesBottom;
    checkedFrames++;
    #ifdef VNOAD
    if( fCallBack )
      fCallBack(iCurrentDecodedFrame,buffer,width,height, yufbuf);
    #endif
  }
  return 0;
}

int BlackframeCallback( void *buffer, int width, int height, void *yufbuf )
{
  if( StdCallBack( buffer, width, height, yufbuf) )
  {
    #ifdef VNOAD
    if( tv )
      tv->drawImage();
    #endif
    data->checkMonoFrame(iCurrentDecodedFrame,(unsigned char **)yufbuf);
    ulTopBlackLines += data->m_nBlackLinesTop;
    ulBotBlackLines += data->m_nBlackLinesBottom;
    checkedFrames++;
    #ifdef VNOAD
    if( fCallBack )
      fCallBack(iCurrentDecodedFrame,buffer,width,height, yufbuf);
    #endif
  }
  return 0;
}

int checkCallback( void *buffer, int width, int height, void *yufbuf )
{
  if( StdCallBack( buffer, width, height, yufbuf) )
  {
    checkedFrames++;
    data->setCorners();
    data->m_pCheckLogo->newData();
    #ifdef VNOAD
    if( tv )
      tv->drawImage();
    if( fCallBack )
      fCallBack(iCurrentDecodedFrame,buffer,width,height,yufbuf);
    #endif
  }
  return 0;
}


uchar readBuffer[MAXFRAMESIZE];	// frame-buffer
int curIndex = 1;						// current index
uchar FileNumber;						// current file-number
int FileOffset;						// current file-offset
uchar PictureType;					// current picture-type
int Length;								// frame-lenght of current frame
uint8_t * end;							// pointer to frame-end
#define FRAMES_TO_CHECK 3000

// check for logo in some parts of the recording
// check 5*100 frames, logo should be bvisible in at least 250 frames
bool checkLogo(cFileName *cfn)
{
  dsyslog(LOG_INFO, "checklogo for %d frames", 250);
  int iLogosFound = 0;
  cbfunc cbf_old = getCB_Func();
  setCB_Func(checkCallback);
  //  data->m_pCheckLogo->reset();

  int index = 5*FRAMESPERMIN;

  index = cIF->GetNextIFrame( index, true, &FileNumber, &FileOffset, &Length, true);
  for( int i = 0; i < 10 && iLogosFound < 250; i++ )
  {
    for( int ii = 1; ii <= 50 && iLogosFound < 250; ii++ )
    {
      cfn->SetOffset( FileNumber, FileOffset);
      //end = readBuffer + read (cfn->File(), readBuffer, Length > MAXFRAMESIZE ? MAXFRAMESIZE : Length);
      end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
      //!!!if( PictureType != B_FRAME )
      demux (readBuffer, end, 0);
      iLogosFound += data->m_pCheckLogo->isLogo ? 1 : 0;
      cIF->Get( index+ii, &FileNumber, &FileOffset, &PictureType, &Length);
      iCurrentDecodedFrame = index+ii;
      //dsyslog(LOG_INFO, "checklogo %d %d", i * 50 + ii, iLogosFound);
      //sleep(1);
    }
    index += 5*FRAMESPERMIN;
    index = cIF->GetNextIFrame( index, true, &FileNumber, &FileOffset, &Length, true);
  }
  setCB_Func(cbf_old);
  dsyslog(LOG_INFO, "checklogo for %d frames gives %d", 250, iLogosFound >= 250);
  return iLogosFound >= 250;
}

// try to detect a Logo within the next #FRAMES_TO_CHECK frames
bool doLogoDetection(cFileName *cfn, int curIndex)
{
  cbfunc cbf_old = getCB_Func();
  setCB_Func(drawCallback);
  // go to the next I-Frame near curIndex
  curIndex = cIF->GetNextIFrame( curIndex, true, &FileNumber, &FileOffset, &Length, true);
  while( curIndex >= 0 && !data->m_bFound && checkedFrames < FRAMES_TO_CHECK )
  {
    iLastIFrame = curIndex;
    iCurrentDecodedFrame = curIndex;
    cfn->SetOffset( FileNumber, FileOffset);
    //dsyslog(LOG_INFO, "index %d (%d), offset %d", curIndex, PictureType, FileOffset);
    //end = readBuffer + read (cfn->File(), readBuffer, Length > MAXFRAMESIZE ? MAXFRAMESIZE : Length);
    end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);

    // call demuxer if no B-Frame
    //!!!if( PictureType != B_FRAME )
    demux (readBuffer, end, 0);
    curIndex++;
    if( !cIF->Get( curIndex, &FileNumber, &FileOffset, &PictureType, &Length) )
    {
      setCB_Func(cbf_old);
      return false;
    }
  }
  setCB_Func(cbf_old);
  return data->m_bFound;
}

void reInitNoad(int top, int bottom )
{
  dsyslog(LOG_INFO, "reInitNoad %d %d", top, bottom);
  ulTopBlackLines = 0;
  ulBotBlackLines = 0;
  checkedFrames = 0;
  delete cctrl;
  cctrl = NULL;
  data->m_nTopLinesToIgnore = top;
  data->m_nBottomLinesToIgnore = bottom;
  data->initBuffer();
  //  data->m_pCheckLogo->reset();
  data->m_bFound = false;
  data->m_nLogoCorner = UNKNOWN;
  #ifdef VNOAD
  cctrl = new CControl(data, widget);
  cctrl->show();
  if( tv )
    tv->reset();
  #else
  cctrl = new CControl(data);
  #endif
}

// try to detect the Logo of the Record and verfiy it
// in some parts of the video
bool detectLogo( cFileName *cfn, char* logoname )
{
  time_t start;
  time_t end;
  reInitNoad( 0, 0 );
  if( bReUseLogo )
  {
    if( data->loadCheckData(logoname, true) )
    {
      start = time(NULL);
      if( checkLogo( cfn ) )
      {
        decodedFramesForLogoCheck = totalDecodedFrames;
        end = time(NULL);
        secsForLogoCheck = end - start;
        return true;
      }
    }
  }

  start = time(NULL);

  int iAStartpos[] =
  {
    0, 8*FRAMESPERMIN, 16*FRAMESPERMIN, 32*FRAMESPERMIN,
       5*FRAMESPERMIN, 10*FRAMESPERMIN, 15*FRAMESPERMIN,
      20*FRAMESPERMIN, 25*FRAMESPERMIN, 30*FRAMESPERMIN,
    -1
  };

  ulTopBlackLines = 0;
  ulBotBlackLines = 0;
  checkedFrames = 0;

  int iPart = 0;
  do
  {
    // detect from start
    dsyslog(LOG_INFO, "detectLogo part%d",iPart+1);
    //detectBlacklines(iAStartpos[iPart], 250, cfn, iTopLines, iBottomLines);
    //reInitNoad( iTopLines, iBottomLines );
    reInitNoad( 0,0 );
    if( doLogoDetection(cfn, iAStartpos[iPart] ) )
      if( checkLogo( cfn ) )
      {
        end = time(NULL);
        secsForLogoDetection = end - start;
        return true;
      }
    iPart++;
  } while( iAStartpos[iPart] >= 0);

  end = time(NULL);
  secsForLogoDetection = end - start;
  dsyslog(LOG_INFO, "detectLogo: no Logo found, give up");
  return false;
}

#define FRAMESTOCHECK 10
int checkLogoState(cFileName *cfn, int iState, int iCurrentFrame, int /*FramesToSkip*/, int FramesToCheck)
{
  int iLastIFrame = 0;

  dsyslog(LOG_INFO, "checkLogoState for %d Frames starting at Frame %d",
          FramesToCheck,iCurrentFrame);
  cbfunc cbf_old = getCB_Func();
  setCB_Func(checkCallback);

  iCurrentFrame = cIF->GetNextIFrame( iCurrentFrame, true, &FileNumber, &FileOffset, &Length, true);
  PictureType = I_FRAME;
  while( iCurrentFrame < cIF->Last() && iCurrentFrame >= 0 && FramesToCheck > 0 )
  {
    cIF->Get( iCurrentFrame, &FileNumber, &FileOffset, &PictureType, &Length);
    cfn->SetOffset( FileNumber, FileOffset);
   	end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
    iCurrentDecodedFrame = iCurrentFrame;
    checkedFrames = 0;
    iLastIFrame = iCurrentFrame;
    //iIgnoreFrames = 2;
    demux (readBuffer, end, 0);
    if( PictureType == I_FRAME )
    {
      iLastIFrame = iCurrentFrame;
    }
    if( iState != data->m_pCheckLogo->isLogo )
    {
      dsyslog(LOG_INFO, "checkLogoState Logo lost, iLastIFrame is %d ", iLastIFrame);
      return( iLastIFrame );
    }
    iCurrentFrame ++;
    FramesToCheck --;
  }
  setCB_Func(cbf_old);
  dsyslog(LOG_INFO, "checkLogoState ends, iLastIFrame %d ", iLastIFrame);
  return( 0 );
}
/*
int checkLogoState(cFileName *cfn, int iState, int iCurrentFrame, int FramesToSkip, int FramesToCheck)
{
  int iLastIFrame = 0;
//  dsyslog(LOG_INFO, "checkLogoState: iState %d, iCurrentFrame %d, iTotalFrames %d, FramesToSkip %d, FramesToCheck %d",
//          iState, iCurrentFrame, cIF->Last(), FramesToSkip, FramesToCheck);

  dsyslog(LOG_INFO, "checkLogoState for %d Frames starting at Frame %d",
          FramesToCheck,iCurrentFrame);
  cbfunc cbf_old = getCB_Func();
  setCB_Func(checkCallback);

  iCurrentFrame = cIF->GetNextIFrame( iCurrentFrame, true, &FileNumber, &FileOffset, &Length, true);
  PictureType = I_FRAME;
  while( iCurrentFrame < cIF->Last() && iCurrentFrame >= 0 && FramesToCheck > 0 )
  {
    checkedFrames = 0;
    iLastIFrame = iCurrentFrame;
    iIgnoreFrames = 2;

    while( checkedFrames < 3 && iCurrentFrame > 0 )
    {
        cfn->SetOffset( FileNumber, FileOffset);
      	end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
        iCurrentDecodedFrame = iCurrentFrame;
        demux (readBuffer, end, 0);
        if( PictureType == I_FRAME )
        {
          iLastIFrame = iCurrentFrame;
        }
        if( !cIF->Get( ++iCurrentFrame, &FileNumber, &FileOffset, &PictureType, &Length) )
          iCurrentFrame = 0;
    }

    if( iState != data->m_pCheckLogo->isLogo )
    {
      dsyslog(LOG_INFO, "checkLogoState Logo lost, iLastIFrame is %d ", iLastIFrame);
      return( iLastIFrame );
    }
    if( iCurrentFrame+FramesToSkip >= cIF->Last() )
    {
      setCB_Func(cbf_old);
      return 0;
    }
    iCurrentFrame += FramesToSkip;
    FramesToCheck -= FramesToSkip;
    cIF->Get( ++iCurrentFrame, &FileNumber, &FileOffset, &PictureType, &Length);
  }
  setCB_Func(cbf_old);
  dsyslog(LOG_INFO, "checkLogoState ends, iLastIFrame %d ", iLastIFrame);
  return( 0 );
}
*/
int findLogoChange(cFileName *cfn, int iState, int& iCurrentFrame,
                   int FramesToSkip, int repeatCheckframes)
{
  dsyslog(LOG_INFO, "findLogoChange: iState %d, iCurrentFrame %d, iTotalFrames %d, FramesToSkip %d, repeatCheckframes=%d",
          iState, iCurrentFrame, cIF->Last(), FramesToSkip, repeatCheckframes);

  cbfunc cbf_old = getCB_Func();
  setCB_Func(checkCallback);

  iCurrentFrame = cIF->GetNextIFrame( iCurrentFrame, true, &FileNumber, &FileOffset, &Length, true);
  PictureType = I_FRAME;
  #ifdef VNOAD
  bool bFirst = true;
  #endif
  while( iCurrentFrame < cIF->Last() && iCurrentFrame >= 0  )
  {
    checkedFrames = 0;
    iLastIFrame = iCurrentFrame;
    iIgnoreFrames = 5;

//    #ifdef VNOAD
//    while( (bFirst && checkedFrames < FRAMESTOCHECK) || checkedFrames < 1)
//    #else
    while( checkedFrames < FRAMESTOCHECK )
//    #endif
    {
      cfn->SetOffset( FileNumber, FileOffset);
      end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
      iCurrentDecodedFrame = iCurrentFrame;
      demux (readBuffer, end, 0);
      //dsyslog(LOG_INFO, "findLogoChange: iCurrentFrame %d, iState %d",iCurrentFrame,  data->m_pCheckLogo->isLogo);
      if( PictureType == I_FRAME )
      {
        iLastIFrame = iCurrentFrame;
      }
      cIF->Get( ++iCurrentFrame, &FileNumber, &FileOffset, &PictureType, &Length);
    }
    #ifdef VNOAD
    bFirst = false;
    #endif
    //dsyslog(LOG_INFO, "findLogoChange: iCurrentFrame %d, iState %d",iCurrentFrame,  data->m_pCheckLogo->isLogo);
    if( iState != data->m_pCheckLogo->isLogo )
    {
      dsyslog(LOG_INFO, "StateChanged: iLastIFrame %d ", iLastIFrame);
      if( repeatCheckframes > 0 )
      {
        // new found state must be stable over repeatCheckframes frames
        int i = checkLogoState(cfn, data->m_pCheckLogo->isLogo, iCurrentFrame, FRAMESPERSEC*1, repeatCheckframes);
        dsyslog(LOG_INFO, "checkLogoState: i = %d",i);
        if( i == 0 )
          return( data->m_pCheckLogo->isLogo );
        else
          iCurrentFrame = i-FramesToSkip;
      }
      else
        return( data->m_pCheckLogo->isLogo );
    }
    if( iCurrentFrame+FramesToSkip >= cIF->Last() )
    {
      setCB_Func(cbf_old);
      return iState;
    }
//    #ifdef VNOAD
    if( FramesToSkip > 1 )
    {
//    #endif
    iCurrentFrame += FramesToSkip;
    iCurrentFrame = cIF->GetNextIFrame( iCurrentFrame, true, &FileNumber, &FileOffset, &Length, true);
    PictureType = I_FRAME;
//    #ifdef VNOAD
    }
//    #endif
    
  }
  setCB_Func(cbf_old);
  return( -1 );
}

void MarkToggle(cMarks *marks, int index)
{
  dsyslog(LOG_INFO, "ToggleMark: %d ", index);
  cMark *m = marks->Get(index);
  if (m)
  {
    dsyslog(LOG_INFO, "del Mark: %d ", m->position);
    marks->Del(m);
  }
  else
  {
    dsyslog(LOG_INFO, "add Mark: %d ", index);
    marks->Add(index);
  }
  marks->Save();
  bMarkChanged = true;
}

void moveMark( cMarks *marks, cMark *m, int iNewPos)
{
  char *cpOldPos, *cpNewPos;
  asprintf(&cpOldPos, "%s",m->ToText(false));
  asprintf(&cpNewPos, "%s",IndexToHMSF(iNewPos, false));
  dsyslog(LOG_INFO, "moveMark from %d to %d (%s-->%s)",m->position, iNewPos, cpOldPos, cpNewPos );
  marks->Del(m);
  marks->Add( iNewPos);
  bMarkChanged = true;
}

void checkOnMarkBlacklines( cMarks *marks, cMark *m, cFileName *cfn)
{
  dsyslog(LOG_INFO, "checkOnMark at %d",m->position );
  if( m->position < BACKFRAMES )
    return;
  int index = cIF->GetNextIFrame( m->position, true, &FileNumber, &FileOffset, &Length, true);
  cfn->SetOffset( FileNumber, FileOffset);
  //end = readBuffer + read (cfn->File(), readBuffer, Length > MAXFRAMESIZE ? MAXFRAMESIZE : Length);
  end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
  demux (readBuffer, end, 0);
  ulTopBlackLines = 0;
  ulBotBlackLines = 0;
  checkedFrames = 0;
  for( int i = 0; i < 10; i++ )
  {
    cIF->Get(index+i,&FileNumber, &FileOffset, NULL, &Length);
    cfn->SetOffset( FileNumber, FileOffset);
    //end = readBuffer + read (cfn->File(), readBuffer, Length > MAXFRAMESIZE ? MAXFRAMESIZE : Length);
    end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
    //if( PictureType != B_FRAME )
    demux (readBuffer, end, 0);
  }

  ulTopBlackLines /= checkedFrames;
  ulBotBlackLines /= checkedFrames;
  dsyslog(LOG_INFO, "checkOnMark TopBlackLines = %ld, BotBlackLines = %ld",ulTopBlackLines, ulBotBlackLines );
  if( ulTopBlackLines < MINBLACKLINES || ulBotBlackLines < MINBLACKLINES )
    return;

  // we have blacklines, check backwards to the first frame with diff blacklines
  int iTopBlackLines = ulTopBlackLines;//data->m_nBlackLinesTop;
  int iBotBlackLines = ulBotBlackLines;//data->m_nBlackLinesBottom;
  dsyslog(LOG_INFO, "checkOnMark ulTopBlackLines = %d, ulBotBlackLines = %d",iTopBlackLines, iBotBlackLines );
  for( int i = 0; i < BACKFRAMES; i++ )
  {
    cIF->Get(index-i,&FileNumber, &FileOffset, NULL, &Length);
    cfn->SetOffset( FileNumber, FileOffset);
    //end = readBuffer + read (cfn->File(), readBuffer, Length > MAXFRAMESIZE ? MAXFRAMESIZE : Length);
    end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
    //!!!if( PictureType != B_FRAME )
    demux (readBuffer, end, 0);
//    dsyslog(LOG_INFO, "checkOnMark(%d) TopBlackLines = %d, BotBlackLines = %d",index-i,data->m_nBlackLinesTop, data->m_nBlackLinesBottom );
    if( data->m_nBlackLinesTop > iTopBlackLines+MAXBLACKLINEDIFF ||
        data->m_nBlackLinesTop < iTopBlackLines-MAXBLACKLINEDIFF ||
        data->m_nBlackLinesBottom > iBotBlackLines+MAXBLACKLINEDIFF ||
        data->m_nBlackLinesBottom < iBotBlackLines-MAXBLACKLINEDIFF )
    {
      int newindex = cIF->GetNextIFrame( index-i+1, true, &FileNumber, &FileOffset, &Length, true);
      if( newindex > 0 )
      {
        dsyslog(LOG_INFO, "do moveMark from %d to %d ",index, newindex );
        moveMark( marks, m, newindex);
      }
      return;
    }
  }
}


#define BACK_TIME 90
bool checkOnMark( cMarks *marks, cMark *m, cFileName *cfn, bool bForward, int iCheckTime)
{
  dsyslog(LOG_INFO, "checkOnMark at %d",m->position );
  int iDiff;
  int iOffset = 0;
  if( bForward )
  {
    if( m->position+iCheckTime*FRAMESPERSEC > cIF->Last() )
      return false;
    iDiff = FRAMESPERSEC;
    iOffset = iCheckTime*FRAMESPERSEC;
  }
  else
  {
    if( m->position < iCheckTime*FRAMESPERSEC )
      return false;
    iDiff = -FRAMESPERSEC;
  }
  iDiff = -FRAMESPERSEC;
  int index = cIF->GetNextIFrame( m->position+iOffset, true, &FileNumber, &FileOffset, &Length, true);
  cfn->SetOffset( FileNumber, FileOffset);
  //end = readBuffer + read (cfn->File(), readBuffer, Length > MAXFRAMESIZE ? MAXFRAMESIZE : Length);
  end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
  demux (readBuffer, end, 0);
  ulTopBlackLines = 0;
  ulBotBlackLines = 0;
  checkedFrames = 0;

  for( int i = 0; i < iCheckTime; i++ )
  {
    for( int ii = 0; ii < FRAMESPERSEC; ii++)
    {
      cIF->Get(index+ii,&FileNumber, &FileOffset, &PictureType, &Length);
      cfn->SetOffset( FileNumber, FileOffset);
      //end = readBuffer + read (cfn->File(), readBuffer, Length > MAXFRAMESIZE ? MAXFRAMESIZE : Length);
      end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
      //if( PictureType != B_FRAME )
      demux (readBuffer, end, 0);
      //dsyslog(LOG_INFO, "checkOnMark(%d) TopBlackLines = %d, BotBlackLines = %d",index-i,data->m_nBlackLinesTop, data->m_nBlackLinesBottom );
      if( data->m_nBlackLinesTop > data->m_nGrabHeight/2 ||
          data->m_nBlackLinesBottom > data->m_nGrabHeight/2 )
      {
        int newIndex = cIF->GetNextIFrame( index, true, &FileNumber, &FileOffset, &Length, true);
        if( newIndex > 0 )
        {
          dsyslog(LOG_INFO, "do moveMark from %d to %d ",index, newIndex );
          moveMark( marks, m, newIndex);
        }
        return true;
      }
    }
    index = cIF->GetNextIFrame( index+iDiff, true, &FileNumber, &FileOffset, &Length, true);
  }
  return false;
}

int displayIFrame(int iFrameIndex)
{
  /*int index = */cIF->GetNextIFrame( iFrameIndex, true, &FileNumber, &FileOffset, &Length, true);

  mpeg2_close (mpeg2dec);
  mpeg2dec= mpeg2_init ();

  bFrameDisplayed = 0;
  int i = 0;
  while( bFrameDisplayed<2 && i++ < 100 )
  {
    cfn->SetOffset( FileNumber, FileOffset);
    //end = readBuffer + read (cfn->File(), readBuffer, Length > MAXFRAMESIZE ? MAXFRAMESIZE : Length);
    end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
    demux (readBuffer, end, DEMUX_PAYLOAD_START);
    if( !bFrameDisplayed)
    {
      iFrameIndex++;
      //iCurrentDecodedFrame++;
      cIF->Get( iFrameIndex, &FileNumber, &FileOffset, &PictureType, &Length);
    }
  }
  return iFrameIndex;
}

bool checkBlacklineOnMark( cMarks *marks, cMark *m, cFileName *cfn, bool bForward, int iCheckTime, int iTopLines, int iBottomLines)
{
    #define POS_OK (data->m_nBlackLinesTop >= iTopMin \
     && data->m_nBlackLinesTop <= iTopMax \
     && data->m_nBlackLinesBottom >= ibotmin \
     && data->m_nBlackLinesBottom <= ibotmax)
    #define POS_NOT_OK (data->m_nBlackLinesTop < iTopMin \
     || data->m_nBlackLinesBottom < ibotmin) 
    #define POS_MAYBE_OK (data->m_nBlackLinesTop > iTopMin \
     && data->m_nBlackLinesBottom > ibotmin)

   #define PRE_POS_OK (data->m_nBlackLinesTop >= iTopLines && data->m_nBlackLinesBottom >= iBottomLines)
   #define PRE_POS_RESET (data->m_nBlackLinesTop + data->m_nBlackLinesBottom < (iTopLines+iBottomLines)-(((iTopLines+iBottomLines)*20)/100))

  dsyslog(LOG_INFO, "checkBlacklineOnMark at %d %s %d %d for %d frames",m->position, bForward ? "forward":"backward",iTopLines, iBottomLines, iCheckTime );
  int iDiff;
  int iOffset = 0;
  int iBestPos = 0;
  int iBestPosResetCount = 0;
  int iPrePos = 0;
  int iPrePosResetCount = 0;
  int iEnd = m->position;
//  int topmaxdiff = iTopLines * 20 / 100;
//  int botmaxdiff = iBottomLines * 20 / 100;
  int iTopMax = iTopLines + iTopLines*30/100;
  int iTopMin = iTopLines - iTopLines*20/100;
  int ibotmax = iBottomLines + iBottomLines*30/100;
  int ibotmin = iBottomLines - iBottomLines*30/100;
  dsyslog(LOG_INFO, "check-range top %d %d, bott %d %d",iTopMin,iTopMax,ibotmin,ibotmax );
  iOffset = 0;//iCheckTime*FRAMESPERSEC;
  if( bForward )
  {
    if( m->position+iCheckTime > cIF->Last() )
      return false;
    iDiff = FRAMESPERSEC;
    iEnd = m->position+iCheckTime;
    iBestPos = iPrePos = m->position;
  }
  else
  {
    if( m->position < iCheckTime )
      return false;
    iOffset = iCheckTime;
    iOffset *= -1;
    iDiff = FRAMESPERSEC;
  }
  int index = displayIFrame(m->position+iOffset-1);
  ulTopBlackLines = 0;
  ulBotBlackLines = 0;
  checkedFrames = 0;

  while( index < iEnd )
  {
    // get the next frame
    cIF->Get( ++index, &FileNumber, &FileOffset, &PictureType, &Length);
    cfn->SetOffset( FileNumber, FileOffset);
    end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
    iCurrentDecodedFrame = index;
    ulTopBlackLines = 0;
    ulBotBlackLines = 0;
    demux (readBuffer, end, 0);

//    dsyslog(LOG_INFO, "checkOnMark(%d) TopBlackLines = %3d, BotBlackLines = %3d %d",index,data->m_nBlackLinesTop, data->m_nBlackLinesBottom,checkedFrames );
    if( iPrePos == 0 && PRE_POS_OK )
    {
      iPrePos = index;
      iPrePosResetCount = 0;
      dsyslog(LOG_INFO, "checkOnMark set prepos %d",iPrePos );
    }
    if( iPrePos != 0 && PRE_POS_RESET )
    {
      iPrePosResetCount++;
      if(iPrePosResetCount > 3)
      {
        iPrePos = 0;
        dsyslog(LOG_INFO, "checkOnMark preposreset at %d",index );
        //sleep(10);
      }
    }
    if( (iBestPos == 0) && (checkedFrames > 3) && POS_OK )
    {
      iBestPos = index;
      dsyslog(LOG_INFO, "checkOnMark best pos = %d, prepos = %d",iBestPos, iPrePos );
      iBestPosResetCount = 0;
    }
    else if( (iBestPos > 0) && (checkedFrames > 3)  && POS_NOT_OK )
    {
      iBestPosResetCount++;
      if(iBestPosResetCount > 3)
      {
        dsyslog(LOG_INFO, "checkOnMark reset best pos to 0 at %d", index );
        //sleep(10);
        iBestPos = 0;
        checkedFrames = 0;
      }
    }
  }
  dsyslog(LOG_INFO, "checkOnMark bestPos %d prepos %d",iBestPos,iPrePos );
  if( (iBestPos == 0 || (iPrePos - iBestPos) < 200) && POS_MAYBE_OK && iPrePos > 0 )
  {
    iBestPos = iPrePos;
    dsyslog(LOG_INFO, "checkOnMark use prepos to set newmark" );
  }
  if( iBestPos > 0 )
  {
    int newIndex = cIF->GetNextIFrame( iBestPos-1, true, &FileNumber, &FileOffset, &Length, true);
    if( newIndex > 0 && newIndex != m->position)
    {
      dsyslog(LOG_INFO, "do moveMark from %d to %d, prepos = %d ",m->position, newIndex, iPrePos );
      moveMark( marks, m, newIndex);
      return true;
    }
    if( newIndex == m->position)
    {
      dsyslog(LOG_INFO, "mark is on same position" );
      if(!bForward)
        return false;
    }
  }
  return false;
#undef POS_OK
#undef POS_NOT_OK
#undef POS_MAYBE_OK
#undef PRE_POS_OK
#undef PRE_POS_RESET
}

bool checkBlacklineOffMark( cMarks *marks, cMark *m, cFileName *cfn, int iCheckTime, int iTopLines, int iBottomLines)
{
    #define POS_OK (data->m_nBlackLinesTop >= iTopMin \
     && data->m_nBlackLinesTop <= iTopMax \
     && data->m_nBlackLinesBottom >= ibotmin \
     && data->m_nBlackLinesBottom <= ibotmax)
    #define POS_NOT_OK (data->m_nBlackLinesTop < iTopMin \
     || data->m_nBlackLinesBottom < ibotmin)
    #define POS_MAYBE_OK (data->m_nBlackLinesTop > iTopMin \
     && data->m_nBlackLinesBottom > ibotmin)

   #define PRE_POS_OK (data->m_nBlackLinesTop >= iTopLines && data->m_nBlackLinesBottom >= iBottomLines)
   #define PRE_POS_RESET (data->m_nBlackLinesTop + data->m_nBlackLinesBottom < (iTopLines+iBottomLines)-(((iTopLines+iBottomLines)*20)/100))

  dsyslog(LOG_INFO, "checkBlacklineOffMark at %d %d %d for %d frames",m->position, iTopLines, iBottomLines, iCheckTime );
  int iDiff;
  int iOffset = 0;
  int iBestPos = 0;
  int iPrePos = 0;
  int iEnd = m->position;
//  int topmaxdiff = iTopLines * 20 / 100;
//  int botmaxdiff = iBottomLines * 20 / 100;
  int iTopMax = iTopLines + iTopLines*30/100;
  int iTopMin = iTopLines - iTopLines*20/100;
  int ibotmax = iBottomLines + iBottomLines*30/100;
  int ibotmin = iBottomLines - iBottomLines*30/100;
  dsyslog(LOG_INFO, "check-range top %d %d, bott %d %d",iTopMin,iTopMax,ibotmin,ibotmax );
  iOffset = 0;

  if( m->position < iCheckTime )
    return false;
  iOffset = iCheckTime;
  iOffset *= -1;
  iDiff = FRAMESPERSEC;

  int index = displayIFrame(m->position+iOffset-1);
  ulTopBlackLines = 0;
  ulBotBlackLines = 0;
  checkedFrames = 0;

  while( index < iEnd )
  {
    // get the next frame
    cIF->Get( ++index, &FileNumber, &FileOffset, &PictureType, &Length);
    cfn->SetOffset( FileNumber, FileOffset);
    end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
    iCurrentDecodedFrame = index;
    ulTopBlackLines = 0;
    ulBotBlackLines = 0;
    demux (readBuffer, end, 0);

    if( (checkedFrames > 3) && POS_OK )
    {
      iBestPos = index;
      //dsyslog(LOG_INFO, "checkOffMark best pos = %d, prepos = %d",iBestPos, iPrePos );
    }
    /*
    else if( (iBestPos > 0) && (checkedFrames > 3)  && POS_NOT_OK )
    {
      dsyslog(LOG_INFO, "checkOffMark reset best pos to 0" );
      iBestPos = 0;
      checkedFrames = 0;
    }
    */
  }
  dsyslog(LOG_INFO, "checkOffMark bestPos %d prepos %d",iBestPos,iPrePos );
  if( (iBestPos == 0 || (iPrePos - iBestPos) < 200) && POS_MAYBE_OK && iPrePos > 0 )
  {
    iBestPos = iPrePos;
    dsyslog(LOG_INFO, "checkOffMark use prepos to set newmark" );
  }
  if( iBestPos > 0 )
  {
    int newIndex = cIF->GetNextIFrame( iBestPos-1, true, &FileNumber, &FileOffset, &Length, true);
    if( newIndex > 0 && newIndex != m->position)
    {
      dsyslog(LOG_INFO, "do moveMark from %d to %d, prepos = %d ",m->position, newIndex, iPrePos );
      moveMark( marks, m, newIndex);
      return true;
    }
    if( newIndex == m->position)
    {
      dsyslog(LOG_INFO, "mark is on same position" );
    }
  }
  return false;
#undef POS_OK
#undef POS_NOT_OK
#undef POS_MAYBE_OK
#undef PRE_POS_OK
#undef PRE_POS_RESET
}

#define MONOFRAME_CUTOFF 65
bool checkBlackFrameOnMark( cMarks *marks, cMark *m, cFileName *cfn, bool bForward, int iCheckTime )
{
  dsyslog(LOG_INFO, "checkBlackFrameOnMark at %d %s",m->position, bForward ? "forward":"backward" );
  int iOffset = 0;
  int iBestPos = 0;
  int iEnd = m->position;
//  int minlines = data->m_nGrabHeight*80/100;
  bool bPosReset = true;
  if( bForward )
  {
    if( m->position+iCheckTime*FRAMESPERSEC > cIF->Last() )
      return false;
    iEnd = m->position+iCheckTime*FRAMESPERSEC;
  }
  else
  {
    if( m->position < iCheckTime*FRAMESPERSEC )
      return false;
    iOffset = iCheckTime*FRAMESPERSEC;
    iOffset *= -1;
  }
  int index = displayIFrame(m->position+iOffset-1);
  ulTopBlackLines = 0;
  ulBotBlackLines = 0;
  checkedFrames = 0;

  cbfunc cbf_old = getCB_Func();
  setCB_Func(BlackframeCallback);

  while( index < iEnd )
  {
    index++;
    cIF->Get(index,&FileNumber, &FileOffset, &PictureType, &Length);
    cfn->SetOffset( FileNumber, FileOffset);
    //end = readBuffer + read (cfn->File(), readBuffer, Length > MAXFRAMESIZE ? MAXFRAMESIZE : Length);
    end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
    iCurrentDecodedFrame = index;
    ulTopBlackLines = 0;
    ulBotBlackLines = 0;
    demux (readBuffer, end, 0);
    if( bPosReset && data->m_nMonoFrameValue < MONOFRAME_CUTOFF )
    {
      dsyslog(LOG_INFO, "set iBestPos to %d",index);
      iBestPos = index;
      bPosReset = false;
    }
    if( data->m_nMonoFrameValue > MONOFRAME_CUTOFF )
      bPosReset = true;
  }
  setCB_Func(cbf_old);
  if( iBestPos > 0 )
  {
    int newIndex = cIF->GetNextIFrame( iBestPos-1, true, &FileNumber, &FileOffset, &Length, true);
    if( newIndex > 0 )
    {
      dsyslog(LOG_INFO, "do moveMark from %d to %d ",m->position, newIndex );
      moveMark( marks, m, newIndex);
    }
    return true;
  }
  return false;
}

void checkOnMarks(cMarks *marks, cFileName *cfn)
{
  cbfunc cbf_old = getCB_Func();
  setCB_Func(BlacklineCallback);
  cMark *m = marks->GetNext(-1);

  while( m != NULL )
  {
    int iPos = m->position;
    if( !checkOnMark( marks, m, cfn, true, 60 ) )
      checkOnMark( marks, m, cfn, false, 90 );
    m = marks->GetNext(iPos);
    if( m != NULL )
      m = marks->GetNext(m->position);
  }
  setCB_Func(cbf_old);
}


bool checkBlackFramesOnMarks(cMarks *marks, cFileName *cfn)
{
  dsyslog(LOG_INFO, "check video for BlackFrames" );
  cbfunc cbf_old = getCB_Func();
  setCB_Func(BlacklineCallback);
  cMark *m = marks->GetNext(-1);
  while( m != NULL )
  {
    //dsyslog(LOG_INFO, "Mark: %d %s %d %s", m->position, cpos, iDiff, cdiff);
    int iPos = m->position;
    if( !checkBlackFrameOnMark( marks, m, cfn, false, 45 ) )
      if( checkBlackFrameOnMark( marks, m, cfn, true, 30 ) )
      {
        m = marks->GetNext(iPos);
        iPos = m->position;
      }
    m = marks->GetNext(iPos);
    if( m != NULL )
      m = marks->GetNext(m->position);
  }
  setCB_Func(cbf_old);
  dsyslog(LOG_INFO, "check for Blacklines done" );
  return true;
}

bool detectBlacklines(cMarks *marks, cFileName *cfn, int& iTopLines, int& iBottomLines)
{
#define RESETBLACKLINES  ulTopBlackLines = 0; ulBotBlackLines = 0; checkedFrames = 0; iBlacklineResets++;

  bool bStartpos = false;
  int index = 0;
  dsyslog(LOG_INFO, "detectBlacklines" );
  cbfunc cbf_old = getCB_Func();
  setCB_Func(BlacklineCallback);

  // Startposition finden
  // erste Unterbrechung überspringen
  cMark *m = marks->GetNext(-1);
  if( m != NULL )
    m = marks->GetNext(m->position);
  if( m != NULL )
    m = marks->GetNext(m->position);
  while( !bStartpos && m != NULL )
  {
    // zum nächsten I-Frame
    index = cIF->GetNextIFrame( m->position, true, &FileNumber, &FileOffset, &Length, true);
    // 5 Minuten weiter
    index += 5*FRAMESPERMIN;
    if( m != NULL )
      m = marks->GetNext(m->position);
    // prüfen, ob mind. 2000 Frames verfügbar sind
    if( m != NULL )
    {
      if( index + 2000 < m->position )
        bStartpos = true;
      else
        m = marks->GetNext(m->position);
    }
  }
  if( !bStartpos )
  {
    setCB_Func(cbf_old);
    return false;
  }
  index = displayIFrame(index);
  ulTopBlackLines = 0;
  ulBotBlackLines = 0;
  checkedFrames = 0;
  int iNumFramesNoBlacklines = 0;
  int iBlacklineResets = 0;
  bool bOk = false;
  while( !bOk )
  {
    // Abbruch bei der nächsten Unterbrechung
    if( index > m->position )
    {
      dsyslog(LOG_INFO, "detectBlacklines out of range" );
      setCB_Func(cbf_old);
      return( false );
    }
    // Abbruch, wenn 50 Frames ohne Blacklines gefunden wurden
    if( iNumFramesNoBlacklines > 50 )
    {
      dsyslog(LOG_INFO, "detectBlacklines: video has no Blacklines" );
      setCB_Func(cbf_old);
      return( false );
    }
    // Frame holen und decodieren
    cIF->Get(index,&FileNumber, &FileOffset, NULL, &Length);
    cfn->SetOffset( FileNumber, FileOffset);
    end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
    iCurrentDecodedFrame = index;
    demux (readBuffer, end, 0);
    index++;

//    dsyslog(LOG_INFO, "detectBlacklines (%d) TopBlackLines = %3d, BotBlackLines = %3d (%4d) %d",index,data->m_nBlackLinesTop, data->m_nBlackLinesBottom,data->m_nBlackLinesTop+data->m_nBlackLinesBottom,checkedFrames );
    // Reset-Bedingungen:
    // - Anzahl der Blacklines oben oder unten < MINBLACKLINES
    // - Anzahl der Blacklines oben oder unten > 1/3 der Bildhöhe
    // - Differenz Top- und BottomBlacklines > 50
    if( data->m_nBlackLinesTop < MINBLACKLINES ||
        data->m_nBlackLinesBottom < MINBLACKLINES )
    {
      iNumFramesNoBlacklines++;
      RESETBLACKLINES;
    }
    if( data->m_nBlackLinesTop > data->m_nGrabHeight/3 ||
        data->m_nBlackLinesBottom > data->m_nGrabHeight/3 ||
        abs(data->m_nBlackLinesTop-data->m_nBlackLinesBottom) > 50)
    {
      RESETBLACKLINES;
    }
    // wenn iBlacklineResets > 10, dann 1 Min springen
    if( iBlacklineResets == 10 )
    {
      index += FRAMESPERMIN;
      iBlacklineResets = 0;
    }
    if( checkedFrames > 50 )
      bOk = true;
//    dsyslog(LOG_INFO, "detectBlacklines (%d) TopBlackLines = %3d, BotBlackLines = %3d (%4d) %d",index,data->m_nBlackLinesTop, data->m_nBlackLinesBottom,data->m_nBlackLinesTop+data->m_nBlackLinesBottom,checkedFrames );
  }
  iTopLines = (ulTopBlackLines / checkedFrames)*90/100;
  iBottomLines = (ulBotBlackLines / checkedFrames)*90/100;
  dsyslog(LOG_INFO, "TopBlackLines = %d, BotBlackLines = %d",iTopLines, iBottomLines );
  setCB_Func(cbf_old);
  return true;
#undef RESETBLACKLINES
}

bool detectBlacklines(int _index, int iFramesToCheck, cFileName *cfn, int& iTopLines, int& iBottomLines)
{
#define RESETBLACKLINES  ulTopBlackLines = 0; ulBotBlackLines = 0; checkedFrames = 0; iBlacklineResets++;

  dsyslog(LOG_INFO, "detectBlacklines from %d over %d frames", _index, iFramesToCheck );
  cbfunc cbf_old = getCB_Func();
  setCB_Func(BlacklineCallback);

  int index = displayIFrame(_index);
  ulTopBlackLines = 0;
  ulBotBlackLines = 0;
  checkedFrames = 0;
  int iNumFramesNoBlacklines = 0;
  int iBlacklineResets = 0;
  int endFrame = _index + iFramesToCheck;
  bool bOk = false;
  while( !bOk )
  {
    // Abbruch, wenn 50 Frames ohne Blacklines gefunden wurden
    if( iNumFramesNoBlacklines > 50 || index > endFrame )
    {
      dsyslog(LOG_INFO, "detectBlacklines: video has no Blacklines in range %d %d", _index, iFramesToCheck );
      setCB_Func(cbf_old);
      return( false );
    }
    // Frame holen und decodieren
    cIF->Get(index,&FileNumber, &FileOffset, NULL, &Length);
    cfn->SetOffset( FileNumber, FileOffset);
    end = readBuffer + ReadFrame (cfn->File(), readBuffer, Length, MAXFRAMESIZE);
    iCurrentDecodedFrame = index;
    demux (readBuffer, end, 0);
    index++;

    dsyslog(LOG_INFO, "detectBlacklines (%d) TopBlackLines = %3d, BotBlackLines = %3d (%4d) %d",index,data->m_nBlackLinesTop, data->m_nBlackLinesBottom,data->m_nBlackLinesTop+data->m_nBlackLinesBottom,checkedFrames );
    // Reset-Bedingungen:
    // - Anzahl der Blacklines oben oder unten < MINBLACKLINES
    // - Anzahl der Blacklines oben oder unten > 1/3 der Bildhöhe
    // - Differenz Top- und BottomBlacklines > 50
    if( data->m_nBlackLinesTop < MINBLACKLINES ||
        data->m_nBlackLinesBottom < MINBLACKLINES )
    {
      iNumFramesNoBlacklines++;
      RESETBLACKLINES;
    }
    if( data->m_nBlackLinesTop > data->m_nGrabHeight/3 ||
        data->m_nBlackLinesBottom > data->m_nGrabHeight/3 ||
        abs(data->m_nBlackLinesTop-data->m_nBlackLinesBottom) > 50)
    {
      RESETBLACKLINES;
    }
    // wenn iBlacklineResets > 10, dann 1 Min springen
    if( iBlacklineResets == 10 )
    {
      index += FRAMESPERMIN;
      iBlacklineResets = 0;
    }
    if( checkedFrames > 50 )
      bOk = true;
    dsyslog(LOG_INFO, "detectBlacklines (%d) TopBlackLines = %3d, BotBlackLines = %3d (%4d) %d",index,data->m_nBlackLinesTop, data->m_nBlackLinesBottom,data->m_nBlackLinesTop+data->m_nBlackLinesBottom,checkedFrames );
  }
  iTopLines = (ulTopBlackLines / checkedFrames);//*95/100;
  iBottomLines = (ulBotBlackLines / checkedFrames);//*95/100;
  dsyslog(LOG_INFO, "TopBlackLines = %d, BotBlackLines = %d",iTopLines, iBottomLines );
  setCB_Func(cbf_old);
  return true;
}

/*
*  checkBlacklineOnMarks
*  prüft, ob die Aufnahme schwarze Ränder oben und unten hat
*  ist dies der Fall, werden die Einschaltmarken nach Untersuchung
*  der umliegenden Bilder ggf. angepasst
*  im Normalfall wird das Senderlogo erst einige sekunden nach
*  Filmbeginn eingeblendet, daher wird zuerst rückwärts gesucht
*  ist die rückwärtssuche erfolglos, wird vorwärts gesucht
*  wg. RTL wird vorher von der Schnittmarke+3min rückwärts gesucht
*/
bool checkBlacklineOnMarks(cMarks *marks, cFileName *cfn)
{
  int iTopLines = 0;
  int iBottomLines = 0;
  // prüfen auf Ränder
  detectBlacklines(marks, cfn, iTopLines, iBottomLines);
  if( iTopLines < MINBLACKLINES || iBottomLines < MINBLACKLINES )
  {
    dsyslog(LOG_INFO, "not enough Blacklines for further inspection" );
    return false;
  }

  // callback-funktion setzen
  cbfunc cbf_old = getCB_Func();
  setCB_Func(BlacklineCallback);
  // auf 1. Schnittmarke
  cMark *m = marks->GetNext(-1);
  while( m != NULL )
  {
    int iPos = m->position;
    int iBackFrames = 60*FRAMESPERSEC;      // Anzahl Frames für normale rückwärtssuche
    int iForwardFrames = 200*FRAMESPERSEC;  // Anzahl Frames für vorwärtssuche
    // rückwärtssuche bis max zur vorherigen Schnittmarke
    cMark *m2 = marks->GetPrev(iPos);
    if( m2 != NULL )
    {
      if( m->position - m2->position < iBackFrames )
        iBackFrames = m->position - m2->position -10;
      if( iBackFrames < 0 )
        iBackFrames = 0;
    }
    // vorwärtssuche bis max zur nächsten Schnittmarke
    m2 = marks->GetNext(iPos);
    if( m2 != NULL )
    {
      if( m2->position - m->position < iForwardFrames )
        iForwardFrames = m2->position - m->position -10;
      if( iForwardFrames < 0 )
        iForwardFrames = 0;
    }

    m->position = iPos;
    if( checkBlacklineOnMark( marks, m, cfn, false, iBackFrames, iTopLines, iBottomLines ) )
    {
      m = marks->GetPrev(iPos+1);
      iPos = m->position;
    }
    if( checkBlacklineOnMark( marks, m, cfn, true, iForwardFrames, iTopLines, iBottomLines ) )
    {
      m = marks->GetNext(iPos);
      iPos = m->position;
    }
/*
      if( !checkBlacklineOnMark( marks, m, cfn, false, iBackFrames, iTopLines, iBottomLines ) )
      {
        if( checkBlacklineOnMark( marks, m, cfn, true, iForwardFrames, iTopLines, iBottomLines ) )
        {
          m = marks->GetNext(iPos);
          iPos = m->position;
        }
      }
*/
    m = marks->GetNext(iPos);
    if( m != NULL )
    {
      iPos = m->position;
      if( checkBlacklineOffMark( marks, m, cfn, 500, iTopLines, iBottomLines ) )
      ;
      m = marks->GetNext(iPos+1);
    }
    listMarks( marks);
  }
  setCB_Func(cbf_old);
  dsyslog(LOG_INFO, "check for Blacklines done" );
  return true;
}

void listMarks(cMarks *marks)
{
  dsyslog(LOG_INFO, "current Marks:" );
  int iDiff;
  int iLastPos = 0;
  char *cpos = NULL;
  char *cdiff = NULL;

  cMark *m = marks->GetNext(-1);
//  cMark *mprev = NULL;

  while( m != NULL )
  {
    iDiff = m->position - iLastPos;
    asprintf(&cpos, "%s", m->ToText(false));
    asprintf(&cdiff, "%s", IndexToHMSF(iDiff, false));
    dsyslog(LOG_INFO, "Mark: %6d %s #frames %6d duration %s", m->position, cpos, iDiff, cdiff);
    iLastPos = m->position;
    m = marks->GetNext(m->position);
  }
  dsyslog(LOG_INFO, "current Marks end" );
}

#define MINMARKDURATION (17*FRAMESPERSEC)
#define MINMARKDURATION2 (70*FRAMESPERSEC)
#define ACTIVEMINMARKDURATION (60*FRAMESPERSEC)
// MarkCleanup
// delete all marks where the difference between
// the marks is shorter than MINMARKDURATION frames
void MarkCleanup(cMarks *marks, cFileName *cfn)
{
  int iDiff;
  int iLastPos = 0;
  char *cpos = NULL;
  char *cdiff = NULL;

  listMarks( marks);
  cMark *m = marks->GetNext(-1);
  cMark *mprev = m;
/*
  iLastPos = m->position;
  if( m != NULL )
    m = marks->GetNext(m->position);

  while( m != NULL )
  {
    iDiff = m->position - iLastPos;
    asprintf(&cpos, "%s", m->ToText(false));
    asprintf(&cdiff, "%s", IndexToHMSF(iDiff, false));
    dsyslog(LOG_INFO, "Mark: %d %s #frames %d duration %s", m->position, cpos, iDiff, cdiff);
    iLastPos = m->position;
    if( iDiff > 0 && iDiff < ACTIVEMINMARKDURATION )
    {
      if( mprev != NULL )
      {
        dsyslog(LOG_INFO, "del Marks: %d %d", mprev->position, m->position );
        marks->Del(mprev);
        marks->Del(m);
        m = marks->GetNext(-1);
        mprev = NULL;
        iLastPos = 0;
        bMarkChanged = true;
      }
    }
    else
    {
      m = marks->GetNext(m->position);
      mprev = m;
      if( m != NULL )
      {
        iLastPos = m->position;
        m = marks->GetNext(m->position);
      }
    }
  }
*/

  listMarks( marks);

  m = marks->GetNext(-1);
  mprev = NULL;
  while( m != NULL )
  {
    iDiff = m->position - iLastPos;
    //asprintf(&cpos, "%s", m->ToText(false));
    //asprintf(&cdiff, "%s", IndexToHMSF(iDiff, false));
    //dsyslog(LOG_INFO, "Mark: %d %s #frames %d duration %s", m->position, cpos, iDiff, cdiff);
    iLastPos = m->position;
    if( iDiff > 0 && iDiff < MINMARKDURATION )
    {
      if( mprev != NULL )
      {
        dsyslog(LOG_INFO, "del Mark: %d ", mprev->position );
        marks->Del(mprev);
        marks->Del(m);
        m = marks->GetNext(-1);
        mprev = NULL;
        iLastPos = 0;
        bMarkChanged = true;
      }
    }
    else
    {
      mprev = m;
      m = marks->GetNext(m->position);
    }
  }

  listMarks( marks);

#ifdef CHECK_ONMARKS
  marks->Save();
  checkOnMarks(marks, cfn);
#endif
#ifdef DO_PASS2
  if( !checkBlacklineOnMarks(marks, cfn) )
    checkBlackFramesOnMarks(marks, cfn);
  listMarks( marks);

  m = marks->GetNext(-1);
  mprev = m;
  iLastPos = m->position;
  if( m != NULL )
    m = marks->GetNext(m->position);

  while( m != NULL )
  {
    iDiff = m->position - iLastPos;
    asprintf(&cpos, "%s", m->ToText(false));
    asprintf(&cdiff, "%s", IndexToHMSF(iDiff, false));
    dsyslog(LOG_INFO, "Mark: %d %s #frames %d duration %s", m->position, cpos, iDiff, cdiff);
    iLastPos = m->position;
    if( iDiff > 0 && iDiff < ACTIVEMINMARKDURATION )
    {
      if( mprev != NULL )
      {
        dsyslog(LOG_INFO, "del Marks: %d %d", mprev->position, m->position );
        marks->Del(mprev);
        marks->Del(m);
        m = marks->GetNext(-1);
        mprev = NULL;
        iLastPos = 0;
        bMarkChanged = true;
      }
    }
    else
    {
      m = marks->GetNext(m->position);
      mprev = m;
      if( m != NULL )
      {
        iLastPos = m->position;
        m = marks->GetNext(m->position);
      }
    }
  }
  listMarks( marks);

  m = marks->GetNext(-1);
  mprev = NULL;
  while( m != NULL )
  {
    iDiff = m->position - iLastPos;
    asprintf(&cpos, "%s", m->ToText(false));
    asprintf(&cdiff, "%s", IndexToHMSF(iDiff, false));
    dsyslog(LOG_INFO, "Mark: %d %s #frames %d duration %s", m->position, cpos, iDiff, cdiff);
    iLastPos = m->position;
    if( iDiff > 0 && iDiff < MINMARKDURATION )
    {
      if( mprev != NULL )
      {
        dsyslog(LOG_INFO, "del Mark: %d ", mprev->position );
        marks->Del(mprev);
        marks->Del(m);
        m = marks->GetNext(-1);
        mprev = NULL;
        iLastPos = 0;
        bMarkChanged = true;
      }
    }
    else
    {
      mprev = m;
      m = marks->GetNext(m->position);
    }
  }

  listMarks( marks);

#endif
}

// scan the hole record for the Logo and set
// the cutting-marks
int scanRecord( int iNumFrames)
{
  time_t start, startall;
  time_t end;

  totalDecodedFrames = 0;
  decodedFramesForLogoDetection = 0;
  decodedFramesForLogoCheck = 0;
  secsForLogoDetection = 0;
  secsForLogoCheck = 0;
  secsForScan = 0;

  start = startall = time(NULL);
  // open the cIndexFile for the record
  cIF = new cNoadIndexFile(filename,false);
  if( cIF == NULL )
    return -1;
  if(cIF->Last() == iNumFrames)
  {
    delete cIF;
    return iNumFrames;
  }

  // open the record
  cfn = new cFileName(filename, false);
  if( cfn->Open() < 0 )
  {
    delete cfn;
    delete cIF;
    return -1;
  }
  demux_track = getVStreamID(cfn->File());

  char logoname[2048];
  strcpy( logoname, filename);
  strcat( logoname, "/cur.logo" );

  if( !detectLogo(cfn,logoname) )
  {
    delete cfn;
    delete cIF;
    return 0;
  }
  data->saveCheckData(logoname, true);

  int iCurrentFrame, iStopFrame;
  int iState = 0;
  int iOldState = -1;

  iCurrentFrame = 0;
  iStopFrame = 0;
  cMarks marks;
  marks.Load(filename);

  marks.ClearList();

  while(  iCurrentFrame < cIF->Last() && iCurrentFrame >= 0 && iState != -1 )
  {
    iOldState = iState;
    iState = findLogoChange(cfn, iState, iCurrentFrame, BIGSTEP );
    dsyslog(LOG_INFO, "StateChanged: newstate=%d oldstate=%d currentFrame=%d", iState, iOldState, iCurrentFrame);
    if( iState != iOldState && iState != -1 )
    {
        //iCurrentFrame -= (BIGSTEP+BIGSTEP/2);
        iCurrentFrame -= (BIGSTEP*2);
      if( iCurrentFrame < iStopFrame )
        iCurrentFrame += (BIGSTEP/2);
      if( iCurrentFrame < iStopFrame )
        iCurrentFrame = iStopFrame;
      iState = findLogoChange(cfn, iOldState, iCurrentFrame, SMALLSTEP, iState==1?LOGOSTABLETIME:0 );
      if( iState >= 0 )
      {
        int iLastLogoFrame = cIF->GetNextIFrame( iLastIFrame-1, false, &FileNumber, &FileOffset, &Length, true);
        MarkToggle(&marks, iLastLogoFrame);
      }
    }
    iStopFrame = iCurrentFrame = iCurrentFrame+1;
  }
  if( marks.Count() & 1 )
    marks.Add(cIF->Last());
  MarkCleanup(&marks, cfn);
  marks.Save();
  setCB_Func(NULL);

  end = time(NULL);
  secsForScan = end - startall;

  dsyslog(LOG_INFO, "totalDecodedFrames %d",totalDecodedFrames );
  dsyslog(LOG_INFO, "decodedFramesForLogoDetection %d",decodedFramesForLogoDetection );
  dsyslog(LOG_INFO, "decodedFramesForLogoCheck %d",decodedFramesForLogoCheck );
  dsyslog(LOG_INFO, "secsForLogoDetection %d",secsForLogoDetection );
  dsyslog(LOG_INFO, "secsForLogoCheck %d",secsForLogoCheck );
  dsyslog(LOG_INFO, "secsForScan %d",secsForScan );

  int iRet = cIF->Last();
  delete cfn;
  delete cIF;
  return iRet;
}

const char *getVersion()
{
  return VERSION;
}


int doX11Scan(noadData *thedata, char *fName, int iNumFrames )
{
  if( fName == NULL )
    return -1;
  else
    filename = fName;
  data = thedata;
  demux_track = 0xe0;
  mpeg2dec = mpeg2_init ();
  int iRet = scanRecord(iNumFrames);
  mpeg2_close (mpeg2dec);
  return iRet;
}

const char *myTime(time_t tim)
{
  static char t_buf[2048];
  strftime(t_buf,2048,"%A,%d.%m.%Y %T",localtime(&tim));
  return t_buf;
}

