/*
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpid.h"

/* This is a special stand-alone error handler */
#include <stdio.h>

/* #include "mpisys.h" */
#include "queue.h"

/* 
   This calls the user-specified error handler.  If that handler returns,
   we return the error code 
 */
int MPIR_Error( comm_ptr, code, string, file, line )
struct MPIR_COMMUNICATOR *comm_ptr;
int       code, line;
char     *string, *file;
{
  fprintf( stderr, "%d - %s\n", MPID_MyWorldRank, 
          string ? string : "<NO ERROR MESSAGE>" );
  MPID_Abort( comm_ptr, code, (char *)0, (char *)0 );
  return (code);
}
