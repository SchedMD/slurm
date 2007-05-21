#ifndef MPDLIB_INCLUDE
#define MPDLIB_INCLUDE

#define MPD_VERSION      2
#define MPD_MAX_PROCESSOR_NAME 128

/* probably ought to keep the next set in sync with mpd.h */
#define MPD_MAXLINE       4096
#define MPD_MAXHOSTNMLEN    64
#define MPD_IDSIZE        (MPD_MAXHOSTNMLEN+8)

#include "mpdconf.h"

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif
#include <ctype.h>
#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/socket.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <sys/un.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <pwd.h>
#include <syslog.h>

/*
char *sys_errlist[];
*/


/***********************************************************
 *	mpdlib.h
 *	Functions callable from MPD Clients
 ***********************************************************/

int MPD_Init( void (*)(char *) );
int MPD_Job( void );
int MPD_Rank( void );
int MPD_Size( void );
int MPD_Peer_listen_fd( void );
int MPD_Poke_peer( int, int, char * );
int MPD_Get_peer_host_and_port( int, int, char *, int * ); 
void MPD_Abort( int );
int MPD_Finalize( void );
int MPD_Man_msgs_fd( void );
int MPD_Test_connections( int *, int * );
int MPD_Request_connect_from_peer( int, int );
void MPD_Man_msg_handler( char * );
void MPD_Set_debug( int );
void MPD_Printf( int, char *, ... ); 

struct mpd_keyval_pairs
{
    char key[32];
    char value[MPD_MAXLINE];	
};

/* from Stevens book */
typedef void Sigfunc( int );

#endif

/*************** prototypes for functions shared with mpd *****************/

int mpd_read_line( int, char *, int );
Sigfunc *mpd_Signal( int , Sigfunc );
int mpd_parse_keyvals( char * );
void mpd_dump_keyvals( void );
char *mpd_getval( char *, char * );
void mpd_chgval( char *, char * );
void mpd_stuff_arg( char *, char * );
void mpd_destuff_arg( char *, char * );
