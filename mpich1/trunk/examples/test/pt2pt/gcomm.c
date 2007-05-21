/* 
    This file generates a few communicators for use in the test suite

    THIS CODE IS FROM mpich/tsuite AND SHOULD BE CHANGED THERE ONLY
 */

#include "mpi.h"

#include "gcomm.h"

void MakeComms( comms, maxn, n, make_intercomm )
MPI_Comm *comms;
int      *n, maxn, make_intercomm;
{
int cnt = 0;
int rank, size;
int dims[2];
int periods[2], range[1][3];
MPI_Group group, newgroup;

MPI_Comm_rank( MPI_COMM_WORLD, &rank );
MPI_Comm_size( MPI_COMM_WORLD, &size );

comms[cnt++] = MPI_COMM_WORLD;
if (cnt == maxn) {*n = cnt; return; }

/* Construct a communicator with the ranks reversed */
MPI_Comm_group( MPI_COMM_WORLD, &group );
range[0][0] = size-1;
range[0][1] = 0;
range[0][2] = -1;
MPI_Group_range_incl( group, 1, range, &newgroup );
MPI_Comm_create( MPI_COMM_WORLD, newgroup, &comms[cnt] );
cnt++;
MPI_Group_free( &group );
MPI_Group_free( &newgroup );
if (cnt == maxn) {*n = cnt; return; }

if (size > 3) {
    /* Divide into odd and even processes */
    MPI_Comm_split( MPI_COMM_WORLD, rank & 0x1, rank, comms + cnt );
    cnt ++;

    /* Use the cartesian constructors */
    dims[0] = 0; dims[1] = 0;
    MPI_Dims_create( size, 2, dims );
    periods[0] = 0; periods[1] = 0;
    MPI_Cart_create( MPI_COMM_WORLD, 2, dims, periods, 0, comms + cnt );
    cnt ++;
    if (cnt == maxn) {*n = cnt; return; }

    /* Create an intercommunicator (point-to-point operations only)
       Note that in this case, codes need to use MPI_Comm_remote_size to
       (added to MPI_Comm_size) to get the size of the full group */
    if (make_intercomm) {
	/* The remote_leader is rank 1 in MPI_COMM_WORLD if we are even
	   and 0 if we are odd (the remote_leader rank is relative to the
	   peer communicator) 
	 */
	MPI_Intercomm_create( comms[2], 0, MPI_COMM_WORLD, !(rank&0x1), 
			      37, comms + cnt );
	cnt ++;
	if (cnt == maxn) {*n = cnt; return; }
	}
    }
*n = cnt;
}

void FreeComms( comms, n )
MPI_Comm *comms;
int      n;
{
int i;
for (i=1; i<n; i++) {
    if (comms[i] != MPI_COMM_NULL) 
	MPI_Comm_free( comms + i );
    }
}
