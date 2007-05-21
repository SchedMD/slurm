/*
 *	Convex SPP
 *	Copyright 1995 Convex Computer Corp.
 *	$CHeader: shmem1stbar.c 1.1 1995/11/08 14:00:00 $
 *
 *	Function:	- memory allocation for collective calls
 */

#include <stdlib.h>
#include "mpi.h"
#include "mpid.h"
#include "shmemfastcoll.h"

/*
 * external functions
 */
extern MPID_Fastbar	*MPID_SHMEM_Alloc_barrier();

/*
 * local variables
 */
static int		mps_db_msgtype = 0;


int
MPID_SHMEM_First_barrier(comm)

MPI_Comm		comm;

{
	int		nproc,node,enode,ncycles;
	int		twok,twok1,k,to,from,c,ns,nr;
	int		rank, ierr;
	int		*lengths;
	char		*ptr;
	MPI_Aint	*addr;
	MPID_Fastbar	*bar;
	MPI_Datatype	stype, rtype;
	MPI_Request	req[2];
	MPI_Status	mpi_status[2];

	MPI_Comm_size(comm, &nproc);

	if (! comm->ADIBarrier) bar = MPID_SHMEM_Alloc_barrier(comm);

	MPI_Comm_rank(comm, &node);
	ncycles = bar->nc;

	twok1 = 1 << (ncycles - 1);

	if (++mps_db_msgtype > 32767) mps_db_msgtype=1;
/*
 * trivial case
 */
	if (nproc <= 1) {
		for (c = 0; c < nproc; ++c){
			ptr = (char *) bar->barf[c].flag;
			bar->barf[c].ival = (int *) (ptr + 4);
			bar->barf[c].addr = (void **) (ptr + 8);
			bar->barf[c].dval = (double *) (ptr + 16);
			bar->barf[c].rval = (float *) (ptr + 20);
		}

		return(MPI_SUCCESS);
	}
/*
 * Do the real setup.
 */
	addr = (MPI_Aint *) malloc(nproc * sizeof(MPI_Aint));
	lengths = (int *) malloc(nproc * sizeof(int));
    
	for (k = 0; k < ncycles; ++k) {
		twok = twok1 + twok1;
/*
 * First do the send.
 */
		ns = 0;
		for (c = 0; c < nproc; ++c) {
			enode = node - c;
			if (enode < 0) enode += nproc;
			if (((enode % twok) == 0) &&
					((enode + twok1) < nproc)) {
				MPI_Address(&(bar->barf[c].flag), &addr[ns]);
				lengths[ns++] = sizeof(MPI_Aint);
			}
		}

		ierr = MPI_Type_hindexed(ns, lengths, addr, MPI_BYTE, &stype);
		if (ierr != MPI_SUCCESS) goto error_exit;

		ierr = MPI_Type_commit(&stype);
		if (ierr != MPI_SUCCESS) goto error_exit;

		to = (node + twok1) % nproc;
		ierr = MPI_Isend(MPI_BOTTOM, 1, stype, to,
					mps_db_msgtype, comm, &req[0]);
		if (ierr != MPI_SUCCESS) goto error_exit;
/*
 * Now do the receive.
 */
		nr = 0;
		for (c = 0; c < nproc; ++c) {
			enode = node - c;
			if (enode < 0) enode += nproc;
			if (((enode % twok) != 0) && ((enode % twok1) == 0)) {
				MPI_Address(&(bar->barf[c].flag), &addr[nr]);
				lengths[nr++] = sizeof(MPI_Aint);
			}
		}

		ierr = MPI_Type_hindexed(nr, lengths, addr, MPI_BYTE, &rtype);
		if (ierr != MPI_SUCCESS) goto error_exit;

		ierr = MPI_Type_commit(&rtype);
		if (ierr != MPI_SUCCESS) goto error_exit;

		from = node - twok1;
		if (from < 0) from += nproc;

		ierr = MPI_Irecv(MPI_BOTTOM, 1, rtype, from,
					mps_db_msgtype, comm, &req[1]);
		if (ierr != MPI_SUCCESS) goto error_exit;
    
		ierr = MPI_Waitall(2, req, mpi_status);
		if (ierr != MPI_SUCCESS) goto error_exit;

		ierr = MPI_Type_free(&stype);
		if (ierr != MPI_SUCCESS) goto error_exit;

		ierr = MPI_Type_free(&rtype);
		if (ierr != MPI_SUCCESS) goto error_exit;
    
		twok1 >>= 1;
	}

	free((char *) addr);
	free((char *) lengths);

	for (c = 0; c < nproc; ++c){
		ptr = (char *) bar->barf[c].flag;
		bar->barf[c].ival = (int *) (ptr + 4);
		bar->barf[c].addr = (void **) (ptr + 8);
		bar->barf[c].dval = (double *) (ptr + 16);
		bar->barf[c].rval = (float *) (ptr + 20);
	}

	return(MPI_SUCCESS);
/*
 * Annoy the purists.
 */
error_exit:
	free((char *) addr);
	free((char *) lengths);
	return(ierr);
}
