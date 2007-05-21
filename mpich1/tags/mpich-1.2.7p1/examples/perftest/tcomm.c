/*
   This program tests the communications channels in a parallel computer
   to see if they have similar speeds.

   You'd think that vendors would have such a diagnostic; you'd be wrong.

   The method is to pass a token between pairs of processors, for all 
   immediage neighbors.  This tests ONLY neighbor links; it does not
   test pass-through effects (so, for a circuit-switched or packet-switched
   system, not all routes are tested).  The times are compared; routes
   whose times very greatly from the average are flagged.

   Recently changed to use a device-independent graphics display
 */

#include "mpi.h"
#include "mpptest.h"
#include "getopts.h"
#include <math.h>
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* #define DEBUG  */

/* Forward references */
void OrderNbrs( int, int * ), 
     TokenTestSync( int, double *, int, int, int ),
     TokenTestASync( int, double *, int, int, int ),
     GenerateReport( int *, int, double *, double, int, int, int *, int ),
     DrawHistogram( double *, int, int, FILE *, double, double, int );
#ifdef DRAW
void DrawDanceHall(), 
     DrawMesh(), 
     DrawGRHistogram();
#endif
double RemoveOutliers( double *, int, double, double, double, double );
int *CollectData( int, int *, int * );
void Error( const char * );
void SYIsort( register int, register int * );

#define MAX_NBRS 1024
#ifdef DRAW
static GRctx        *gctx = 0;
#endif

int main( int argc, char *argv[] )
{
    double       *times;
    int          nnbrs, nbrs[MAX_NBRS], mtype, badnbrs[MAX_NBRS];
    int          len, reps, nbr, sval[3], ctype, myid;
    double       rtol;
    int          k, do_graph = 0, nx = 0, ndim[2];
    int          mysize;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &myid );
    MPI_Comm_size( MPI_COMM_WORLD, &mysize );

/* Get test parameters */
    if (SYArgHasName( &argc, argv, 1, "-help" )) {
	if (myid == 0) {
	    fprintf( stderr,
		     "%s -size len max incr", argv[0] );
	    fprintf( stderr, "\
-rtol <tolerance> -reps <repititions> -async -force\n\
-nx n\n\
-mesh nx ny Use 2d mesh topology\n\
-all        Use complete connection topology\n" );
#ifdef DRAW
	    fprintf( stderr, "\
-graph      Draw graph of communication test\n" );
#endif
	}
	MPI_Finalize();
	return 0;
    }
    
    rtol    = 0.05;
    reps    = 100;
    ctype   = 0;
    sval[0] = 64;
    sval[1] = 64;
    sval[2] = 64;
    SYArgGetDouble( &argc, argv, 1, "-rtol", &rtol );
    SYArgGetInt(    &argc, argv, 1, "-reps", &reps );
    SYArgGetIntVec( &argc, argv, 1, "-size", 3, sval );
    if (SYArgHasName( &argc, argv, 1, "-async" )) ctype = 1;
    SYArgGetInt( &argc, argv, 1, "-nx", &nx );
#ifdef DRAW
    do_graph = SYArgHasName( &argc, argv, 1, "-graph" );
    if (do_graph && PImytid == 0) {
	/* Use "-grfname name" for output graphics file */
	gctx = GRCreateFromArgsNoX( &argc, argv );
    }
#endif

