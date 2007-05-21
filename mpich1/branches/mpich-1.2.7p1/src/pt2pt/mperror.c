/*
 *  $Id: mperror.c,v 1.11 2000/12/19 18:38:01 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
/* Include the prototypes for these service functions */
#include "mpipt2pt.h"

/* Note that some systems define all of these, but because of problems 
   in the header files, don't actually support them.  We've had this
   problem with Solaris systems.

   If you change this test, you *must* change the corresponding test
   int include/mpipt2pt.h!
 */
#if defined(USE_STDARG) 
#if !defined(MPIR_USE_STDARG)
#define MPIR_USE_STDARG
#endif
#include <stdarg.h>

/* Place to put varargs code, which should look something like

   void mpir_errors_are_fatal( MPI_Comm *comm, int *code, ... )
   {
   va_list Argp;

   va_start( Argp, code );
   string = va_arg(Argp,char *);
   file   = va_arg(Argp,char *);
   line   = va_arg(Argp,int *);
   va_end( Argp );
   ... 
   }
 */
#endif

/*
   Fatal error handler.  Prints a message and aborts.
 */
#ifdef MPIR_USE_STDARG
void MPIR_Errors_are_fatal(  MPI_Comm *comm, int * code, ... )
{
  char buf[MPI_MAX_ERROR_STRING];
  int  result_len; 
  char *string, *file;
  int  *line; 
  va_list Argp;
  struct MPIR_COMMUNICATOR *comm_ptr;

#ifdef USE_OLDSTYLE_STDARG
  va_start( Argp );
#else
  va_start( Argp, code );
#endif
  string = va_arg(Argp,char *);
  file   = va_arg(Argp,char *);
  line   = va_arg(Argp,int *);
  va_end( Argp );
#else
void MPIR_Errors_are_fatal( MPI_Comm *comm, int *code, char *string, 
			    char *file, int *line )
{
  char buf[MPI_MAX_ERROR_STRING];
  int  result_len; 
  struct MPIR_COMMUNICATOR *comm_ptr;
#endif

  MPI_Error_string( *code, (char *)buf, &result_len );
  if (result_len == 0) {
      SPRINTF(buf,"No message for error in %s:%d", file, *line );
  }
  FPRINTF( stderr, "%d - %s : %s\n", MPID_MyWorldRank,
          string ? string : "<NO ERROR MESSAGE>", buf );

#ifdef DEBUG_TRACE
  /* Print internal trace from top down */
  TR_stack_print( stderr, -1 );
#endif
#ifdef HAVE_PRINT_BACKTRACE
  MPIR_Print_backtrace( NULL, 1, "Call stack\n" );
#endif

  /* Comm might be null; must NOT invoke error handler from 
     within error handler */
  comm_ptr = MPIR_GET_COMM_PTR(*comm);

  MPID_Abort( comm_ptr, *code, (char *)0, (char *)0 );
}


/*
   Handler ignores errors.
 */   
#ifdef MPIR_USE_STDARG
void MPIR_Errors_return( MPI_Comm *comm, int *code, ... )
{
}
#else
void MPIR_Errors_return(  comm, code, string, file, line )
MPI_Comm *comm;
int      *code, *line;
char     *string, *file;
{
}
#endif

/*
   Handler prints warning messsage and returns.  Internal.  Not
   a part of the standard.
 */
#ifdef MPIR_USE_STDARG
void MPIR_Errors_warn(  MPI_Comm *comm, int *code, ... )
{  
  char buf[MPI_MAX_ERROR_STRING];
  int  myid, result_len; 
  char *string, *file;
  int  *line;
  va_list Argp;

#ifdef USE_OLDSTYLE_STDARG
  va_start( Argp );
#else
  va_start( Argp, code );
#endif
  string = va_arg(Argp,char *);
  file   = va_arg(Argp,char *);
  line   = va_arg(Argp,int *);
  va_end( Argp );
#else
void MPIR_Errors_warn(  comm, code, string, file, line )
MPI_Comm *comm;
int      *code, *line;
char     *string, *file;
{
  char buf[MPI_MAX_ERROR_STRING];
  int  myid, result_len; 
#endif

  myid = MPID_MyWorldRank;
  MPI_Error_string( *code, buf, &result_len );
  if (result_len == 0) {
      SPRINTF(buf,"No message for error in %s:%d", file, *line );
  }
#ifdef MPIR_DEBUG
  /* Generate this information ONLY when debugging MPIR */
  FPRINTF( stderr, "%d -  File: %s   Line: %d\n", myid, 
		   file, *line );
#endif
  FPRINTF( stderr, "%d - %s : %s\n", myid, 
          string ? string : "<NO ERROR MESSAGE>", buf );
}


/* 
   This calls the user-specified error handler.  If that handler returns,
   we return the error code 
 */
int MPIR_Error( 
	struct MPIR_COMMUNICATOR *comm, 
	int code, 
	char *string, 
	char *file, 
	int line )
{
  MPI_Errhandler handler;
  static int InHandler = 0;

  if (InHandler) return code;
  InHandler = 1;

  /* Check for bad conditions */
  if (!comm)
    comm = MPIR_COMM_WORLD;
  /* This can happen if MPI_COMM_WORLD is not initialized */
  if (!comm || (handler = comm->error_handler) == MPI_ERRHANDLER_NULL) 
    handler = MPI_ERRORS_ARE_FATAL;
  if (!handler) {
      /* Fatal error, probably a call before MPI_Init */
      fprintf( stderr, "Fatal error; unknown error handler\n\
May be MPI call before MPI_INIT.  Error message is %s and code is %d\n", 
	      string, code );
      InHandler = 0;
      return code;
      }

  /* If we're calling MPI routines from within an MPI routine, we 
   (probably) just want to return.  If so, we set "use_return_handler" */
  if (comm && comm->use_return_handler) {
      InHandler = 0;
      return code;
  }

  /* Call handler routine */
  {struct MPIR_Errhandler *errhand = MPIR_ToPointer( handler );
  if (!errhand || !errhand->routine) {
      fprintf( stderr, "Fatal error; unknown error handler\n\
May be MPI call before MPI_INIT.  Error message is %s and code is %d\n", 
	      string, code );
      InHandler = 0;
      return code;
  }
  (*errhand->routine)( &comm->self, &code, string, file, &line );
  }

  InHandler = 0;
  return (code);
}

/*
 * The following is a special routine to set the MPI_ERROR fields in
 * an array of statuses when a request fails.  We do NOT attempt to 
 * complete requests once an error is detected; we just use MPI_ERR_PENDING
 * to indicate any incomplete requests.
 */
void MPIR_Set_Status_error_array( 
	MPI_Request array_of_requests[], 
	int count, int i_failed, int err_failed, 
	MPI_Status array_of_statuses[] )
{
    int i;
    MPI_Request request;

    for (i=0; i<count; i++) {
	request = array_of_requests[i];
	if (i == i_failed) array_of_statuses[i].MPI_ERROR = err_failed;
	else if (!request) array_of_statuses[i].MPI_ERROR = MPI_SUCCESS;
	else {
	    switch (request->handle_type) {
	    case MPIR_SEND:
/*
		if (request->shandle.is_complete) 
		    array_of_statuses[i].MPI_ERROR = MPI_SUCCESS;
		else */
		    array_of_statuses[i].MPI_ERROR = MPI_ERR_PENDING;
		break;
	    case MPIR_RECV:
/*		if (request->rhandle.is_complete) 
		    array_of_statuses[i].MPI_ERROR = MPI_SUCCESS;
		else */
		    array_of_statuses[i].MPI_ERROR = MPI_ERR_PENDING;
		break;
	    case MPIR_PERSISTENT_SEND:
		if (!request->persistent_shandle.active /* ||
		    request->persistent_shandle.shandle.is_complete */) 
		    array_of_statuses[i].MPI_ERROR = MPI_SUCCESS;
		else
		    array_of_statuses[i].MPI_ERROR = MPI_ERR_PENDING;
		break;
	    case MPIR_PERSISTENT_RECV:
		if (!request->persistent_rhandle.active /* ||
		    request->persistent_rhandle.rhandle.is_complete */) 
		    array_of_statuses[i].MPI_ERROR = MPI_SUCCESS;
		else
		    array_of_statuses[i].MPI_ERROR = MPI_ERR_PENDING;
		break;
	    }
	}
    }
}
