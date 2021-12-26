/***************************************************************************
                          main.cpp  -  description
                             -------------------
    begin                : Son Okt 20 11:24:44 CEST 2002
    copyright            : (C) 2002 by
    email                :
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

/*
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
*/

#include <sstream>
#include <stdlib.h>
#include "noad.h"
void doNoad(bool isLive, const char *fname)
{
  time_t              start;
  time_t              end;
  int iNumFrames = -1;
  int iNewNumFrames = -1;
  do
  {
    if( isLive )
      sleep(300);
    noadData *pdata = new noadData();
    pdata->initBuffer();
    start = time(NULL);
    syslog(LOG_INFO, "%s start noad%s for %s ", myTime(start), getVersion(), fname);
    fprintf(stderr,"%s start noad%s for %s\n", myTime(start), getVersion(), fname);
    iNumFrames = iNewNumFrames;
    iNewNumFrames = doX11Scan(pdata, fname, iNumFrames);
    if( cctrl != NULL )
      delete cctrl;
    cctrl = NULL;
    delete pdata;
    end = time(NULL);
    fprintf(stderr,"%s noad done for %s (%ld secs)\n", myTime(end), fname,end-start);
    syslog(LOG_INFO, "%s noad done for %s (%ld secs)", myTime(end), fname,end-start);
  }while(isLive == true && iNewNumFrames > iNumFrames );
}

void writeStatistic(const char *statfilename, const char *recording)
{
  FILE *f = fopen(statfilename, "a+");
  if (!f)
  {
    LOG_ERROR_STR(statfilename);
    return;
  }
  time_t start = time(NULL);
  fprintf(f,"%s;%d (%s);%d;%d;%d;%d;%d;%d;%s;%s\n",
      myTime(start),
      totalFrames,
      IndexToHMSF(totalFrames,false),
      totalDecodedFrames,
      decodedFramesForLogoDetection,
      secsForLogoDetection,
      decodedFramesForLogoCheck,
      secsForLogoCheck,
      secsForScan,
      recording,
      "add your comment here");
  fclose(f);
}

int main(int argc, char ** argv)
{
  syslog(LOG_INFO, "noad args: %s %s %s", argv[0], argv[1], argv[2]);


  int c;
  bool bImmediateCall = false;
  bool bAfter = false;
  bool bBefore = false;
  bool bNice = false;
  bool bFork = false;
  int verbosity = 1;
  const char *recDir = NULL;
  const char *logoCacheDir = NULL;
  const char *statFile = NULL;

  while (1)
  {
    int option_index = 0;
    static struct option long_options[] =
    {
      {"statisticfile", 1, 0, 's'},
      {"logocachedir", 1, 0, 'l'},
      {"verbose", 0, 0, 'v'},
      {"background", 0, 0, 'b'},
      {0, 0, 0, 0}
    };

    c = getopt_long  (argc, argv, "s:l:v",
      long_options, &option_index);
    if (c == -1)
    break;

    switch (c)
    {
      case 's':
        statFile = optarg;
      break;

      case 'l':
        logoCacheDir = optarg;
      break;

      case 'v':
        verbosity++;
      break;

      case 'b':
        bFork = true;
      break;

      case '?':
        printf("unknow option ?\n");
      break;

      default:
        printf ("?? getopt returned character code 0%o ??\n", c);
    }
  }

  if (optind < argc)
  {
    while (optind < argc)
    {
      if(strcmp(argv[optind], "after" ) == 0 )
      {
        bAfter = bFork = bNice = true;
      }
      else if(strcmp(argv[optind], "before" ) == 0 )
      {
        bBefore = true;
      }
      else if(strcmp(argv[optind], "nice" ) == 0 )
        bNice = true;
      else if(strcmp(argv[optind], "-" ) == 0 )
        bImmediateCall = true;
      else
      {
        if( strstr(argv[optind],".rec") != NULL )
          recDir = argv[optind];
      }
      optind++;
    }
  }

  // we can run, if one of bImmediateCall, bAfter, bBefore or bNice is true
  // and recDir is given
  if( (bImmediateCall || bAfter || bBefore || bNice) && recDir )
  {
    // do nothing if called from vdr before the recording has startet
    if( bBefore )
      return 0;

    // if bFork is given go in background
    if( bFork )
    {
      pid_t pid = fork();
      if (pid < 0)
      {
        fprintf(stderr, "%m\n");
        esyslog(LOG_ERR, "ERROR: %m");
        return 2;
      }
      if (pid != 0)
        return 0; // initial program immediately returns
    }

    // should we renice ?
    if( bNice )
      nice(20);

    // set the log-Level
    SysLogLevel = verbosity;

    // now do the work..,
    doNoad(false, recDir);

    // write statistic
    if( statFile != NULL )
       writeStatistic(statFile,recDir);

    return 0;
  }

  // nothing done, give the user some help
  printf("Usage: noad [options] cmd <record>\n"
         "options:\n"
         //"-s <filename>,  --statisticfile=<file> filename where some statistic datas are stored\n"
         //"-l <directory>, --logocachedir=<directory>\n"
         "-v,             --verbose    increments loglevel by one, can be given mutiple\n"
         "-b,             --background noad runs as a background-process\n"
         "\ncmd: one of\n"
         "before                       from vdr if used in the -r option of vdr\n"
         "                             noad exits immediatly if called with \"before\"\n"
         "after                        from vdr if used in the -r option of vdr\n"
         "-                            dummy-parameter if called directly\n"
         "nice                         runs noad with nice(20)\n"
         "\n<record>                     is the name of the directory where the recording\n"
         "                             is stored\n\n"
         );

}


