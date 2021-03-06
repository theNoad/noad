// vdr-classes
#ifndef VDR_CL_H
#define VDR_CL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
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
#include <getopt.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#include <signal.h>
#endif
#include "tools.h"

typedef unsigned char uchar;
#define INDEXFILESUFFIX     "/index.vdr"
#define INDEXFILESUFFIXEX   "/indexEx.vdr"
#define RECORDFILESUFFIX    "/%03d.vdr"
#define RECORDFILESUFFIXLEN 20 // some additional bytes for safety...
#define RESUMEFILESUFFIX  "/resume.vdr"
#define MARKSFILESUFFIX   "/marks.vdr"

#define MAXFILESPERRECORDING 255

// The maximum time to wait before giving up while catching up on an index file:
#define MAXINDEXCATCHUP   2 // seconds

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

#define FRAMESPERSEC 25
#define FRAMESPERMIN 1500
//#define LOGOSTABLETIME 60*FRAMESPERSEC
//#define LOGOSTABLETIME 2*FRAMESPERMIN


//#define esyslog(...) void( (SysLogLevel > 0) ? syslog(__VA_ARGS__) : void() )
//#define isyslog(...) void( (SysLogLevel > 1) ? syslog(__VA_ARGS__) : void() )
//#define dsyslog(...) void( (SysLogLevel > 2) ? syslog(__VA_ARGS__) : void() )

//#define LOG_ERROR         esyslog(LOG_ERR, "ERROR (%s,%d): %s", __FILE__, __LINE__,strerror(errno))
//#define LOG_ERROR_STR(s)  esyslog(LOG_ERR, "ERROR: %s: %s", s,strerror(errno))

extern int SysLogLevel;


// --- cFileName -------------------------------------------------------------
class cFileName
{
private:
  int file;
  int fileNumber;
  char *fileName, *pFileNumber;
  bool record;
  bool blocking;
public:
  cFileName(const char *FileName, bool Record, bool Blocking = false);
  ~cFileName();
  const char *Name(void) { return fileName; }
  int Number(void) { return fileNumber; }
  int Open(void);
  void Close(void);
  int SetOffset(int Number, int Offset = 0);
  int NextFile(void);
  // NoAd:
  int File() { return file; }
};

// --- cResumeFile ------------------------------------------------------------
class cResumeFile
{
private:
  char *fileName;
public:
  cResumeFile(const char *FileName);
  ~cResumeFile();
  int Read(void);
  bool Save(int Index);
  void Delete(void);
};


// --- cIndexFile ------------------------------------------------------------
class cIndexFile
{
protected:
  struct tIndex { int offset; uchar type; uchar number; short reserved; };
  int f;
  char *fileName;
  int size, last;
  tIndex *index;
  cResumeFile resumeFile;
  bool CatchUp(int Index = -1);
public:
  cIndexFile(const char *FileName, bool Record);
  ~cIndexFile();
  bool Ok(void) { return index != NULL; }
  bool Write(uchar PictureType, uchar FileNumber, int FileOffset);
  bool Get(int Index, uchar *FileNumber, int *FileOffset, uchar *PictureType = NULL, int *Length = NULL);
  int GetNextIFrame(int Index, bool Forward, uchar *FileNumber = NULL, int *FileOffset = NULL, int *Length = NULL, bool StayOffEnd = false);
  int Get(uchar FileNumber, int FileOffset);
  int Last(void) { CatchUp(); return last; }
  int GetResume(void) { return resumeFile.Read(); }
  bool StoreResume(int Index) { return resumeFile.Save(Index); }
#ifdef NOAD
  int getLast() { return( last ); }
#endif

};

// --- cNoadIndexFile ------------------------------------------------------------
class cNoadIndexFile : public cIndexFile
{
  struct tIndexEx{ int isLogo; int blackTop; int blackBottom; };
  int fEx;
  char *fileNameEx;
  int sizeEx, lastEx;
  tIndexEx *indexEx;
public:
  cNoadIndexFile(const char *FileName, bool Record);
  ~cNoadIndexFile();
  bool Get(int Index, uchar *FileNumber, int *FileOffset, uchar *PictureType = NULL, int *Length = NULL);
  int GetNextIFrame(int Index, bool Forward, uchar *FileNumber = NULL, int *FileOffset = NULL, int *Length = NULL, bool StayOffEnd = false);
  int Get(uchar FileNumber, int FileOffset);
  int Last(void) { return last; }
  bool setIndexEx( int index, int _isLogo, int _blackTop, int _blackBottom);
  void logIndexEx();
};


// --- cMark ------------------------------------------------------------
class cMark : public cListObject
{
private:
  static char *buffer;
public:
  int position;
  char *comment;
  cMark(int Position = 0, const char *Comment = NULL);
  ~cMark();
  const char *ToText(bool bWithNewline = true);
  bool Parse(const char *s);
  bool Save(FILE *f);
};

template<class T> class cConfig : public cList<T>
{
private:
  char *fileName;
public:
  void Clear(void)
  {
    delete fileName;
    cList<T>::Clear();
  }
public:
  cConfig(void) { fileName = NULL; }
  virtual ~cConfig() { delete fileName; }
  virtual bool Load(const char *FileName, bool AllowComments = false)
  {
    Clear();
    fileName = strdup(FileName);
    bool result = false;
    if (access(FileName, F_OK) == 0)
    {
      isyslog(LOG_INFO, "loading %s", FileName);
      FILE *f = fopen(fileName, "r");
      if (f)
      {
        int line = 0;
        char buffer[MAXPARSEBUFFER];
        result = true;
        while (fgets(buffer, sizeof(buffer), f) > 0)
        {
          line++;
          if (AllowComments)
          {
            char *p = strchr(buffer, '#');
            if (p)
              *p = 0;
          }
          if (!isempty(buffer))
          {
            T *l = new T;
            if (l->Parse(buffer))
              Add(l);
            else
            {
              esyslog(LOG_ERR, "error in %s, line %d\n", fileName, line);
              delete l;
              result = false;
              break;
            }
          }
        }
        fclose(f);
      }
      else
        LOG_ERROR_STR(fileName);
    }
    return result;
  }
  bool Save(void)
  {
    bool result = true;
    T *l = (T *)First();
    cSafeFile f(fileName);
    if (f.Open())
    {
      while (l)
      {
        if (!l->Save(f))
        {
          result = false;
          break;
        }
        l = (T *)l->Next();
      }
      if (!f.Close())
        result = false;
    }
    else
      result = false;
    return result;
  }
  //#ifdef NOAD
public:
  void ClearList(void)
  {
    cList<T>::Clear();
  }
  //#endif
};


// --- cMarks -------------------------------------------------------------
class cMarks : public cConfig<cMark>
{
public:
  bool Load(const char *RecordingFileName, bool AllowComments = false);
  void Sort(void);
  cMark *Add(int Position);
  cMark *Get(int Position);
  cMark *GetPrev(int Position);
  cMark *GetNext(int Position);
  int nextActivePosition(int Position);
};

const char *IndexToHMSF(int Index, bool WithFrame = false);
int HMSFToIndex(const char *HMSF);
 
#endif
