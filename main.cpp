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
void doNoad(bool isLive, char *fname)
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
    dsyslog(LOG_INFO, "%s start noad for %s ", myTime(start), fname);
    fprintf(stderr,"%s start noad for %s\n", myTime(start), fname);
    iNumFrames = iNewNumFrames;
    iNewNumFrames = doX11Scan(pdata, fname, iNumFrames);
    if( cctrl != NULL )
      delete cctrl;
    cctrl = NULL;
    delete pdata;
    end = time(NULL);
    fprintf(stderr,"%s noad done for %s (%ld secs)\n", myTime(end), fname,end-start);
    dsyslog(LOG_INFO, "%s noad done for %s (%ld secs)", myTime(end), fname,end-start);
  }while(isLive == true && iNewNumFrames > iNumFrames );
}

int main(int argc, char ** argv)
{
  dsyslog(LOG_INFO, "noad args: %s %s %s", argv[0], argv[1], argv[2]);

  if( argc > 2 &&
     (strcmp(argv[1], "after" ) == 0 ||
      /*(strcmp(argv[1], "before" ) == 0 && strstr(argv[2],"@") != NULL )||*/ //not yet!
      strcmp(argv[1], "-" ) == 0 ||
      strcmp(argv[1], "nice" ) == 0 )
    )
  {
    if( strcmp(argv[1], "after" ) == 0 || strcmp(argv[1],"before") == 0)
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

    if( strcmp(argv[1], "after" ) == 0 || strcmp(argv[1],"before") == 0 || strcmp(argv[1], "nice" ) == 0)
      nice(20);
    doNoad(strcmp(argv[1],"before") == 0, argv[2]);
  }
  else
  {
    fprintf( stderr, "usage: noad after <record>\n");
  }
}
