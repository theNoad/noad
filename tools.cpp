/*
 * tools.c: Various tools
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: tools.c 1.62 2002/03/31 20:51:06 kls Exp $
 */

#include "tools.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
//#include "i18n.h"

extern int SysLogLevel;

ssize_t safe_read(int filedes, void *buffer, size_t size)
{
  for (;;) {
      ssize_t p = read(filedes, buffer, size);
      if (p < 0 && errno == EINTR) {
         dsyslog(LOG_INFO, "EINTR while reading from file handle %d - retrying", filedes);
         continue;
         }
      return p;
      }
}

ssize_t safe_write(int filedes, const void *buffer, size_t size)
{
  ssize_t p = 0;
  ssize_t written = size;
  const unsigned char *ptr = (const unsigned char *)buffer;
  while (size > 0) {
        p = write(filedes, ptr, size);
        if (p < 0) {
           if (errno == EINTR) {
              dsyslog(LOG_INFO, "EINTR while writing to file handle %d - retrying", filedes);
              continue;
              }
           break;
           }
        ptr  += p;
        size -= p;
        }
  return p < 0 ? p : written;
}

void writechar(int filedes, char c)
{
  safe_write(filedes, &c, sizeof(c));
}

char *readline(FILE *f)
{
  static char buffer[MAXPARSEBUFFER];
  if (fgets(buffer, sizeof(buffer), f) > 0) {
     int l = strlen(buffer) - 1;
     if (l >= 0 && buffer[l] == '\n')
        buffer[l] = 0;
     return buffer;
     }
  return NULL;
}

char *strcpyrealloc(char *dest, const char *src)
{
  if (src) {
     int l = max(dest ? strlen(dest) : 0, strlen(src)) + 1; // don't let the block get smaller!
     dest = (char *)realloc(dest, l);
     if (dest)
        strcpy(dest, src);
     else
        esyslog(LOG_ERR, "ERROR: out of memory");
     }
  else {
     delete dest;
     dest = NULL;
     }
  return dest;
}

char *strn0cpy(char *dest, const char *src, size_t n)
{
  char *s = dest;
  for ( ; --n && (*dest = *src) != 0; dest++, src++) ;
  *dest = 0;
  return s;
}

char *strreplace(char *s, char c1, char c2)
{
  char *p = s;

  while (p && *p) {
        if (*p == c1)
           *p = c2;
        p++;
        }
  return s;
}

char *strreplace(char *s, const char *s1, const char *s2)
{
  char *p = strstr(s, s1);
  if (p) {
     int of = p - s;
     int l  = strlen(s);
     int l1 = strlen(s1);
     int l2 = strlen(s2);
     if (l2 > l1)
        s = (char *)realloc(s, strlen(s) + l2 - l1 + 1);
     if (l2 != l1)
        memmove(s + of + l2, s + of + l1, l - of - l1 + 1);
     strncpy(s + of, s2, l2);
     }
  return s;
}

char *skipspace(const char *s)
{
  while (*s && isspace(*s))
        s++;
  return (char *)s;
}

char *stripspace(char *s)
{
  if (s && *s) {
     for (char *p = s + strlen(s) - 1; p >= s; p--) {
         if (!isspace(*p))
            break;
         *p = 0;
         }
     }
  return s;
}

char *compactspace(char *s)
{
  if (s && *s) {
     char *t = stripspace(skipspace(s));
     char *p = t;
     while (p && *p) {
           char *q = skipspace(p);
           if (q - p > 1)
              memmove(p + 1, q, strlen(q) + 1);
           p++;
           }
     if (t != s)
        memmove(s, t, strlen(t) + 1);
     }
  return s;
}