/* Get the neighbors */
    if (SYArgHasName( &argc, argv, 1, "-all" )) {
	nnbrs = 0;
	for (k=0; k<mysize; k++) {
	    if (k != myid)
		nbrs[nnbrs++] = k;
        }
    }
    else if (SYArgGetIntVec( &argc, argv, 1, "-mesh", 2, ndim )) {
	nx = ndim[0];
    }
    else {
	/* Should ask the system for good neighbors.  This assumes that
	   a 2-d topology fits well */
	MPI_Comm cart2d;
	MPI_Group cart_group, world_group;
	int      dims[2], periods[2], cnbrs[4], itmp[4];

	periods[0] = periods[1] = 0;
	MPI_Dims_create( mysize, 2, dims );
	MPI_Cart_create( MPI_COMM_WORLD, 2, dims, periods, 1, &cart2d );
	MPI_Cart_shift( cart2d, 0, 1, &cnbrs[0], &cnbrs[1] );
	MPI_Cart_shift( cart2d, 1, 1, &cnbrs[2], &cnbrs[3] );
	/* Convert these ranks to ranks in comm world */
	MPI_Comm_group( MPI_COMM_WORLD, &world_group );
	MPI_Comm_group( cart2d, &cart_group );
#ifdef MPI_PROC_NULL_OK	
	nnbrs = 4;
	MPI_Group_translate_ranks( cart_group, nnbrs, cnbrs, world_group, nbrs );
#else
	/* Remove MPI_PROC_NULL */
	nnbrs = 0;
	for (k=0; k<4; k++) {
	    if (cnbrs[k] != MPI_PROC_NULL) {
		itmp[nnbrs++] = cnbrs[k];
	    }
	}
	MPI_Group_translate_ranks( cart_group, nnbrs, itmp, world_group, nbrs );
#endif
	MPI_Group_free( &cart_group );
	MPI_Group_free( &world_group );
	MPI_Comm_free( &cart2d );
	for (k=0; k<nnbrs; k++) {
	    if (nbrs[k] == MPI_UNDEFINED) nbrs[k] = MPI_PROC_NULL;
	}
    }

/* need to compute the message type to use and order so that nodes don't
   block.  Note that everyone already knows all the processors that they
   will be communicating with. */
    OrderNbrs( nnbrs, nbrs );

    times = (double *)malloc( nnbrs * sizeof(double) );
    if (!times) Error( "malloc failed" );

    for (len=sval[0]; len<=sval[1]; len+=sval[2]) {
	/* For each neighbor, start the test.  
	 */
	for (nbr = 0; nbr < nnbrs; nbr++) {
	    mtype = (nbrs[nbr] > myid) ? myid : nbrs[nbr];
	    switch (ctype) {
	    case 0:
		TokenTestSync( nbrs[nbr], times + nbr, mtype, len, reps ); 
		break;
	    case 1:
		TokenTestASync( nbrs[nbr], times + nbr, mtype, len, reps ); 
		break;
	    default:
		Error( "Unknown ctype" );
		break;
            }
	}
	/* Generate report */
	GenerateReport( nbrs, nnbrs, times, rtol, len, reps, badnbrs, 
			do_graph );
#ifdef DRAW
	if (do_graph) {
	    if (nx == 0) 
		DrawDanceHall( gctx, nbrs, nnbrs, times, rtol, badnbrs );
	    else 
		DrawMesh( gctx, nbrs, nnbrs, times, rtol, badnbrs, nx );
	}
#endif
    }
    
    free( times );
/* This won't work for batch jobs (like EUI-H...) */
#ifdef DRAW
    if (do_graph && myid == 0) {
	GRClose( gctx );
    }
#endif
    MPI_Finalize();
    return 0;
}

/* To get the timing right, the low processor sends to the high processor
   to start the test, then the timing starts */
void TokenTestSync( int nbr, double *time, int phase, int len, int reps )
{
    int          myid, i;
    double       t1, t2;
    char         *sbuf, *rbuf;
    MPI_Status   status;

    MPI_Comm_rank( MPI_COMM_WORLD, &myid );
    rbuf = (char *)malloc( len ); if (!rbuf) Error("malloc failed");
    sbuf = (char *)malloc( len ); if (!sbuf) Error("malloc failed");

#ifdef DEBUG
    printf( "[%d] exchanging with %d in phase %d\n", myid, nbr, phase );
    fflush(stdout);
#endif
    if (myid < nbr) {
	MPI_Send( sbuf, 0, MPI_BYTE, nbr, phase, MPI_COMM_WORLD );
	MPI_Recv( rbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD, &status );
	t1 = MPI_Wtime();
	for (i=0; i<reps; i++) {
	    MPI_Send( sbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD );
	    MPI_Recv( rbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD, 
		      &status );
	}
	t2 = MPI_Wtime();
	*time = t2 - t1;
    }
    else {
	MPI_Recv( rbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD, &status );
	MPI_Send( sbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD );
	t1 = MPI_Wtime();
	for (i=0; i<reps; i++) {
	    MPI_Recv( rbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD, 
		      &status );
	    MPI_Send( sbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD );
	}
	t2 = MPI_Wtime();
	*time = t2 - t1;
    }
#ifdef DEBUG
    printf( "[%d] time for test was %f\n", myid, *time );
#endif
    free( rbuf );
    free( sbuf );
}

