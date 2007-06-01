/*
 * This is an experimental program to evaluate a 
 * cluster of machines.  The approach is this
 *
 * For each PAIR, time a relatively long message.  Do the test
 * often enough to get a .5 second run (actually, GOAL_SEC).
 * In order to ensure that there is no contention caused by this
 * test, only one pair at a time runs.  To handle transient events that
 * may distort the results, the tests are run 5 times and the best result
 * is used.
 *
 * Create a graph of all pairs.  Find the highest achieved rate.  For
 * each processor, attach it to a "super" group consisting of all 
 * processors with nearly (TOLERANCE) the same speed.  Repeat until
 * all processors are in groups (which may be of size 1).
 */
#include <mpi.h>
#include <stdio.h>
#include <memory.h>

/* Tag values */
#define YOUR_TURN 1
#define RATE_VAL  2
#define DATA_VAL  3
#define TIME_VAL  4
#define NAME_VAL  5

/* Run times and tolerances */
#define GOAL_SEC  0.5
#define TOLERANCE 0.3
#define MSG_LEN   65536

double GetRate();
void   FindClusters();

int main( argc, argv )
int  argc;
char **argv;
{
int world_rank, size;
int i, j;
double rate;
MPI_Status status;
double *ratematrix;
char   myname[MPI_MAX_PROCESSOR_NAME];
char   **sysnames;
int    resultlen;
MPI_Init( &argc, &argv );
MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );
MPI_Comm_size( MPI_COMM_WORLD, &size );

/* 
   Gather the data.  Rank 0 tells everyone what to do, then interacts with
   everyone last.
 */
if (world_rank == 0) {
    ratematrix = (double *)malloc( size * size * sizeof(double) );
    if (!ratematrix) {
	fprintf( stderr, "Can not allocate rate matrix for %d procs\n",
		 size );
	MPI_Abort( MPI_COMM_WORLD, 1 );
	}
    /* Get the names of all of the systems */
    sysnames = (char **)malloc( size * sizeof(char *) );
    if (!sysnames) {
	fprintf( stderr, "Can not allocate name matrix for %d procs\n", size );
	MPI_Abort( MPI_COMM_WORLD, 1 );
	}
    sysnames[0] = myname;
    MPI_Get_processor_name( myname, &resultlen );
    for (i=1; i<size; i++) {
	sysnames[i] = (char *)malloc( MPI_MAX_PROCESSOR_NAME * sizeof(char) );
	MPI_Recv( sysnames[i], MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, 
		  NAME_VAL, MPI_COMM_WORLD, &status );
	}
	
    for (i=1; i<size; i++) {
	for (j=i+1; j<size; j++) {
	    MPI_Ssend( &j, 1, MPI_INT, i, YOUR_TURN, MPI_COMM_WORLD );
	    MPI_Ssend( &i, 1, MPI_INT, j, YOUR_TURN, MPI_COMM_WORLD );
	    MPI_Recv( &ratematrix[i+j*size], 1, MPI_DOUBLE, 
		      i, RATE_VAL, MPI_COMM_WORLD, &status );
	    ratematrix[j+i*size] = ratematrix[i+j*size];
	    printf( "." ); fflush( stdout );
	    }
	MPI_Ssend( &world_rank, 1, MPI_INT, i, YOUR_TURN, MPI_COMM_WORLD );
	ratematrix[i*size] = GetRate( i, MPI_COMM_WORLD );
	ratematrix[i]      = ratematrix[i*size];
	printf( "." ); fflush( stdout );
	}
    printf( "\n" ); fflush( stdout );
    }
else {
    MPI_Get_processor_name( myname, &resultlen );
    MPI_Ssend( myname, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, 0, NAME_VAL, 
	       MPI_COMM_WORLD );
    /* Keep handling requests until we do the master; that is the last one */
    do { 
	MPI_Recv( &j, 1, MPI_INT, 0, YOUR_TURN, MPI_COMM_WORLD, &status );
	rate = GetRate( j, MPI_COMM_WORLD );
	if (j > world_rank) {
	    MPI_Send( &rate, 1, MPI_DOUBLE, 0, RATE_VAL, MPI_COMM_WORLD );
	    }
	} while (j != 0);
    }

/* Produce the final report */
if (world_rank == 0) {
    FindClusters( ratematrix, size, sysnames );
    free( ratematrix );
    for (i=1; i<size; i++) 
	free( sysnames[i] );
    free( sysnames );
    }
MPI_Finalize( );
return 0;
}

/* Get an estimate for the communication rate to partner */
double GetRate( partner, comm )
int      partner;
MPI_Comm comm;
{
double     rate = 0;
double     tstart, t, rtest;
int        len = MSG_LEN;
int        *sbuf, *rbuf;
int        myrank;
int        i, cnt, testnum;
MPI_Status status;

MPI_Comm_rank( comm, &myrank );
/* Get the buffers */
sbuf = (int *)malloc( len * sizeof(int) );
rbuf = (int *)malloc( len * sizeof(int) );
if (!sbuf || !rbuf) {
    fprintf( stderr, "Could not allocate buffers of size %d\n", len );
    MPI_Abort( MPI_COMM_WORLD, 2 );
    }
/* Get the "we're both ready" message */
MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, DATA_VAL, 
	      MPI_BOTTOM, 0, MPI_INT, partner, DATA_VAL, comm, &status );

