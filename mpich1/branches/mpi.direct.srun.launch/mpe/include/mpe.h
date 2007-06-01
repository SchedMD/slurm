/* MPE_Graphics should not be included here in case the system does not
   support the graphics features. */

#ifndef _MPE_INCLUDE
#define _MPE_INCLUDE

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef MPE_GRAPHICS
#include "mpe_graphics.h"
#endif
#include "mpe_log.h"

#define MPE_SUCCESS        0		/* successful operation */
#define MPE_ERR_NOXCONNECT 1		/* could not connect to X server */
#define MPE_ERR_BAD_ARGS   2            /* graphics handle invalid */
#define MPE_ERR_LOW_MEM    3	        /* out of memory (malloc() failed) */

#if defined(F77_NAME_LOWER)

#define mpe_initlog_          mpe_initlog
#define mpe_startlog_         mpe_startlog
#define mpe_stoplog_          mpe_stoplog
#define mpe_describe_state_   mpe_describe_state
#define mpe_describe_event_   mpe_describe_event
#define mpe_log_event_        mpe_log_event
#define mpe_finishlog_        mpe_finishlog

#endif

void MPE_Seq_begin ( MPI_Comm, int );
void MPE_Seq_end   ( MPI_Comm, int );

int MPE_DelTag     ( MPI_Comm, int, void *, void * );
int MPE_GetTags    ( MPI_Comm, int, MPI_Comm *, int * );
int MPE_ReturnTags ( MPI_Comm, int, int );
int MPE_TagsEnd    ( void );

void MPE_IO_Stdout_to_file ( char *, int );

void MPE_GetHostName       ( char *, int );

void MPE_Start_debugger ( void );
#if (defined(__STDC__) || defined(__cplusplus))
void MPE_Errors_to_dbx ( MPI_Comm *, int *, ... );
#else
void MPE_Errors_to_dbx ( MPI_Comm *, int *, char *, char *, int * );
#endif
void MPE_Errors_call_debugger ( char *, char *, char ** );
void MPE_Errors_call_xdbx     ( char *, char * );
void MPE_Errors_call_dbx_in_xterm ( char *, char * );
void MPE_Signals_call_debugger ( void );

int MPE_Decomp1d ( int, int, int, int *, int * );

void MPE_Comm_global_rank ( MPI_Comm, int, int * );

#if defined(__cplusplus)
};
#endif

#endif