const char *strescape(const char *s, const char *chars)
{
  static char *buffer = NULL;
  const char *p = s;
  char *t = NULL;
  while (*p) {
        if (strchr(chars, *p)) {
           if (!t) {
              buffer = (char *)realloc(buffer, 2 * strlen(s) + 1);
              t = buffer + (p - s);
              s = strcpy(buffer, s);
              }
           *t++ = '\\';
           }
        if (t)
           *t++ = *p;
        p++;
        }
  if (t)
     *t = 0;
  return s;
}

bool startswith(const char *s, const char *p)
{
  while (*p) {
        if (*p++ != *s++)
           return false;
        }
  return true;
}

bool endswith(const char *s, const char *p)
{
  const char *se = s + strlen(s) - 1;
  const char *pe = p + strlen(p) - 1;
  while (pe >= p) {
        if (*pe-- != *se-- || (se < s && pe >= p))
           return false;
        }
  return true;
}

bool isempty(const char *s)
{
  return !(s && *skipspace(s));
}

int time_ms(void)
{
  static time_t t0 = 0;
  struct timeval t;
  if (gettimeofday(&t, NULL) == 0) {
     if (t0 == 0)
        t0 = t.tv_sec; // this avoids an overflow (we only work with deltas)
     return (t.tv_sec - t0) * 1000 + t.tv_usec / 1000;
     }
  return 0;
}

void delay_ms(int ms)
{
  int t0 = time_ms();
  while (time_ms() - t0 < ms)
        ;
}

bool isnumber(const char *s)
{
  while (*s) {
        if (!isdigit(*s))
           return false;
        s++;
        }
  return true;
}

const char *AddDirectory(const char *DirName, const char *FileName)
{
  static char *buf = NULL;
  delete buf;
  asprintf(&buf, "%s/%s", DirName && *DirName ? DirName : ".", FileName);
  return buf;
}

int FreeDiskSpaceMB(const char *Directory, int *UsedMB)
{
  if (UsedMB)
     *UsedMB = 0;
  int Free = 0;
  struct statfs statFs;
  if (statfs(Directory, &statFs) == 0) {
     int blocksPerMeg = 1024 * 1024 / statFs.f_bsize;
     if (UsedMB)
        *UsedMB = (statFs.f_blocks - statFs.f_bfree) / blocksPerMeg;
     Free = statFs.f_bavail / blocksPerMeg;
     }
  else
     LOG_ERROR_STR(Directory);
  return Free;
}

bool DirectoryOk(const char *DirName, bool LogErrors)
{
  struct stat ds;
  if (stat(DirName, &ds) == 0) {
     if (S_ISDIR(ds.st_mode)) {
        if (access(DirName, R_OK | W_OK | X_OK) == 0)
           return true;
        else if (LogErrors)
           esyslog(LOG_ERR, "ERROR: can't access %s", DirName);
        }
     else if (LogErrors)
        esyslog(LOG_ERR, "ERROR: %s is not a directory", DirName);
     }
  else if (LogErrors)
     LOG_ERROR_STR(DirName);
  return false;
}

