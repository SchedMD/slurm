/* mpe_log.c */
/* Custom Fortran interface file */
/* These have been edited because they require special string processing */
#ifndef DEBUG_ALL
#define DEBUG_ALL
#endif
#include <stdio.h>
#include "mpeconf.h"
#include "mpe.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
extern char *malloc();
extern void free();
#endif

#if defined(HAVE_STRING_H) || defined(STDC_HEADERS)
#include <string.h>
#endif

/* This is needed to process Cray - style character data */
#if defined(MPI_CRAY) || defined(_CRAY)
#include <fortran.h>
#endif

#ifdef F77_NAME_UPPER
#define mpe_init_log_ MPE_INIT_LOG
#define mpe_start_log_ MPE_START_LOG
#define mpe_stop_log_ MPE_STOP_LOG
#define mpe_log_get_event_number_ MPE_LOG_GET_EVENT_NUMBER
#define mpe_describe_state_ MPE_DESCRIBE_STATE
#define mpe_describe_event_ MPE_DESCRIBE_EVENT
#define mpe_log_event_ MPE_LOG_EVENT
#define mpe_log_send_ MPE_LOG_SEND
#define mpe_log_receive_ MPE_LOG_RECEIVE
#define mpe_finish_log_ MPE_FINISH_LOG
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_init_log_ mpe_init_log__
#define mpe_start_log_ mpe_start_log__
#define mpe_stop_log_ mpe_stop_log__
#define mpe_log_get_event_number_ mpe_log_get_event_number__
#define mpe_describe_state_ mpe_describe_state__
#define mpe_describe_event_ mpe_describe_event__
#define mpe_log_event_ mpe_log_event__
#define mpe_log_send_ mpe_log_send__
#define mpe_log_receive_ mpe_log_receive__
#define mpe_finish_log_ mpe_finish_log__
#elif defined(F77_NAME_LOWER)
#define mpe_init_log_ mpe_init_log
#define mpe_start_log_ mpe_start_log
#define mpe_stop_log_ mpe_stop_log
#define mpe_log_get_event_number_ mpe_log_get_event_number
#define mpe_describe_state_ mpe_describe_state
#define mpe_describe_event_ mpe_describe_event
#define mpe_log_event_ mpe_log_event
#define mpe_log_send_ mpe_log_send
#define mpe_log_receive_ mpe_log_receive
#define mpe_finish_log_ mpe_finish_log
#endif

/* 
 * In order to suppress warnings about missing prototypes, we've added
 * them to this file.
 */

/* 
   This function makes a copy of a Fortran string into a C string.  Some
   Unix Fortran compilers add nulls at the ends of string CONSTANTS, but
   (a) not for substring expressions and (b) not all compilers do so (e.g.,
   RS6000)
 */

static char *mpe_tmp_cpy ( char *, int );
static char *mpe_tmp_cpy( s, d )
char *s;
int  d;
{
    char *p;
    p = (char *)malloc( d + 1 );
    if (!p) ;
    strncpy( p, s, d );
    p[d] = 0;
    return p;
}

int mpe_init_log_ ( void );
int  mpe_init_log_( void )
{
    return MPE_Init_log();
}

int mpe_start_log_ ( void );
int  mpe_start_log_( void )
{
    return MPE_Start_log();
}

int mpe_stop_log_ ( void );
int  mpe_stop_log_( void )
{
    return MPE_Stop_log();
}

int mpe_log_get_event_number_ ( void );
int mpe_log_get_event_number_( void )
{
    return MPE_Log_get_event_number();
}

int mpe_log_send_( int*, int*, int* );
int mpe_log_send_( int *otherParty, int *tag, int *size )
{
    return MPE_Log_send( *otherParty, *tag, *size );
}

int mpe_log_receive_( int*, int*, int* );
int mpe_log_receive_( int *otherParty, int *tag, int *size )
{
    return MPE_Log_receive( *otherParty, *tag, *size );
}

#ifdef MPI_CRAY
int  mpe_describe_state_( start, end, name, color )
int *start, *end;
_fcd name, color;
{
    char *c1, *c2;
    int  err;
    c1 = mpe_tmp_cpy( _fcdtocp( name ), _fcdlen( name ) );
    c2 = mpe_tmp_cpy( _fcdtocp( color ), _fcdlen( color ) );
    err = MPE_Describe_state(*start,*end,c1, c2);
    free( c1 );
    free( c2 );
    return err;
}
#else
int mpe_describe_state_ ( int *, int *, char *, char *, int, int );
int  mpe_describe_state_( start, end, name, color, d1, d2 )
int *start, *end;
char *name, *color;
int  d1, d2;
{
    char *c1, *c2;
    int  err;
    c1 = mpe_tmp_cpy( name, d1 );
    c2 = mpe_tmp_cpy( color, d2 );
    err = MPE_Describe_state(*start,*end,c1, c2);
    free( c1 );
    free( c2 );
    return err;
}
#endif

#ifdef MPI_CRAY
int mpe_describe_event_( event, name )
int *event;
_fcd name;
{
    char *c1;
    int  err;
    c1 = mpe_tmp_cpy( _fcdtocp( name ), _fcdlen( name ) );
    err = MPE_Describe_event(*event,c1);
    free( c1 );
    return err;
}
#else
int mpe_describe_event_ ( int *, char *, int );
int  mpe_describe_event_( event, name, d1)
int *event;
char *name;
int  d1;
{
    char *c1;
    int  err;
    c1 = mpe_tmp_cpy( name, d1 );
    err = MPE_Describe_event(*event,c1);
    free( c1 );
    return err;
}
#endif

#ifdef MPI_CRAY
int  mpe_log_event_(event,data,string)
int *event, *data;
_fcd string;
{
    char *c1;
    int  err;
    c1 = mpe_tmp_cpy( _fcdtocp( string ), _fcdlen( string ) );
    err = MPE_Log_event(*event,*data,c1);
    free( c1 );
    return err;
}
#else
int mpe_log_event_ ( int *, int *, char *, int );
int  mpe_log_event_(event,data,string, d1)
int *event, *data;
char *string;
int  d1;
{
    char *c1;
    int  err;
    c1 = mpe_tmp_cpy( string, d1 );
    err = MPE_Log_event(*event,*data,c1);
    free( c1 );
    return err;
}
#endif

#ifdef MPI_CRAY
int  mpe_finish_log_( filename)
_fcd filename;
{
    char *c1;
    int  err;
    c1 = mpe_tmp_cpy( _fcdtocp( filename ), _fcdlen( filename ) );
    err =  MPE_Finish_log(c1);
    free( c1 );
    return err;
}
#else
int mpe_finish_log_ ( char *, int );
int  mpe_finish_log_( filename, d1)
char *filename;
int  d1;
{
    char *c1;
    int  err;
    c1 = mpe_tmp_cpy( filename, d1 );
    err =  MPE_Finish_log(c1);
    free( c1 );
    return err;
}
#endif