/* Estimate the number of times to run the test.  */
cnt = 1;
do {
    tstart = MPI_Wtime();
    for (i=0; i<cnt; i++) {
	MPI_Sendrecv( sbuf, len, MPI_INT, partner, DATA_VAL, 
		     rbuf, len, MPI_INT, partner, DATA_VAL, comm, &status );
	}
    t      = MPI_Wtime() - tstart;
    /* The low partner is the master; both must use the same values */
    if (myrank < partner) 
	MPI_Send( &t, 1, MPI_DOUBLE, partner, TIME_VAL, comm );
    else
	MPI_Recv( &t, 1, MPI_DOUBLE, partner, TIME_VAL, comm, &status );
    if (t > 0.0) {
	cnt = GOAL_SEC/t;
	if (cnt <= 0) cnt = 1;
	break;
	}
    cnt *= 2;
    } while(1);
/* Do the test several times, and take the max value */
rtest = 10000000;
for (testnum = 0; testnum < 5; testnum++) {
    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, DATA_VAL, 
		 MPI_BOTTOM, 0, MPI_INT, partner, DATA_VAL, comm, &status );
    tstart = MPI_Wtime();
    for (i=0; i<cnt; i++) {
	if (myrank < partner) {
	    MPI_Send( sbuf, len, MPI_INT, partner, DATA_VAL, comm );
	    MPI_Recv( rbuf, len, MPI_INT, partner, DATA_VAL, comm, &status );
	    }
	else {
	    MPI_Recv( rbuf, len, MPI_INT, partner, DATA_VAL, comm, &status );
	    MPI_Send( sbuf, len, MPI_INT, partner, DATA_VAL, comm );
	    }
	}
    t      = MPI_Wtime() - tstart;
    if (t < rtest && t > 0.0) rtest = t;
    }
if (t > 0) {
    rate = cnt * 2 * len * sizeof(int) / t;
    }

free( sbuf );
free( rbuf );
return rate;
}

/* 
 */
void FindClusters( ratematrix, size, sysnames )
double *ratematrix;
int    size;
char   **sysnames;
{
int    i, j, k;
int    *members, nclusters = 0;
int    *thiscluster, clustersize;
int    fastloc;
double *internal_rate,   /* LOWEST rate to members accepted */
       *external_rate;   /* HIGHEST rate to rejected member */
double i_rate, x_rate;   /* Temps for getting these values */
double rtest;            /* Use for temp rate value */
double maxrate;

/* Initial cluster membership */
members	    = (int *)malloc( size * sizeof(int ) );
thiscluster = (int *)malloc( size * sizeof(int ) );
internal_rate = (double *)malloc( size * sizeof(double) );
external_rate = (double *)malloc( size * sizeof(double) );

for (i=0; i<size; i++) 
    members[i] = -1;

while (1) {
    /* Find the highest rate in the remaining members */
    maxrate = 0.0;
    for (i=0; i<size; i++) {
	if (members[i] >= 0) continue;
	for (j=0; j<size; j++) {
	    if (members[j] >= 0 || i == j) continue;
	    if (ratematrix[i+j*size] > maxrate) {
		maxrate = ratematrix[i+j*size];
		fastloc = j;
		}
	    }
	}
    if (maxrate == 0) break;
    /* Find all systems not yet in a cluster that have the same rate
       to all members of this cluster */
    clustersize		       = 0;
    members[fastloc]	       = nclusters;
    thiscluster[clustersize++] = fastloc;
    for (i=0; i<size; i++) {
	/* For each processor not in a cluster */
	if (members[i] >= 0) {
	    /* Should we look at x_rate? */
	    continue;
	    }
	for (j=0; j<clustersize; j++) {
	    /* For each of the cluster members */
	    k = thiscluster[j];
	    if (ratematrix[i+k*size] < maxrate * (1-TOLERANCE)) break;
	    }
	if (j == clustersize) {
	    /* Add i to cluster */
	    members[i]		       = nclusters;
	    thiscluster[clustersize++] = i;
	    }
	}
    /* Compute the rate spread between members and non-members */
    x_rate = 0;
    for (i=0; i<size; i++) {
	if (members[i] == nclusters) continue;
	for (j=0; j<clustersize; j++) {
	    rtest = ratematrix[thiscluster[j] + i * size];
	    if (rtest > x_rate) x_rate = rtest;
	    }
	}
    i_rate = maxrate;
    for (i=0; i<clustersize; i++) {
	for (j=i+1; j<clustersize; j++) {
	    rtest = ratematrix[thiscluster[j] + thiscluster[i]*size];
	    if (rtest< i_rate) i_rate = rtest;
	    }
	}
    internal_rate[nclusters] = i_rate;
    external_rate[nclusters] = x_rate;
    nclusters++;
    }

/* Print out membership */
for (i=0; i<nclusters; i++) {
    printf( 
        "Cluster %d (min internal rate = %.2f, max external rate = %.2f):\n", 
	   i, internal_rate[i] * 1.0e-6, external_rate[i] * 1.0e-6 );
    if (internal_rate[i] < external_rate[i]) {
	printf( "* Warning! Data does not cluster cleanly.  This cluster\n" );
	printf( "* probably belongs to another group\n" );
	}
    for (j=0; j<size; j++) {
	if (members[j] == i) {
	    printf( "        %d (%s)\n", j, sysnames[j] );
	    }
	}
    }

/* 
   Note that we COULD perform tests that the systems ARE clustered; 
   in particular, we could try long data runs with two members of different
   clusters.  However, this works only if the cluster traffic is isolated.
 */
free( members );
free( thiscluster );
free( internal_rate );
free( external_rate );
}