void TokenTestASync( int nbr, double *time, int phase, int len, int reps )
{
    int            myid, i;
    double         t1, t2;
    char           *sbuf, *rbuf;
    MPI_Request    rid;
    MPI_Status     status;

    MPI_Comm_rank( MPI_COMM_WORLD, &myid );
    rbuf = (char *)malloc( len ); if (!rbuf) Error("malloc failed");
    sbuf = (char *)malloc( len ); if (!sbuf) Error("malloc failed");

    if (myid < nbr) {
	MPI_Send( sbuf, 0, MPI_BYTE, nbr, phase, MPI_COMM_WORLD );
	MPI_Recv( rbuf, 0, MPI_BYTE, nbr, phase, MPI_COMM_WORLD, &status );
	t1 = MPI_Wtime();
	for (i=0; i<reps; i++) {
	    MPI_Irecv( rbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD, &rid );
	    MPI_Send( sbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD );
	    MPI_Wait( &rid, &status );
	}
	t2 = MPI_Wtime();
	*time = t2 - t1;
    }
    else {
	MPI_Recv( rbuf, 0, MPI_BYTE, nbr, phase, MPI_COMM_WORLD, &status );
	MPI_Irecv( rbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD, &rid );
	MPI_Send( sbuf, 0, MPI_BYTE, nbr, phase, MPI_COMM_WORLD );
	t1 = MPI_Wtime();
	for (i=0; i<reps-1; i++) {
	    MPI_Wait( &rid, &status );
	    MPI_Send( sbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD );
	    MPI_Irecv( rbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD, &rid );
	}
	MPI_Wait( &rid, &status );
	MPI_Send( sbuf, len, MPI_BYTE, nbr, phase, MPI_COMM_WORLD );
	t2 = MPI_Wtime();
	*time = t2 - t1;
    }
    free( rbuf );
    free( sbuf );
}

#define MAXPHYLEN 40
typedef struct {
    int id, partner;
    double time;
    } BadList;

/*
   This routine analyzes the results, looking for unusually fast or
   slow nodes.  A histogram of the times is produced as well.

   badnbrs[i] = +/- 1 if nbrs[i] is out-of-range, 0 otherwise.
                (+ if time > average, - if < average)
 */