bool MakeDirs(const char *FileName, bool IsDirectory)
{
  bool result = true;
  char *s = strdup(FileName);
  char *p = s;
  if (*p == '/')
     p++;
  while ((p = strchr(p, '/')) != NULL || IsDirectory) {
        if (p)
           *p = 0;
        struct stat fs;
        if (stat(s, &fs) != 0 || !S_ISDIR(fs.st_mode)) {
           dsyslog(LOG_INFO, "creating directory %s", s);
           if (mkdir(s, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1) {
              LOG_ERROR_STR(s);
              result = false;
              break;
              }
           }
        if (p)
           *p++ = '/';
        else
           break;
        }
  delete s;
  return result;
}

bool RemoveFileOrDir(const char *FileName, bool FollowSymlinks)
{
  struct stat st;
  if (stat(FileName, &st) == 0) {
     if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(FileName);
        if (d) {
           struct dirent *e;
           while ((e = readdir(d)) != NULL) {
                 if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
                    char *buffer;
                    asprintf(&buffer, "%s/%s", FileName, e->d_name);
                    if (FollowSymlinks) {
                       int size = strlen(buffer) * 2; // should be large enough
                       char *l = new char[size];
                       int n = readlink(buffer, l, size);
                       if (n < 0) {
                          if (errno != EINVAL)
                             LOG_ERROR_STR(buffer);
                          }
                       else if (n < size) {
                          l[n] = 0;
                          dsyslog(LOG_INFO, "removing %s", l);
                          if (remove(l) < 0)
                             LOG_ERROR_STR(l);
                          }
                       else
                          esyslog(LOG_ERR, "ERROR: symlink name length (%d) exceeded anticipated buffer size (%d)", n, size);
                       delete l;
                       }
                    dsyslog(LOG_INFO, "removing %s", buffer);
                    if (remove(buffer) < 0)
                       LOG_ERROR_STR(buffer);
                    delete buffer;
                    }
                 }
           closedir(d);
           }
        else {
           LOG_ERROR_STR(FileName);
           return false;
           }
        }
     dsyslog(LOG_INFO, "removing %s", FileName);
     if (remove(FileName) < 0) {
        LOG_ERROR_STR(FileName);
        return false;
        }
     }
  else if (errno != ENOENT) {
     LOG_ERROR_STR(FileName);
     return false;
     }
  return true;
}

bool RemoveEmptyDirectories(const char *DirName, bool RemoveThis)
{
  DIR *d = opendir(DirName);
  if (d) {
     bool empty = true;
     struct dirent *e;
     while ((e = readdir(d)) != NULL) {
           if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && strcmp(e->d_name, "lost+found")) {
              char *buffer;
              asprintf(&buffer, "%s/%s", DirName, e->d_name);
              struct stat st;
              if (stat(buffer, &st) == 0) {
                 if (S_ISDIR(st.st_mode)) {
                    if (!RemoveEmptyDirectories(buffer, true))
                       empty = false;
                    }
                 else
                    empty = false;
                 }
              else {
                 LOG_ERROR_STR(buffer);
                 delete buffer;
                 return false;
                 }
              delete buffer;
              }
           }
     closedir(d);
     if (RemoveThis && empty) {
        dsyslog(LOG_INFO, "removing %s", DirName);
        if (remove(DirName) < 0) {
           LOG_ERROR_STR(DirName);
           return false;
           }
        }
     return empty;
     }
  else
     LOG_ERROR_STR(DirName);
  return false;
}

char *ReadLink(const char *FileName)
{
  char RealName[_POSIX_PATH_MAX];
  const char *TargetName = NULL;
  int n = readlink(FileName, RealName, sizeof(RealName) - 1);
  if (n < 0) {
     if (errno == ENOENT || errno == EINVAL) // file doesn't exist or is not a symlink
        TargetName = FileName;
     else // some other error occurred
        LOG_ERROR_STR(FileName);
     }
  else if (n < int(sizeof(RealName))) { // got it!
     RealName[n] = 0;
     TargetName = RealName;
     }
  else
     esyslog(LOG_ERR, "ERROR: symlink's target name too long: %s", FileName);
  return TargetName ? strdup(TargetName) : NULL;
}

