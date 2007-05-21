#include "mpi.h"
#include <stdio.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/*
   Check pack/unpack of mixed datatypes.
 */
#define BUF_SIZE 100
int main( int argc, char **argv )
{
    int myrank;
    char buffer[BUF_SIZE];
    int n, size, src, dest, errcnt, errs;
    double a,b;
    int pos;

    MPI_Status status;
    MPI_Init(&argc, &argv);
    
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

    src	   = 0;
    dest   = 1;

    src	   = 1;
    dest   = 0;

    errcnt = 0;
    if (myrank == src)
	{
	    pos	= 0;
	    n	= 10;
	    a	= 1.1;
	    b	= 2.2;
	    MPI_Pack(&n, 1, MPI_INT, buffer, BUF_SIZE, &pos, MPI_COMM_WORLD);
	    MPI_Pack(&a, 1, MPI_DOUBLE, buffer, BUF_SIZE, &pos, 
		     MPI_COMM_WORLD);
	    MPI_Pack(&b, 1, MPI_DOUBLE, buffer, BUF_SIZE, &pos, 
		     MPI_COMM_WORLD);
	    /* printf( "%d\n", pos ); */
	    MPI_Send(&pos, 1, MPI_INT, dest, 999, MPI_COMM_WORLD);
	    MPI_Send(buffer, pos, MPI_PACKED, dest, 99, MPI_COMM_WORLD);
	}
    else
	{
	    MPI_Recv(&size, 1, MPI_INT, src, 999, MPI_COMM_WORLD, &status);
	    MPI_Recv(buffer, size, MPI_PACKED, src, 99, 
		     MPI_COMM_WORLD, &status);
	    pos = 0;
	    MPI_Unpack(buffer, size, &pos, &n, 1, MPI_INT, MPI_COMM_WORLD);
	    MPI_Unpack(buffer, size, &pos, &a, 1, MPI_DOUBLE, MPI_COMM_WORLD);
	    MPI_Unpack(buffer, size, &pos, &b, 1, MPI_DOUBLE, MPI_COMM_WORLD);
	    /* Check results */
	    if (n != 10) { 
		errcnt++;
		printf( "Wrong value for n; got %d expected %d\n", n, 10 );
		}
	    if (a != 1.1) { 
		errcnt++;
		printf( "Wrong value for a; got %f expected %f\n", a, 1.1 );
		}
	    if (b != 2.2) { 
		errcnt++;
		printf( "Wrong value for b; got %f expected %f\n", b, 2.2 );
		}
	}
    MPI_Allreduce( &errcnt, &errs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (myrank == 0) {
	if (errs == 0) printf( "No errors\n" );
	else           printf( "%d errors\n", errs );
	}
    MPI_Finalize();
return 0;
}