void GenerateReport( int *nbrs, int nnbrs, double *times, double rtol, 
		     int len, int reps, int *badnbrs, int do_graph )
{
    int     i, nlinks, j;
    int     cnt = 0;
    double  rlow, rhigh;
    double  mintime, maxtime, avetime, wtime;
    char    phy_name[MPI_MAX_PROCESSOR_NAME];
    int     namelen;
    int     nslow = 0;       /* This is a simple list of the 5 slowest links */
    BadList slowest[5];
    int     kk, jj;
    int     myid, mysize;

    MPI_Comm_rank( MPI_COMM_WORLD, &myid );
    MPI_Comm_size( MPI_COMM_WORLD, &mysize );

/* Get some information on the global times.  We are assuming that
   all neighbors should have the same speed of links */
    maxtime = times[0];
    mintime = times[0];
    avetime = times[0];
    for (i=1; i<nnbrs; i++) {
	avetime += times[i];
	if (times[i] > maxtime)      maxtime = times[i];
	else if (times[i] < mintime) mintime = times[i];
    }

    wtime = maxtime;
    MPI_Allreduce( &wtime, &maxtime, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD );
    wtime = mintime;
    MPI_Allreduce( &wtime, &mintime, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD );
    wtime = avetime;
    MPI_Allreduce( &wtime, &avetime, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD );

    MPI_Allreduce( &nnbrs, &nlinks, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    avetime /= nlinks;

    for (i=0; i<nnbrs; i++) 
	badnbrs[i] = 0;
/*
  Here we could try to remove outliers and recompute the average
  (that is, discard any local values that are well out-of-range, then
  recompute the average time on the remaining links.  Do this until
  no further values are discarded.
*/

/* Save the name of THIS processor */
    MPI_Get_processor_name( phy_name, &namelen );

#ifdef DEBUG
    if (myid == 0) {
	printf( "times are (%f,%f,%f) on %d links\n", 
		mintime, maxtime, avetime, nlinks );
    }
    printf( "Results for %s\n", phy_name );
#endif
/* Look for nodes that are away from the mean */
    if (maxtime - mintime >= rtol * avetime) {
	/* Compute a new average */
	avetime = RemoveOutliers( times, nnbrs, 
				  mintime, maxtime, 2*rtol, avetime );
    
	/* Somebody is bad */
	rlow  = avetime * (1.0 - rtol);
	rhigh = avetime * (1.0 + rtol);
	if (myid == 0) {
	    printf( "%cNode[  PhysNode  ] Nbr         Time  AverageTime        %%Diff\n",
		    do_graph ? '#' : ' ' );
	    fflush( stdout );
	}

	/* This begins a sequential section; all processes execute in rank 
	   order */
/* 	MPE_Seq_begin( MPI_COMM_WORLD, 1 );*/
	if (myid > 0) {
	    MPI_Status status;
	    MPI_Recv( MPI_BOTTOM, 0, MPI_BYTE, myid-1, 57, MPI_COMM_WORLD,
		      &status );
	}
	j = myid;
	/* If we could pass a data-value with the token, we could
	   pass the error count along.  If we were the first one,
	   we could issue an error message. */
	for (i = 0; i < nnbrs; i++) {
	    if (times[i] < rlow || times[i] > rhigh) {
		cnt ++;
		printf( "%c%4d[%12.12s] %3d %12.2e %12.2e %12.2e\n", 
			do_graph ? '#' : ' ', 
			j, phy_name, nbrs[i], times[i], avetime, 
			100.0*(times[i] - avetime)/avetime );
		fflush( stdout );
		badnbrs[i] = (times[i] < rlow) ? -1 : 1;
	    }
	    /* Update the list of slowest links */
	    if (nslow < 5) {
		/* See if we should insert it into the middle ... */
		for (kk=0; kk<nslow; kk++) {
		    if (slowest[kk].time < times[i]) {
			/* Move the values down */
			for (jj=nslow; jj>kk; jj--) 
			    slowest[jj] = slowest[jj-1];
			slowest[kk].id      = j;
			slowest[kk].partner = nbrs[i];
			slowest[kk].time    = times[i];
			break;
		    }
		}
		if (kk == nslow) {
		    slowest[kk].id = j;
		    slowest[kk].partner = nbrs[i];
		    slowest[kk].time = times[i];
		}
		nslow++;
	    }
	    else if (times[i] > slowest[nslow-1].time) {
		for (kk=0; kk<nslow; kk++) {
		    if (slowest[kk].time < times[i]) {
			/* Move the values down */
			for (jj=4; jj>kk; jj--) 
			    slowest[jj] = slowest[jj-1];
			slowest[kk].id      = j;
			slowest[kk].partner = nbrs[i];
			slowest[kk].time    = times[i];
			break;
		    }
		}
	    }
	}
    }
/*    MPE_Seq_end( MPI_COMM_WORLD, 1 ); */
    if (myid < mysize - 1) {
	MPI_Send( MPI_BOTTOM, 0, MPI_BYTE, myid+1, 57, MPI_COMM_WORLD );
    }

    i = cnt;
    MPI_Allreduce( &i, &cnt, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (myid == 0) {
	if (do_graph) printf( "#" );
	if (cnt == 0) printf( "All links within range\n" );
	else          printf( "%d links are out-of-range\n", cnt );
	if (do_graph) printf( "#" );
	printf( "For message size = %d, Average rate = %.0f bytes/sec\n",
		len, 2.0 * (double)(len * reps) / avetime );
    }
    if (cnt) {
	int nbin = 40;
	while (nbin > nlinks * 2) nbin /= 2;
	if (myid == 0) {
	    printf( "\n%cHistogram by time on each link\n", do_graph ? '#' : ' ' );
	    printf( "%c(Number of links in each bin by time)\n", 
		    do_graph ? '#' : ' ' );
	}
	DrawHistogram( times, nnbrs, nbin, stdout, mintime, maxtime, 
		       do_graph );
#ifdef DRAW
	if (do_graph)
	    DrawGRHistogram( gctx, times, nnbrs, mintime, maxtime );
#endif
    }

/* Data for a plot of processors versus time would also be interesting,
   perhaps to an auxillery file */

/* Also useful to do a short list of the physical nodes involved in the 
   SLOWEST links, the help assimilate the information */
#ifdef FOO
    /* still need to get the top 5 over the machine...  */
    for (j=0; j<=PInumtids; j++) {
	if (PIgtoken(PSAllProcs,j)) {
	    for (kk=0; kk<nslow; kk) {
		printf( "[%d] ");
	    }
	}
    }
#endif
}


/* 
   Algorithm for ordering the partners.
   This is an ordering that can be computed entirely locally for
   each processor.  A similar ordering is used in BlockComm for
   synchronous communication.

   The idea is to break the communication up into phases between processors
   that differ in a current bit position (mask).  Only those processors
   may communicate in that phase.

   This algorithm is NOT guarenteed to generate an optimial ordering.
   It does do so for hypercubes and for meshes with even dimension.
   Meshes with odd dimension will have a poor schedule (roughly
   proportional to the diameter of the mesh).
 */
void OrderNbrs( int nnbrs, int *nbrs )
{
    int mask = 0x1, i;
    int *newnbrs, cnt, myid;

    MPI_Comm_rank( MPI_COMM_WORLD, &myid );
    newnbrs = (int *)malloc( nnbrs * sizeof(int) );    
    if (!newnbrs) Error( "malloc failed" );

/* Sort by increasing node number */
    SYIsort( nnbrs, nbrs );

    cnt     = 0;
    while (cnt < nnbrs) {
	for (i=0; i<nnbrs; i++) {
	    if (nbrs[i] >= 0 && ((myid & mask) ^ (nbrs[i] & mask)) &&
		myid > nbrs[i]) {
/* Masters */
		newnbrs[cnt++] = nbrs[i];
		nbrs[i] = -1;
	    }
	}
	for (i=0; i<nnbrs; i++) {
	    if (nbrs[i] >= 0 && ((myid & mask) ^ (nbrs[i] & mask))) {
/* Slaves */
		newnbrs[cnt++] = nbrs[i];
		nbrs[i] = -1;
	    }
	}
	mask <<= 1;
    }

    memcpy( nbrs, newnbrs, nnbrs*sizeof(int) );
    free(newnbrs);
}

/*
    Draw a histogram on FILE fp using (double) data and nbin bins
    The values in data are bined between dmin and dmax
 */     
void DrawHistogram( double *data, int n, int nbin, FILE *fp, double dmin, 
		    double dmax, int do_graph )
{
    char *line;
    int  *bins, *work;
    int  i, j, ib;
    int  maxcnt;
    int  myid;

    MPI_Comm_rank( MPI_COMM_WORLD, &myid );

    bins = (int *)malloc( nbin * 2 * sizeof(int) );
    work = bins + nbin;
    line = (char *)malloc( nbin + 3 );             
    if (!bins || !line) Error( "malloc failed" );

    for (i=0; i<nbin; i++)
	bins[i] = 0;

    for (i=0; i<n; i++) {
	ib  = (nbin - 1) * (data[i] - dmin) / (dmax - dmin);
	bins[ib]++;
    }
    for (i=0; i<nbin; i++) work[i] = bins[i];
    MPI_Allreduce( work, bins, nbin, MPI_INT, MPI_SUM, MPI_COMM_WORLD );

    if (myid == 0) {
	maxcnt = 0;
	for (i=0; i<nbin; i++) {
	    line[i+1] = ' ';
	    if (bins[i] > maxcnt) maxcnt = bins[i];
        }
	line[0]      = '|';
	line[nbin+1] = '|';
	line[nbin+2] = 0;
	for (j=maxcnt; j>0; j--) {
	    for (i=0; i<nbin; i++)
		if (bins[i] == j) line[i+1] = '*';
	    fprintf( fp, "%c%s\n", do_graph ? '#' : ' ', line );
        }
	fprintf( fp, "%cmin = %12.2e max = %12.2e\n", 
		 do_graph ? '#' : ' ', dmin, dmax );
    }
    free( bins );
    free( line );
}

/* Compute a new average time after discarding times that are away
   from the average.

   This should really be enhanced to remove any far-out clumps.  For example,
   if the histogram is

   0 10 9 0 0 0 0 0 0 0 0 2 0 0 2

   The 4 on the end should be considered outliers.  One possibility
   is to accept any negative number bu reject large positive differences;
   this would eliminate these.

   Another possibility is to successively remove the fartherest from the 
   average until the remainer fits within the original limits.  We could
   even do a fit to the data, and remove the outside ones until the remainder
   had a good fit.
   
*/
double RemoveOutliers( double *times, int n, double mintime, double maxtime, 
		       double rtol, double avetime )
{
    int    cnt, i;
    double sum, work;
    double newave;
    int    *discard;   /* indicates if a value has been discarded */
    int    maxdev;     /* index of maximum deviation */
    double curdev;     /* size of current deviation */
    double glodev;     /* global deviation */
    double sum2;       /* used to compute new average */
    int    cnt2; 
    int    myid;

    MPI_Comm_rank( MPI_COMM_WORLD, &myid );

    if (myid == 0) {
	printf("About to try and remove outliers.  This may take some time...\n" );
	fflush( stdout );
    }
    discard = (int *)malloc( n * sizeof(int) );  
    if (!discard) Error( "malloc failed" );
    for (i=0; i<n; i++) discard[i] = 0;
    cnt    = 0;
    newave = avetime;
    while (cnt == 0) {
	cnt    = 0;
	sum    = 0.0;
	maxdev = -1;
	curdev = 0.0;
	for (i=0; i<n; i++) {
	    if (!discard[i] && fabs( times[i] - newave ) > rtol * newave) {
		/* Keep track of the largest deviation */
		if (maxdev == -1 || fabs( times[i] - newave ) > curdev) {
		    maxdev = i;
		    curdev = fabs( times[i] - newave );
		}
	    }
	    if (fabs( times[i] - newave ) < rtol * newave ) {
		cnt++;
		sum += times[i];
	    }
	}

	MPI_Allreduce( &curdev, &glodev, 1, MPI_DOUBLE, MPI_MAX, 
		       MPI_COMM_WORLD );
	if (maxdev >= 0 && curdev >= 0.95 * glodev)  {
#ifdef DEBUG
	    printf( "Discarding outlier (%d)\n", myid );
	    fflush( stdout ); 
#endif
	    discard[maxdev] = 1;
	}
	/* compute a new average */
	sum2 = 0.0;
	cnt2 = 0;
	for (i=0; i<n; i++) {
	    if (!discard[i]) {
		sum2 += times[i];
		cnt2++;
	    }
	}
	work = sum2;
	MPI_Allreduce( &work, &sum2, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD );
	i    = cnt2;
	MPI_Allreduce( &i, &cnt2, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
	if (cnt2 > 0) newave = sum2 / cnt2;
	/* printf( "New average is %f\n", newave ); */

	
	work = sum;
	MPI_Allreduce( &work, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD );
	i    = cnt;
	MPI_Allreduce( &i, &cnt, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
	if (glodev == 0.0) {
	    /* We didn't find anyone to remove, so we'll expand the limits */
	    /* If cnt is STILL 0, EVERYTHING was an outlier, so we prepare to 
	       try again with twice the tolerance */
	    rtol *= 2.0;
	}
    }
    free( discard );
    return sum / cnt;
}

/*
   The following code (not yet written) 
   is a first attempt to help generate graphical 
   output for the output from this routine.  The idea is

   If the topology is known and has limited connectivity (i.e., mesh), 
   then draw it.  For this, we need to know our location in the mesh.

   If the topology is not known or is highly connected, generate one of 
   two plots:
       for each node, a separate plot of all connections
       a "dance hall" diagram.

   The graphs should draw in-range lines as dotted and out-of-range lines
   as solids

   Note that since the data is distributed, these are parallel routines.
 */

#ifdef DRAW
/* type == 0 for in range, 1 otherwise */
void DrawConnection( gctx, fromx, fromy, tox, toy, type )
GRctx *gctx;
int  fromx, fromy, tox, toy, type;
{
GRStyle style;

if (type == 0) {
    style.line_style = GRSOLID;
    style.color      = GRBLACK;
    }
else if (type < 0) {
    style.line_style = GRDOTS;
    style.color      = GRGREEN;
    }
else {
    style.line_style = GRDASH;
    style.color      = GRRED;
    }
GRDrawLine( gctx, (double)fromx, (double)fromy, (double)tox, (double)toy,
	    &style );
}

void DrawNode( gctx, xc, yc, num )
GRctx *gctx;
int  xc, yc, num;
{
char snum[5];	
GRStyle style;
style.line_style = GRSOLID;
style.color      = GRBLACK;
GRDrawRectangle( gctx, (double) (xc - 2), (double) (xc + 2),
		       (double) (yc - 2), (double) (yc + 2), &style );
sprintf( snum, "%d", num );
GRDrawText( gctx, (double)xc, (double)yc, snum, 10, GRCENTER ); 
}

/* 
   Draw a dance-hall diagram (all processors on bottom and top)

   This uses a single program (the root) to draw, avoiding 
   problems with multiple writers and connections
 */
void DrawDanceHall( gctx, nbrs, nnbrs, times, rtol, badnbrs )
GRctx  *gctx;
int    *nbrs, nnbrs, *badnbrs;
double *times, rtol;
{
int i, j, k, *icol;

if (PImytid == 0) {
    GRStartPage( gctx );	
    GRSetLimits( gctx, -3.0, (double)(PInumtids * 5), -1.0, 15.0 );
    GRDrawAxis( gctx, 0 );
    }
/* The approach is for each node to wait until node 0 tells them to go ahead,
   then they send a single record of the form nnbrs(to,link)*; 
   To speed the collection, PIgcol is used */

/* Draw the nodes */
if (PImytid == 0) {
    for (i=0; i<PInumtids; i++) {
        DrawNode( gctx, i*5, 2,  i );
        DrawNode( gctx, i*5, 12, i );
        }
    }
icol = CollectData( nnbrs, nbrs, badnbrs );
if (PImytid == 0) {
    for (i=0; i<PInumtids; i++) {
        nnbrs = icol[0];
	j     = icol[1];   /* Source */
	for (k=0; k<nnbrs; k++) {
	    DrawConnection( gctx, j*5, 4, icol[2*k+2]*5, 10, icol[2*k+3] );
	    }
	icol += icol[0] * 2 + 2;
	}
    GREndPage( gctx );    
    }
}

void DrawMesh( gctx, nbrs, nnbrs, times, rtol, badnbrs, nx )
GRctx  *gctx;
int    *nbrs, nnbrs, *badnbrs, nx;
double *times, rtol;
{
int i, k, ifrom, jfrom, ito, jto, *icol;

if (PImytid == 0) {
    GRSetLimits( gctx, -3.0, (double)(nx * 5), 
		       -3.0, (double)((PInumtids / nx) * 5 - 2) );
    GRDrawAxis( gctx, 0 );
    }
/* Draw the nodes in a mesh framework */
if (PImytid == 0) {
    for (i=0; i<=PInumtids; i++) {
	ifrom = i % nx;
	jfrom = i / nx;
	DrawNode( gctx, ifrom*5, jfrom*5,  i );
        }
    }
icol = CollectData( nnbrs, nbrs, badnbrs );

if (PImytid == 0) {
    for (i=0; i<PInumtids; i++) {
    	ifrom = icol[1] % nx;
	jfrom = icol[1] / nx;
	nnbrs = icol[0];
	for (k=0; k<nnbrs; k++) {
	    ito = icol[2*k+2] % nx;
	    jto = icol[2*k+2] / nx;
	    if (jfrom == jto) {
		if (ifrom > ito) 
		    DrawConnection( gctx, ito*5+2, jto*5 - 1, 
				    ifrom*5-2, jfrom*5 - 1, icol[2*k+3] );
		else
		    DrawConnection( gctx, ifrom*5+2, jfrom*5 + 1, ito*5-2, 
				    jto*5 + 1,  icol[2*k+3] );
		}
	    else if (jfrom < jto) 
		DrawConnection( gctx, ifrom*5+1, jfrom*5 + 2, 
			        ito*5+1, jto*5 - 2, icol[2*k+3] );
	    else 
		DrawConnection( gctx, ito*5 - 1, jto*5 + 2, 
			        ifrom*5 - 1, jfrom*5 - 2, icol[2*k+3] );
	    }
	icol += icol[0] * 2 + 2;
	}
    }
}

void DrawOneToAll( )
{
    Error( "Draw one to all not implemented" );
}

#endif

int *CollectData( int nnbrs, int *nbrs, int *badnbrs )
{
    int *icol, *lcol, k, nlinks, iwork, glen, *gcol, *displs;
    int mysize, myid;

    MPI_Comm_size( MPI_COMM_WORLD, &mysize );
    MPI_Comm_rank( MPI_COMM_WORLD, &myid );

/* Get the number of links */
    MPI_Allreduce( &nnbrs, &nlinks, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );

/* Allocate storage */
    icol = (int *)malloc( mysize * sizeof(int) );  
    lcol = (int *)malloc( (nnbrs * 2 + 2) * sizeof(int) );
    gcol = (int *)malloc( (nlinks * 2 + 2 * mysize) * sizeof(int) );  
    displs = (int *)malloc( mysize * sizeof(int) );
    if (!icol || !lcol || !gcol || !displs) Error( "malloc failed" );
    lcol[0] = nnbrs;
    lcol[1] = myid;
    for (k=0; k<nnbrs; k++) {
	lcol[2*k+2] = nbrs[k];
	lcol[2*k+3] = badnbrs[k];
    }
    /* Get the array of receive counts and displacements */
    iwork = 2 * nnbrs + 1;
    MPI_Allgather( &iwork, 1, MPI_INT, icol, 1, MPI_INT, MPI_COMM_WORLD );
    displs[0] = 0;
    for (k=1; k<mysize; k++) 
	displs[k] = displs[k-1] + icol[k-1];

    MPI_Allgatherv( lcol, 2*nnbrs+2, MPI_INT, gcol, icol, displs, MPI_INT, 
		    MPI_COMM_WORLD );

    /* PIgcol( lcol, (2*nnbrs+2)*sizeof(int), icol, 
	    (nlinks * 2 + 2 * PInumtids)*sizeof(int), &glen, PSAllProcs, MSG_INT ); */
    free(lcol);
    free(icol);
    free(displs);
    return gcol;
}

#ifdef DRAW
/*
  Draw a histogram using GR code.  Only ONE GR device is open, so 
  data must be sent to the master node
 */     
void DrawGRHistogram( gctx, data, n, dmin, dmax )
GRctx   *gctx;
double  *data, dmin, dmax;
int     n;
{
double *gdata;
int    nnbrs, i, j, off;

nnbrs = n;
PIgisum( &nnbrs, 1, &i, procset );
if (PImytid > 0) {
    for (i=0; i<=PInumtids; i++) {
	if (PIgtoken( procset, i )) {
	    PIbsend( 11, data, n * sizeof(double), 0, MSG_DBL );
	    }
	}
    }
else {
    gdata = (double *)MALLOC( nnbrs * sizeof(double) );
    off   = 0;
    for (i=0; i<=PInumtids; i++) {
	PIgtoken( procset, i );
	if (i > 0 && i < PInumtids) {
	    PIbrecv( 11, gdata+off, (nnbrs-off) * sizeof(double), MSG_DBL );
	    off += PIsize() / sizeof(double);
	    }
	else if (i == 0) {
	    for (j=0; j<n; j++)
		gdata[j] = data[j];
	    off += n;
	    }
	}
    GRStartPage( gctx );	
    GRDrawHistogram( gctx, gdata, nnbrs, (dmax - dmin) / 40 );
    GREndPage( gctx );    
    FREE( gdata );
    }
}
#endif

void Error( const char *msg )
{
    fprintf( stderr, "Error: %s\n", msg );
    MPI_Abort( MPI_COMM_WORLD, 1 );
}

/* Old sorting code */
#define SWAP(a,b,t) {t=a;a=b;b=t;}

void SYiIqsort( register int *, register int);

/*@
  SYIsort - sort an array of integer inplace in increasing order

  Input Parameters:
. n  - number of values
. i  - array of integers
@*/
void SYIsort( register int n, register int *i )
{
    register int j, k, tmp, ik;

    if (n<8) {
	for (k=0; k<n; k++) {
	    ik = i[k];
	    for (j=k+1; j<n; j++) {
		if (ik > i[j]) {
		    SWAP(i[k],i[j],tmp);
		    ik = i[k];
		}
	    }
	}
    }
    else 
	SYiIqsort(i,n-1);
}

/* A simple version of quicksort; taken from Kernighan and Ritchie, page 87.
   Assumes 0 origin for v, number of elements = right+1 (right is index of
   right-most member). */
void SYiIqsort( register int *v, register int right)
{
    int          tmp;
    register int i, vl, last;
    if (right <= 1) {
	if (right == 1) {
	    if (v[0] > v[1]) SWAP(v[0],v[1],tmp);
	}
	return;
    }
    SWAP(v[0],v[right/2],tmp);
    vl   = v[0];
    last = 0;
    for ( i=1; i<=right; i++ ) {
	if (v[i] < vl ) {last++; SWAP(v[last],v[i],tmp);}
    }
    SWAP(v[0],v[last],tmp);
    SYiIqsort(v,last-1);
    SYiIqsort(v+last+1,right-(last+1));
}
