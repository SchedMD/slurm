/******************** clog2slog.c ****************************/
/*
  This program converts a clog file generated using MPE Logging calls into an 
  slog file.
*/

/*
  a clog file format:
  divided into chunks of 1024 bytes containing a CLOG block.
  the block contains several records of different types.
  a record consists of a header which contains the timestamp, record type and
  process id.
  the headers are the same for all record types but the records themselves are
  very much different. this converter only pays attention to the following 
  record types: 
  CLOG_STATEDEF,
  CLOG_RAWEVENT,
  CLOG_COMMEVENT.
  the rest are ignored. for more information on other record types, please look
  at clog.h in the MPE source.
*/


#include "mpeconf.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#if defined( STDC_HEADERS ) || defined( HAVE_STDLIB_H )
#include <stdlib.h>
#endif
#if defined( HAVE_UNISTD_H )
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT_H
#include "getopt.h"
#endif
#include "clog2slog.h"
#ifdef HAVE_WINDOWS_H
#include <io.h>
#include "getopt.h"
#endif

int main (int argc, char **argv) {

  long frame_size = C2S_FRAME_BYTE_SIZE,   /* default frame kilo-byte size.*/
       num_frames = 0,    /* stores the no of frames per directory in slog.*/
       cal1; 

  int clogfd,             /* file descriptor for reading clog files.       */
      err_chk,            /* error check flag.                             */ 
      num_args = 0;       /* number of options specified on command line.  */

#ifndef HAVE_GETOPT_H
  extern char *optarg;
  /* optind, opterr, and optopt allow access to more information from 
     getopt.  Only optind is used in this code. */
  extern int optind /*, opterr, optopt */;
#endif

  double membuff[CLOG_BLOCK_SIZE/sizeof(double)];
                             /* buffer to read clog records into.*/

  char *clog_file     = NULL,/* name of clog file.*/
       *slog_file     = NULL,/* name of slog file.*/
       *optstring     = "d:f:h",
        optchar;
  
  optchar = getopt(argc,argv,optstring);
  while(strchr(optstring,optchar) != NULL) {
    if(num_args <= 2)
      switch(optchar) {
      case 'd':
	if((optarg != NULL) && (*optarg == '=')) {
	  cal1 = atol(optarg+1);
	  num_frames = cal1;
	}
	num_args++;
	break;
      case 'f':
	if((optarg != NULL) && (*optarg == '=')) {
	  cal1 = atol(optarg+1);
	  frame_size = cal1;
	}
	num_args++;
	break;
      default:
	C2S1_print_help();
	exit(0);
      }
    else {
      C2S1_print_help();
      exit(0);
    }
    optchar = getopt(argc,argv,optstring);
    
  }
  
  /*  checkForBigEndian();  */
  
  
  if((argc-num_args) > 1)
    clog_file = argv[optind];
  else {
    fprintf(stderr, "clog2slog.c:%d: No clog file specified in command line.\n",
	    __LINE__);
    C2S1_print_help();
    exit(1);
  }
 
  if(strstr(clog_file,".clog") == NULL) {
    fprintf(stderr, "clog2slog.c:%d: specified file is not a clog file.\n",
	    __LINE__);
    C2S1_print_help();
    exit(1);
  }
  
  if((clogfd = OPEN(clog_file, O_RDONLY, 0)) == -1) {
    fprintf(stderr,"clog2slog.c:%d: Could not open clog file %s for"
	    " reading.\n",__LINE__,clog_file);
    exit(1);
  }

  if((err_chk = C2S1_init_clog2slog(clog_file, &slog_file)) == C2S_ERROR) 
    exit(1);

  /*
    first pass to initialize the state definitions list using the 
    C2S1_init_state_defs() function.
  */
  while
    ((err_chk = read(clogfd, membuff, CLOG_BLOCK_SIZE)) != -1) {
    if(err_chk != CLOG_BLOCK_SIZE) {
      fprintf(stderr,"clog2slog.c:%d: Unable to read %d bytes.\n",
	      __LINE__,CLOG_BLOCK_SIZE);
      exit(1);
    }
    if((err_chk = C2S1_init_state_defs(membuff)) == CLOG_ENDLOG)
      break;
    else if(err_chk == C2S_ERROR) {
      C2S1_free_state_info();
      exit(1);
    }
  }

  /* initialize slog tables */
  if((err_chk = C2S1_init_SLOG(num_frames,frame_size,slog_file)) == C2S_ERROR) 
    exit(1);
  
  /*
    going back to the beginning of clog file for second pass
  */
  if(lseek(clogfd,(long)0,0) == -1){
    fprintf(stderr,"clog2slog.c:%d: Could not go to top of file\n",__LINE__);
    exit(1);
  }
  /*
    making second pass of clog file to log slog intervals using clog events.
    the function used here is C2S1_make_SLOG()
  */
  while((err_chk = read(clogfd, membuff, CLOG_BLOCK_SIZE)) != -1) {
    if(err_chk != CLOG_BLOCK_SIZE) {
      fprintf(stderr,"clog2slog.c:%d: Unable to read %d bytes.\n",
	      __LINE__,CLOG_BLOCK_SIZE);
      exit(1);
    }

    if((err_chk = C2S1_make_SLOG(membuff)) == CLOG_ENDLOG)
      break;
    else if(err_chk == C2S_ERROR)
      exit(1);
  }
  
 
  close(clogfd);
  C2S1_free_resources();
  return 0;
}

