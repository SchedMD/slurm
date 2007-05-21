
#include <stdio.h>

#include "mpi.h"
#if HAVE_STDLIB_H || STDC_HEADERS
#include <stdlib.h>
#else
#ifdef __STDC__
extern void 	*calloc(/*size_t, size_t*/);
extern void	free(/*void * */);
#else
extern char *malloc();
extern int free();
#endif
#endif

/*
    This program attempts to tune the message packet size for a given
    architecture.  This program works ONLY with the MPICH implementation,
    and then only when configured with -var_pkt
 */
main( argc, argv )
int argc;
char **argv;
{
int    len_small, len_large, len_test, rank;
double time_small, time_large;
extern double RunTest();

MPI_Init( &argc, &argv );
MPI_Comm_rank( MPI_COMM_WORLD, &rank );

len_small = 0;
len_large = MPID_SetPktSize( -1 );
if (MPID_SetPktSize( 0 ) != 0) {
    fprintf( stderr, 
"This version of MPICH does not allow you to change the small packet length\n");
    MPI_Abort( MPI_COMM_WORLD, 1 );
    }
if (rank == 0) {
    printf( "Len\tShort\t\tLong\n" );
    }
while (len_large - len_small > sizeof(long)) {
    len_test = (len_large + len_small) / 2;
    MPID_SetPktSize( len_test );
    time_small = RunTest( len_test );
    MPID_SetPktSize( 0 );
    time_large = RunTest( len_test );
    if (rank == 0) {
	printf( "%d\t%f\t%f\n", len_test, time_small, time_large );
	fflush( stdout );
	}
    if (time_small < time_large) 
	len_small = len_test;
    else if (time_small > time_large)
	len_large = len_test;
    else 
	break;
    }

if (rank == 0)
    printf( "A good value of MPID_PKT_DATA_SIZE is %d\n", len_test );
MPI_Finalize( );
return 0;
}

double RunTest( len )
int len;
{
extern double RunTestSingle();
double t1, tcur;
int    cnt = 15;

tcur = RunTestSingle( len );
while (cnt--) {
    t1 = RunTestSingle( len );
    if (t1 < tcur) tcur = t1;
    }
return tcur;
}

double RunTestSingle( len )
int len;
{
int        rank;
int        cnt, i;
double     t1;
char       *rbuffer;
char       *sbuffer;
MPI_Status status;

rbuffer = (char *)malloc( len );
sbuffer = (char *)malloc( len );
if (!rbuffer || !sbuffer) {
    fprintf( stderr, "Could not allocate buffers of length %d\n", len );
    MPI_Abort( MPI_COMM_WORLD, 0 );
    }
MPI_Comm_rank( MPI_COMM_WORLD, &rank );

cnt = 25;
if (rank == 0) {
    MPI_Recv(rbuffer,len,MPI_BYTE,MPI_ANY_SOURCE,0,MPI_COMM_WORLD,&status);
    t1 = MPI_Wtime();
    for(i=0;i<cnt;i++){
	MPI_Send(sbuffer,len,MPI_BYTE,1,1,MPI_COMM_WORLD);
	MPI_Recv(rbuffer,len,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&status);
	}
    t1 = MPI_Wtime() - t1;
    }
else if (rank == 1) {
    MPI_Send(sbuffer,len,MPI_BYTE,0,0,MPI_COMM_WORLD);
    for(i=0;i<cnt;i++){
	MPI_Recv(rbuffer,len,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&status);
	MPI_Send(sbuffer,len,MPI_BYTE,0,1,MPI_COMM_WORLD);
	}
    }

free( rbuffer );
free( sbuffer );
MPI_Bcast( &t1, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD );
return t1;
}
