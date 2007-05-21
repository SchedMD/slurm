#include "mpi.h"
#include <stdlib.h>
#include <stdio.h>
#include "test.h" 

/* Prototypes for picky compilers */
void SetData ( double *, double *, int, int, int, int, int, int );
int CheckData ( double *, int, int, int, int, int );
/* 
   This is an example of using scatterv to send a matrix from one
   process to all others, with the matrix stored in Fortran order.
   Note the use of an explicit UB to enable the sources to overlap.

   This tests scatterv to make sure that it uses the datatype size
   and extent correctly.  It requires number of processors that
   can be split with MPI_Dims_create.

 */

void SetData( sendbuf, recvbuf, nx, ny, myrow, mycol, nrow, ncol )
double *sendbuf, *recvbuf;
int    nx, ny, myrow, mycol, nrow, ncol;
{
int coldim, i, j, m, k;
double *p;

if (myrow == 0 && mycol == 0) {
    coldim = nx * nrow;
    for (j=0; j<ncol; j++) 
	for (i=0; i<nrow; i++) {
	    p = sendbuf + i * nx + j * (ny * coldim);
	    for (m=0; m<ny; m++) {
		for (k=0; k<nx; k++) {
		    p[k] = 1000 * j + 100 * i + m * nx + k;
		    }
		p += coldim;
		}
	    }
    }
for (i=0; i<nx*ny; i++) 
    recvbuf[i] = -1.0;
}

int CheckData( recvbuf, nx, ny, myrow, mycol, nrow )
double *recvbuf;
int    nx, ny, myrow, mycol, nrow;
{
int coldim, m, k;
double *p, val;
int errs = 0;

coldim = nx;
p      = recvbuf;
for (m=0; m<ny; m++) {
    for (k=0; k<nx; k++) {
	val = 1000 * mycol + 100 * myrow + m * nx + k;
	if (p[k] != val) {
	    errs++;
	    if (errs < 10) {
		printf( 
		   "Error in (%d,%d) [%d,%d] location, got %f expected %f\n", 
		        m, k, myrow, mycol, p[k], val );
		}
	    else if (errs == 10) {
		printf( "Too many errors; suppressing printing\n" );
		}
	    }
	}
    p += coldim;
    }
return errs;
}

int main( int argc, char **argv )
{
    int rank, size, myrow, mycol, nx, ny, stride, cnt, i, j, errs, tot_errs;
    double    *sendbuf, *recvbuf;
    MPI_Datatype vec, block, types[2];
    MPI_Aint displs[2];
    int      *scdispls;
    int      blens[2];
    MPI_Comm comm2d;
    int dims[2], periods[2], coords[2], lcoords[2];
    int *sendcounts;
	

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    /* Get a 2-d decomposition of the processes */
    dims[0] = 0; dims[1] = 0;
    MPI_Dims_create( size, 2, dims );
    periods[0] = 0; periods[1] = 0;
    MPI_Cart_create( MPI_COMM_WORLD, 2, dims, periods, 0, &comm2d );
    MPI_Cart_get( comm2d, 2, dims, periods, coords );
    myrow = coords[0];
    mycol = coords[1];
    if (rank == 0) 
	printf( "Decomposition is [%d x %d]\n", dims[0], dims[1] );

    /* Get the size of the matrix */
    nx = 10;
    ny = 8;
    stride = nx * dims[0];

    recvbuf = (double *)malloc( nx * ny * sizeof(double) );
    if (!recvbuf) {
	MPI_Abort( MPI_COMM_WORLD, 1 );
	}
    sendbuf = 0;
    if (myrow == 0 && mycol == 0) {
	sendbuf = (double *)malloc( nx * ny * size * sizeof(double) );
	if (!sendbuf) {
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	    }
	}
    sendcounts = (int *) malloc( size * sizeof(int) );
    scdispls   = (int *)malloc( size * sizeof(int) );

    MPI_Type_vector( ny, nx, stride, MPI_DOUBLE, &vec );
    blens[0]  = 1;   blens[1] = 1;
    types[0]  = vec; types[1] = MPI_UB;
    displs[0] = 0;   displs[1] = nx * sizeof(double);
    
    MPI_Type_struct( 2, blens, displs, types, &block );
    MPI_Type_free( &vec );
    MPI_Type_commit( &block );

    /* Set up the transfer */
    cnt	    = 0;
    for (i=0; i<dims[1]; i++) {
	for (j=0; j<dims[0]; j++) {
	    sendcounts[cnt] = 1;
	    /* Using Cart_coords makes sure that ranks (used by
	       sendrecv) matches the cartesian coordinates (used to
	       set data in the matrix) */
	    MPI_Cart_coords( comm2d, cnt, 2, lcoords );
	    scdispls[cnt++] = lcoords[0] + lcoords[1] * (dims[0] * ny);
	    }
	}

    SetData( sendbuf, recvbuf, nx, ny, myrow, mycol, dims[0], dims[1] );
    MPI_Scatterv( sendbuf, sendcounts, scdispls, block, 
		  recvbuf, nx * ny, MPI_DOUBLE, 0, comm2d );
    if((errs = CheckData( recvbuf, nx, ny, myrow, mycol, dims[0] ))) {
	fprintf( stdout, "Failed to transfer data\n" );
	}
    MPI_Allreduce( &errs, &tot_errs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (rank == 0) {
	if (tot_errs == 0)
	    printf( "No errors\n" );
	else
	    printf( "%d errors in use of MPI_SCATTERV\n", tot_errs );
	}
	
    if (sendbuf) free( sendbuf );
    free( recvbuf );
    free( sendcounts );
    free( scdispls );
    MPI_Type_free( &block );
    MPI_Comm_free( &comm2d );
    MPI_Finalize();
    return errs;
}