bool SpinUpDisk(const char *FileName)
{
  static char *buf = NULL;
  for (int n = 0; n < 10; n++) {
      delete buf;
      if (DirectoryOk(FileName))
         asprintf(&buf, "%s/vdr-%06d", *FileName ? FileName : ".", n);
      else
         asprintf(&buf, "%s.vdr-%06d", FileName, n);
      if (access(buf, F_OK) != 0) { // the file does not exist
         timeval tp1, tp2;
         gettimeofday(&tp1, NULL);
         int f = open(buf, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
         // O_SYNC doesn't work on all file systems
         if (f >= 0) {
            close(f);
            system("sync");
            remove(buf);
            gettimeofday(&tp2, NULL);
            double seconds = (((long long)tp2.tv_sec * 1000000 + tp2.tv_usec) - ((long long)tp1.tv_sec * 1000000 + tp1.tv_usec)) / 1000000.0;
            if (seconds > 0.5)
               dsyslog(LOG_INFO, "SpinUpDisk took %.2f seconds\n", seconds);
            return true;
            }
         else
            LOG_ERROR_STR(buf);
         }
      }
  esyslog(LOG_ERR, "ERROR: SpinUpDisk failed");
  return false;
}

const char *WeekDayName(int WeekDay)
{
  static char buffer[4];
  WeekDay = WeekDay == 0 ? 6 : WeekDay - 1; // we start with monday==0!
  if (0 <= WeekDay && WeekDay <= 6) {
//     const char *day = tr("MonTueWedThuFriSatSun");
     const char *day = "MonTueWedThuFriSatSun";
     day += WeekDay * 3;
     strncpy(buffer, day, 3);
     return buffer;
     }
  else
     return "???";
}

const char *DayDateTime(time_t t)
{
  static char buffer[32];
  if (t == 0)
     time(&t);
  struct tm tm_r;
  tm *tm = localtime_r(&t, &tm_r);
  snprintf(buffer, sizeof(buffer), "%s %2d.%02d %02d:%02d", WeekDayName(tm->tm_wday), tm->tm_mday, tm->tm_mon + 1, tm->tm_hour, tm->tm_min);
  return buffer;
}

// --- cFile -----------------------------------------------------------------

bool cFile::files[FD_SETSIZE] = { false };
int cFile::maxFiles = 0;

cFile::cFile(void)
{
  f = -1;
}

cFile::~cFile()
{
  Close();
}

bool cFile::Open(const char *FileName, int Flags, mode_t Mode)
{
  if (!IsOpen())
     return Open(open(FileName, Flags, Mode));
  esyslog(LOG_ERR, "ERROR: attempt to re-open %s", FileName);
  return false;
}

bool cFile::Open(int FileDes)
{
  if (FileDes >= 0) {
     if (!IsOpen()) {
        f = FileDes;
        if (f >= 0) {
           if (f < FD_SETSIZE) {
              if (f >= maxFiles)
                 maxFiles = f + 1;
              if (!files[f])
                 files[f] = true;
              else
                 esyslog(LOG_ERR, "ERROR: file descriptor %d already in files[]", f);
              return true;
              }
           else
              esyslog(LOG_ERR, "ERROR: file descriptor %d is larger than FD_SETSIZE (%d)", f, FD_SETSIZE);
           }
        }
     else
        esyslog(LOG_ERR, "ERROR: attempt to re-open file descriptor %d", FileDes);
     }
  return false;
}

void cFile::Close(void)
{
  if (f >= 0) {
     close(f);
     files[f] = false;
     f = -1;
     }
}

bool cFile::Ready(bool Wait)
{
  return f >= 0 && AnyFileReady(f, Wait ? 1000 : 0);
}

bool cFile::AnyFileReady(int FileDes, int TimeoutMs)
{
  fd_set set;
  FD_ZERO(&set);
  for (int i = 0; i < maxFiles; i++) {
      if (files[i])
         FD_SET(i, &set);
      }
  if (0 <= FileDes && FileDes < FD_SETSIZE && !files[FileDes])
     FD_SET(FileDes, &set); // in case we come in with an arbitrary descriptor
  if (TimeoutMs == 0)
     TimeoutMs = 10; // load gets too heavy with 0
  struct timeval timeout;
  timeout.tv_sec  = TimeoutMs / 1000;
  timeout.tv_usec = (TimeoutMs % 1000) * 1000;
  return select(FD_SETSIZE, &set, NULL, NULL, &timeout) > 0 && (FileDes < 0 || FD_ISSET(FileDes, &set));
}

bool cFile::FileReady(int FileDes, int TimeoutMs)
{
  fd_set set;
  struct timeval timeout;
  FD_ZERO(&set);
  FD_SET(FileDes, &set);
  if (TimeoutMs < 100)
     TimeoutMs = 100;
  timeout.tv_sec  = TimeoutMs / 1000;
  timeout.tv_usec = (TimeoutMs % 1000) * 1000;
  return select(FD_SETSIZE, &set, NULL, NULL, &timeout) > 0 && FD_ISSET(FileDes, &set);
}

bool cFile::FileReadyForWriting(int FileDes, int TimeoutMs)
{
  fd_set set;
  struct timeval timeout;
  FD_ZERO(&set);
  FD_SET(FileDes, &set);
  if (TimeoutMs < 100)
     TimeoutMs = 100;
  timeout.tv_sec  = 0;
  timeout.tv_usec = TimeoutMs * 1000;
  return select(FD_SETSIZE, NULL, &set, NULL, &timeout) > 0 && FD_ISSET(FileDes, &set);
}

// --- cSafeFile -------------------------------------------------------------

cSafeFile::cSafeFile(const char *FileName)
{
  f = NULL;
  fileName = ReadLink(FileName);
  tempName = fileName ? new char[strlen(fileName) + 5] : NULL;
  if (tempName)
     strcat(strcpy(tempName, fileName), ".$$$");
}

cSafeFile::~cSafeFile()
{
  if (f)
     fclose(f);
  unlink(tempName);
  delete fileName;
  delete tempName;
}

bool cSafeFile::Open(void)
{
  if (!f && fileName && tempName) {
     f = fopen(tempName, "w");
     if (!f)
        LOG_ERROR_STR(tempName);
     }
  return f != NULL;
}

bool cSafeFile::Close(void)
{
  bool result = true;
  if (f) {
     if (ferror(f) != 0) {
        LOG_ERROR_STR(tempName);
        result = false;
        }
     if (fclose(f) < 0) {
        LOG_ERROR_STR(tempName);
        result = false;
        }
     f = NULL;
     if (result && rename(tempName, fileName) < 0) {
        LOG_ERROR_STR(fileName);
        result = false;
        }
     }
  else
     result = false;
  return result;
}

// --- cLockFile -------------------------------------------------------------

#define LOCKFILENAME      ".lock-vdr"
#define LOCKFILESTALETIME 600 // seconds before considering a lock file "stale"

cLockFile::cLockFile(const char *Directory)
{
  fileName = NULL;
  f = -1;
  if (DirectoryOk(Directory))
     asprintf(&fileName, "%s/%s", Directory, LOCKFILENAME);
}

cLockFile::~cLockFile()
{
  Unlock();
  delete fileName;
}

bool cLockFile::Lock(int WaitSeconds)
{
  if (f < 0 && fileName) {
     time_t Timeout = time(NULL) + WaitSeconds;
     do {
        f = open(fileName, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (f < 0) {
           if (errno == EEXIST) {
              struct stat fs;
              if (stat(fileName, &fs) == 0) {
                 if (time(NULL) - fs.st_mtime > LOCKFILESTALETIME) {
                    esyslog(LOG_ERR, "ERROR: removing stale lock file '%s'", fileName);
                    if (remove(fileName) < 0) {
                       LOG_ERROR_STR(fileName);
                       break;
                       }
                    continue;
                    }
                 }
              else if (errno != ENOENT) {
                 LOG_ERROR_STR(fileName);
                 break;
                 }
              }
           else {
              LOG_ERROR_STR(fileName);
              break;
              }
           if (WaitSeconds)
              sleep(1);
           }
        } while (f < 0 && time(NULL) < Timeout);
     }
  return f >= 0;
}

void cLockFile::Unlock(void)
{
  if (f >= 0) {
     close(f);
     remove(fileName);
     f = -1;
     }
  else
     esyslog(LOG_ERR, "ERROR: attempt to unlock %s without holding a lock!", fileName);
}

// --- cListObject -----------------------------------------------------------

cListObject::cListObject(void)
{
  prev = next = NULL;
}

cListObject::~cListObject()
{
}

void cListObject::Append(cListObject *Object)
{
  next = Object;
  Object->prev = this;
}

void cListObject::Unlink(void)
{
  if (next)
     next->prev = prev;
  if (prev)
     prev->next = next;
  next = prev = NULL;
}

int cListObject::Index(void)
{
  cListObject *p = prev;
  int i = 0;

  while (p) {
        i++;
        p = p->prev;
        }
  return i;
}

// --- cListBase -------------------------------------------------------------

cListBase::cListBase(void)
{ 
  objects = lastObject = NULL;
}

cListBase::~cListBase()
{
  Clear();
}

void cListBase::Add(cListObject *Object) 
{ 
  if (lastObject)
     lastObject->Append(Object);
  else
     objects = Object;
  lastObject = Object;
}

void cListBase::Del(cListObject *Object)
{
  if (Object == objects)
     objects = Object->Next();
  if (Object == lastObject)
     lastObject = Object->Prev();
  Object->Unlink();
  delete Object;
}

void cListBase::Move(int From, int To)
{
  Move(Get(From), Get(To));
}

void cListBase::Move(cListObject *From, cListObject *To)
{
  if (From && To) {
     if (From->Index() < To->Index())
        To = To->Next();
     if (From == objects)
        objects = From->Next();
     if (From == lastObject)
        lastObject = From->Prev();
     From->Unlink();
     if (To) {
        if (To->Prev())
           To->Prev()->Append(From);
        From->Append(To);
        }
     else {
        lastObject->Append(From);
        lastObject = From;
        }
     if (!From->Prev())
        objects = From;
     }
}

void cListBase::Clear(void)
{
  while (objects) {
        cListObject *object = objects->Next();
        delete objects;
        objects = object;
        }
  objects = lastObject = NULL;
}

cListObject *cListBase::Get(int Index) const
{
  if (Index < 0)
     return NULL;
  cListObject *object = objects;
  while (object && Index-- > 0)
        object = object->Next();
  return object;
}

int cListBase::Count(void) const
{
  int n = 0;
  cListObject *object = objects;

  while (object) {
        n++;
        object = object->Next();
        }
  return n;
}

void cListBase::Sort(void)
{
  bool swapped;
  do {
     swapped = false;
     cListObject *object = objects;
     while (object) {
           if (object->Next() && *object->Next() < *object) {
              Move(object->Next(), object);
              swapped = true;
              }
           object = object->Next();
           }
     } while (swapped);
}

// --- ReadFrame -------------------------------------------------------------

int ReadFrame(int f, unsigned char *b, int Length, int Max)
{
  if (Length == -1)
     Length = Max; // this means we read up to EOF (see cIndex)
  else if (Length > Max) {
     esyslog(LOG_ERR, "ERROR: frame larger than buffer (%d > %d)", Length, Max);
     Length = Max;
     }
  //int r = safe_read(f, b, Length);
  int r = read( f,b,Length);
  if (r < 0)
     LOG_ERROR;
  return r;
}

#define VIDEO_STREAM_S   0xE0
#define VIDEO_STREAM_E   0xEF

int getVStreamID(int f)
{
  unsigned char buf[8192];
  int r = read( f,buf,8192);
  int i = 0;
  int found = 0;
  int iRet = VIDEO_STREAM_S;
  while( i < r )
  {
    switch(found)
    {
      case 0:
      case 1:
         if(buf[i] == 0 )
           found++;
      break;
      
      case 2:
         if(buf[i] == 1 )
           found++;
      break;

      case 3:
         if(buf[i] >= VIDEO_STREAM_S && buf[i] <= VIDEO_STREAM_E )
         {
           iRet = buf[i];
           return iRet;
         }
      break;

      default:
        found = 0;
      break;
    }
    i++;
  }
  return iRet;
}

