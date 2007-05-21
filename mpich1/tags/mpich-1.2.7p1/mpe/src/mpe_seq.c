#include "mpeconf.h"
#include "mpi.h"
#include "mpe.h"
#ifndef NULL
#define NULL (void *)0
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
extern void *malloc();
#endif

static int MPE_Seq_keyval = MPI_KEYVAL_INVALID;

static int MPE_Seq_del_fn( MPI_Comm comm, int keyval, void *attribute, 
			   void *extra_state )
{
    MPI_Comm_free( (MPI_Comm *)attribute );
    free( attribute );
    return 0;
}
/*@
   MPE_Seq_begin - Begins a sequential section of code.  

   Input Parameters:
+  comm - Communicator to sequentialize.  
-  ng   - Number in group.  This many processes are allowed to execute
   at the same time.  Usually one.  

   Notes:
   'MPE_Seq_begin' and 'MPE_Seq_end' provide a way to force a section of code 
   to
   be executed by the processes in rank order.  Typically, this is done 
   with
.vb
  MPE_Seq_begin( comm, 1 );
  <code to be executed sequentially>
  MPE_Seq_end( comm, 1 );
.ve$
   Often, the sequential code contains output statements (e.g., 'printf') to
   be executed.  Note that you may need to flush the I/O buffers before
   calling 'MPE_Seq_end'; also note that some systems do not propagate I/O in 
   any
   order to the controling terminal (in other words, even if you flush the
   output, you may not get the data in the order that you want).
@*/
void MPE_Seq_begin( MPI_Comm comm, int ng )
{
    int        lidx, np;
    int        flag;
    MPI_Comm   *local_comm;
    MPI_Status status;

    /* Get the private communicator for the sequential operations */
    if (MPE_Seq_keyval == MPI_KEYVAL_INVALID) {
	MPI_Keyval_create( MPI_NULL_COPY_FN, MPE_Seq_del_fn,
			   &MPE_Seq_keyval, NULL );
    }
    MPI_Attr_get( comm, MPE_Seq_keyval, &local_comm, &flag );
    if (!flag) {
	/* We must allocate private storage for our value */
	local_comm = (MPI_Comm *)malloc( sizeof(MPI_Comm) );
	MPI_Comm_dup( comm, local_comm );
	MPI_Attr_put( comm, MPE_Seq_keyval, local_comm );
    }
    MPI_Comm_rank( comm, &lidx );
    MPI_Comm_size( comm, &np );
    if (lidx != 0) {
	MPI_Recv( NULL, 0, MPI_INT, lidx-1, 0, *local_comm, &status );
    }
    /* Send to the next process in the group unless we are the last process 
       in the processor set */
    if ( (lidx % ng) < ng - 1 && lidx != np - 1) {
	MPI_Send( NULL, 0, MPI_INT, lidx + 1, 0, *local_comm );
    }
}

/*@
   MPE_Seq_end - Ends a sequential section of code.

   Input Parameters:
+  comm - Communicator to sequentialize.  
-  ng   - Number in group.  This many processes are allowed to execute
   at the same time.  Usually one.  

   Notes:
   See 'MPE_Seq_begin' for more details.
@*/
void MPE_Seq_end( MPI_Comm comm, int ng )
{
    int        lidx, np, flag;
    MPI_Status status;
    MPI_Comm   *local_comm;

    MPI_Comm_rank( comm, &lidx );
    MPI_Comm_size( comm, &np );
    MPI_Attr_get( comm, MPE_Seq_keyval, &local_comm, &flag );
    if (!flag) 
	MPI_Abort( comm, MPI_ERR_UNKNOWN );
    /* Send to the first process in the next group OR to the first process
       in the processor set */
    if ( (lidx % ng) == ng - 1 || lidx == np - 1) {
	MPI_Send( NULL, 0, MPI_INT, (lidx + 1) % np, 0, *local_comm );
    }
    if (lidx == 0) {
	MPI_Recv( NULL, 0, MPI_INT, np-1, 0, *local_comm, &status );
    }
}


