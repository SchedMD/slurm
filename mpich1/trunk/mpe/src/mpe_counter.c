/**\ --MPE_Log--
*  * mpe_log.c - the externally callable functions in MPE_Log
*  *
*  * MPE_Log currently represents some code written by Dr. William
*  * Gropp, stolen from Chameleon's 'blog' logging package and
*  * modified by Ed Karrels, as well as some fresh code written
*  * by Ed Karrels.
*  *
*  * All work funded by Argonne National Laboratory
\**/
#include "mpeconf.h"

#include <stdio.h>
#include "mpi.h"

/* tag values */
#define REQUEST 0
#define GOAWAY  1
#define VALUE   2

/*@
    MPE_Counter_create - create and initialize shared counter (process)

    Input Parameter:
.   oldcomm - Communicator to 

    Output Parameters:
+   smaller_comm - 
-   counter_comm - Duplicate of 'oldcomm'

@*/
int MPE_Counter_create( oldcomm, smaller_comm, counter_comm )
MPI_Comm  oldcomm,  *smaller_comm,  *counter_comm;
{
    int counter = 0;
    int message, done = 0, myid, numprocs, server, ranks[1];
    MPI_Status status;
    MPI_Group oldgroup, smaller_group;

    MPI_Comm_size(oldcomm, &numprocs);
    MPI_Comm_rank(oldcomm, &myid);
    server = numprocs-1;     /*   last proc is server */
    MPI_Comm_dup( oldcomm, counter_comm ); /* make one new comm */
    MPI_Comm_group( oldcomm, &oldgroup );
    ranks[0] = server;
    MPI_Group_excl( oldgroup, 1, ranks, &smaller_group );
    MPI_Comm_create( oldcomm, smaller_group, smaller_comm );
    MPI_Group_free(&smaller_group);

    if (myid == server) {       /* I am the server */
        while (!done) {
            MPI_Recv(&message, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG,
                     *counter_comm, &status ); 
            if (status.MPI_TAG == REQUEST) {
                MPI_Send(&counter, 1, MPI_INT, status.MPI_SOURCE, VALUE,
                         *counter_comm );
                counter++;
            }
            else if (status.MPI_TAG == GOAWAY) {
                done = 1;
	    }
            else
                fprintf(stderr, "bad tag sent to MPE counter\n");
        }
        MPE_Counter_free( smaller_comm, counter_comm );
    }
    return 0;
}
        
/*@
    MPE_Counter_free - free communicators associated with counter
@*/
int MPE_Counter_free( smaller_comm, counter_comm )      
MPI_Comm *smaller_comm;
MPI_Comm *counter_comm;
{
    int myid, numprocs;

    MPI_Comm_rank( *counter_comm, &myid );
    MPI_Comm_size( *counter_comm, &numprocs );
    if (myid == 0)
        MPI_Send(NULL, 0, MPI_INT, numprocs-1, GOAWAY, *counter_comm);
    MPI_Comm_free( counter_comm );
    if (smaller_comm && *smaller_comm) 
	MPI_Comm_free( smaller_comm );
    return 0;
}

/*@
    MPE_Counter_nxtval - obtain next value from shared counter, and update
@*/
int MPE_Counter_nxtval(counter_comm, value)
MPI_Comm counter_comm;
int *value;
{
    int server,numprocs, myid;
    MPI_Status status;

    MPI_Comm_size( counter_comm, &numprocs );
    MPI_Comm_rank( counter_comm, &myid);
    server = numprocs-1; 
    MPI_Send(NULL, 0, MPI_INT, server, REQUEST, counter_comm );
    MPI_Recv(value, 1, MPI_INT, server, VALUE, counter_comm, &status );
    /* fprintf(stderr,"requestor %d received %d\n", myid, *value); */
    return MPE_SUCCESS;
}
