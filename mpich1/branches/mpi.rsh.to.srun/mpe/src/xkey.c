#include <math.h>
#include "mpeconf.h"
#include "mpetools.h"      /*I "mpetools.h" I*/
#include "basex11.h"    /*I "basex11.h" I*/

#ifdef MPE_NOMPI
#define MPI_MAX_PROCESSOR_NAME 256
#else
#include "mpi.h"
#endif

#define MPE_INTERNAL
#include "mpe.h"        /*I "mpe.h" I*/

int MPE_SetKeyCallback( MPE_XGraph, int, int(*)(MPE_XGraph, XEvent *) );
int MPE_SetKeyCallback( MPE_XGraph graph, int key, int (*routine)(MPE_XGraph, XEvent *) )
/*
MPE_XGraph graph;
int        key;
int        (*routine)();
*/
{
  if (graph->Cookie != MPE_G_COOKIE) {
    fprintf( stderr, "Handle argument is incorrect or corrupted\n" );
    return MPE_ERR_BAD_ARGS;
  }

  graph->input_mask |= KeyPressMask;
  /* Not quite correct.  we want to have a keypress routine that
     calls the given routine for the particular key.
     Might as well have a keypress vector for each keycode 
   */
  graph->event_routine = routine;

return MPE_SUCCESS;  
}

int MPE_ClrKeyCallback( MPE_XGraph, int );
int MPE_ClrKeyCallback( graph, key )
MPE_XGraph graph;
int        key;
{

return MPE_SUCCESS;
}

/* XKeyEvent.keycode */


