#include <stdio.h>
#include "mpi.h"
#include "test.h"
#include <math.h>

/* # define MPI_Wtime PMPI_Wtime */

/*
 * This program tests that if MPI_WTIME_IS_GLOBAL is set, the timer
 * IS in fact global.  We have some suspicions about certain vendor systems
 */

int CheckTime( void );

/*
 * Check time tests that the timers are synchronized 
 */
int CheckTime( void )
{
    int        rank, size, i;
    double     wtick, t1, t2, t3, delta_t;
    int        ntest=20;
    MPI_Status status;
    int        err = 0;
    double     max_diff = 0.0;

    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    if (rank == 0) {
	wtick = MPI_Wtick();
#ifdef DEBUG
	printf( "Wtick is %lf\n", wtick );
#endif
	while (ntest--) {
	    for (i=1; i<size; i++) {
		MPI_Send( MPI_BOTTOM, 0, MPI_INT, i, 0, MPI_COMM_WORLD );
		MPI_Recv( MPI_BOTTOM, 0, MPI_INT, i, 1, MPI_COMM_WORLD, 
			  &status );
		t1 = MPI_Wtime();
		MPI_Send( &t1, 1, MPI_DOUBLE, i, 2, MPI_COMM_WORLD );
		MPI_Recv( &t2, 1, MPI_DOUBLE, i, 3, MPI_COMM_WORLD, &status );
		t3 = MPI_Wtime();
#ifdef DEBUG
		printf( "Process %d(%f) to 0(%f): diff= %f\n", 
			i, 0.5 * (t1 + t3), t2, 0.5*(t1+t3)-t2 );
#endif
		delta_t = fabs( 0.5 * (t1 + t3) - t2 );
		if( delta_t > (t3 - t1 + wtick)) {
		    err++;
		    printf( "Process %d has %f; Process 0 has %f\n",
			    i, t2, 0.5 * (t1 + t3) );
		}
		if (delta_t > max_diff) max_diff = delta_t;
	    }
#ifdef DEBUG	    
	    printf( "delta_t = %lf\n", delta_t );
#endif
	    /* Release all process for the next pass */
	    for (i=1; i<size; i++) {
		MPI_Send( MPI_BOTTOM, 0, MPI_INT, i, 3, MPI_COMM_WORLD );
	    }
	}
    }
    else {
	while (ntest--) {
	    MPI_Recv( MPI_BOTTOM, 0, MPI_INT, 0, 0, MPI_COMM_WORLD, &status );
	    MPI_Send( MPI_BOTTOM, 0, MPI_INT, 0, 1, MPI_COMM_WORLD );
	    /* Insure a symmetric transfer */
	    MPI_Recv( &t1, 1, MPI_DOUBLE, 0, 2, MPI_COMM_WORLD, &status );
	    t2 = MPI_Wtime();
	    MPI_Send( &t2, 1, MPI_DOUBLE, 0, 3, MPI_COMM_WORLD );
	    MPI_Recv( MPI_BOTTOM, 0, MPI_INT, 0, 3, MPI_COMM_WORLD, &status );
	}
    }
    return err;
}

int main( int argc, char **argv )
{
    int    err = 0;
    void *v;
    int  flag;
    int  vval;
    int  rank;
    double t1;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    MPI_Attr_get( MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL, &v, &flag );
#ifdef DEBUG
    if (v) vval = *(int*)v; else vval = 0;
    printf( "WTIME flag = %d; val = %d\n", flag, vval );
#endif
    if (flag) {
	/* Wtime need not be set */
	vval = *(int*)v;
	if (vval < 0 || vval > 1) {
	    err++;
	    fprintf( stderr, "Invalid value for WTIME_IS_GLOBAL (got %d)\n", 
		     vval );
	}
    }
    if (flag && vval) {
	/* Wtime is global is true.  Check it */
#ifdef DEBUG
	printf( "WTIME_IS_GLOBAL\n" );
#endif	
	err += CheckTime();
	
	/* Wait for 10 seconds */
	t1 = MPI_Wtime();
	while (MPI_Wtime() - t1 < 10.0) ;
	
	err += CheckTime();
    }
    if (rank == 0) {
	if (err > 0) {
	    printf( "Errors in MPI_WTIME_IS_GLOBAL\n" );
	}
	else {
	    printf( " No Errors\n" );
	}
    }
    /* The SGI implementation of MPI sometimes fails to flush stdout 
       properly.  This fflush will work around that bug.  */
    /* fflush( stdout ); */
    MPI_Finalize( );
    
    return err;
}
