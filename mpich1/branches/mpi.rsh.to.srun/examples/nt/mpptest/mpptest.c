/*D
   mpptest - Measure the communications performance of a message-passing system

   Details:
   The greatest challange in performing these experiments in making the results
   reproducible.  On many (most?) systems, there are various events that 
   perturb timings; these can occur on the scale of 10's of milliseconds. 
   To attempt to remove the effect of these events, we make multiple tests,
   taking the minimum of many tests, each of which gives an average time.  To
   reduce the effect of transient perturbations, the entire sequence of tests
   is run several times, taking the best (fastest) time on each test.  Finally,
   a post-processing step retests any anomolies, defined as single peaks peaks
   that are significantly greater than the surrounding times (using a locally
   linear-fit model).
D*/

/* 
  This code is a major re-write of an older version that was generated 
  automatically from an older Chameleon program.  Previous versions
  worked with a wide variety of message-passing systems.
*/

#include <stdio.h>
#include <math.h>
#ifndef HUGE_VAL
#define HUGE_VAL 10.0e38
#endif


#include "mpi.h"
#include "mpptest.h"
#include "getopts.h"
int __NUMNODES, __MYPROCID;

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifndef DEFAULT_AVG
#define DEFAULT_AVG 50
#endif

#include <string.h>

/* Forward declarations */
void PrintHelp( char *[] );

/*
    This is a simple program to test the communications performance of
    a parallel machine.  
 */
    
/* If doinfo is 0, don't write out the various text lines */
static int    doinfo = 1;

/* Scaling of time and rate */
static double TimeScale = 1.0;
static double RateScale = 1.0;

/* The maximum of the MPI_Wtick values for all processes */
static double gwtick;

/* This is the number of times to run a test, taking as time the minimum
   achieve timing.  
   (NOT CURRENTLY IMPLEMENTED)
   This uses an adaptive approach that also stops when
   minThreshTest values are within a few percent of the current minimum 

n_avg - number of iterations used to average the time for a test
n_rep - number of repititions of a test, used to sample test average
to avoid transient effects 
*/
static int    minreps       = 30;
/* n_stable is the number of tests that must not (significantly, see 
   repsThresh) change the results before mpptest will decide that no
   further tests are required
*/
static int    n_stable;
static double repsThresh    = 0.05;

/* n_smooth is the number of passes over the data that will be taken to
   smooth out any anomolies, defined as times that deviate significantly from
   a linear progression
 */ 
static int    n_smooth      = 5;
char   protocol_name[256];

/* 
   We would also like to adaptively modify the number of repetitions to 
   meet a time estimate (later, we'd like to meet a statistical estimate).
   
   One relatively easy way to do this is to use a linear estimate (either
   extrapolation or interpolation) based on 2 other computations.
   That is, if the goal time is T and the measured tuples (time,reps,len)
   are, the formula for the local time is s + r n, where

   r = (time2/reps2 - time1/reps1) / (len2 - len1)
   s = time1/reps1 - r * len1

   Then the appropriate number of repititions to use is

   Tgoal / (s + r * len) = reps
 */
static double Tgoal = 1.0;
/* If less than Tgoalmin is spent, increase the number of tests to average */
static double TgoalMin = 0.5;
static int autoavg = 0;

/* This structure allows a collection of arbitray sizes to be specified */
#define MAX_SIZE_LIST 256
static int sizelist[MAX_SIZE_LIST];
static int nsizes = 0;

/* We wish to control the TOTAL amount of time that the test takes.
   We could do this with gettimeofday or clock or something, but fortunately
   the MPI timer is an elapsed timer */
static double max_run_time = 15.0*60.0;
static double start_time = 0.0;

/* All test data is contained in an array of values.  Because we may 
   adaptively choose the message lengths, provision is made to maintain the
   list elements in an array, and for many processing tasks (output, smoothing)
   only the list version is used. */

/* These are used to contain results for a single test */
typedef struct _TwinResults {
    double t,               /* min of the observations (per loop) */
	   max_time,        /* max of the observations (per loop) */
           sum_time;        /* sum of all of the observations */
    int    len;             /* length of the message for this test */
    int    ntests;          /* number of observations */
    int    n_avg;           /* number of times to run a test to get average 
			       time */
    int    new_min_found;   /* true if a new minimum was found */
    int    n_loop;          /* number of times the timing loop was
			       run and accepted */
    struct _TwinResults *next, *prev;
    } TwinResults;

TwinResults *AllocResultsArray( int );
void FreeResults( TwinResults * );
void SetResultsForStrided( int first, int last, int incr, TwinResults *twin );
void SetResultsForList( int sizelist[], int nsizes, TwinResults *twin );
void SetRepsForList( TwinResults *, int );
int RunTest( TwinResults *, double (*)(int,int,void *), void *, double );
int RunTestList( TwinResults *, double (*)(int,int,void*), void* );
int SmoothList( TwinResults *, double (*)(int,int,void *), void * );
int RefineTestList( TwinResults *, double (*)(int,int,void *),void *,
		    int, double );
void OutputTestList( TwinResults *, void *, int, int, int );
double LinearTimeEst( TwinResults *, double );
double LinearTimeEstBase( TwinResults *, TwinResults *, TwinResults*, double );
TwinResults *InsertElm( TwinResults *, TwinResults * );

/* Initialize the results array of a given list of data */

/* This structure is used to provice information for the automatic 
   message-length routines */
typedef struct {
    double (*f)( int, int, void * );
    int    reps, proc1, proc2;
    void *msgctx;
    /* Here is where we should put "recent" timing data used to estimate
       the values of reps */
    double t1, t2;
    int    len1, len2;
    } TwinTest;

int main( int argc, char *argv[] )
{
    int    dist;
    double (* BasicCommTest)( int, int, void * ) = 0;
    void *MsgCtx = 0; /* This is the context of the 
			 message-passing operation */
    void *outctx;
    void (*ChangeDist)( int, PairData ) = 0;
    int  n_avg, proc1, proc2, distance_flag, distance;
    int  first,last,incr, svals[3];
    int      autosize = 0, autodx;
    double   autorel;
    double   wtick;
    char     units[32];         /* Name of units of length */

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &__NUMNODES );
    MPI_Comm_rank( MPI_COMM_WORLD, &__MYPROCID );

    /* Get the maximum clock grain */
    wtick = MPI_Wtick();
    MPI_Allreduce( &wtick, &gwtick, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD );

    /* Set the default test name and labels */
    strcpy( protocol_name, "blocking" );
    strcpy( units, "(bytes)" );

    if (SYArgHasName( &argc, argv, 1, "-help" )) {
	if (__MYPROCID == 0) PrintHelp( argv );
	MPI_Finalize();
	return 0;
    }

    if (__NUMNODES < 2 && !SYArgHasName( &argc, argv, 0, "-memcpy" )) {
	fprintf( stderr, "Must run mpptest with at least 2 nodes\n" );
	MPI_Finalize();
	return 1;
    }

/* Get the output context */
    outctx = SetupGraph( &argc, argv );
    if (SYArgHasName( &argc, argv, 1, "-noinfo" ))    doinfo    = 0;

    /* Proc1 *must* be 0 because of the way other data is collected */
    proc1         = 0;
    proc2         = __NUMNODES-1;
    distance_flag = 0;
    if (SYArgHasName( &argc, argv, 0, "-logscale" )) {
	svals[0]      = sizeof(int);
	svals[1]      = 131072;   /* 128k */
	svals[2]      = 32;
    }
    else {
	svals[0]      = 0;
	svals[1]      = 1024;
	svals[2]      = 32;
    }
    if (SYArgHasName( &argc, argv, 1, "-distance" ))  distance_flag++;
    SYArgGetIntVec( &argc, argv, 1, "-size", 3, svals );
    nsizes = SYArgGetIntList( &argc, argv, 1, "-sizelist", MAX_SIZE_LIST, 
			      sizelist );

    if (SYArgHasName( &argc, argv, 1, "-logscale" )) {
	/* Use the sizelist field to specify a collection of power of
	   two sizes.  This is a temporary hack until we have something
	   better.  You can use the -size argument to set min and max values
	   (the stride is ignored) */
	int k;
	nsizes = 0;
	if (svals[0] == 0) {
	    sizelist[nsizes++] = 0;
	    k = 4;
	}
	else {
	    k = svals[0];
	}
	while( k <= svals[1] && nsizes < MAX_SIZE_LIST ) {
	    sizelist[nsizes++] = k;
	    k *= 2;
	}
	/* Need to tell graphics package to use log/log scale */
	DataScale( outctx, 1 );
    }

    /* Get the number of tests to average over */
    n_avg          = DEFAULT_AVG;
    if (SYArgHasName( &argc, argv, 1, "-autoavg" )) {
        autoavg = 1;
	n_avg   = 5;  /* Set a new default.  This can be overridden */
    }
    SYArgGetInt( &argc, argv, 1, "-n_avg", &n_avg ); /* was -reps */

    if (SYArgGetDouble( &argc, argv, 1, "-tgoal", &Tgoal )) {
	if (TgoalMin > 0.1 * Tgoal) TgoalMin = 0.1 * Tgoal;
    }
    SYArgGetDouble( &argc, argv, 1, "-rthresh", &repsThresh );

    SYArgGetInt( &argc, argv, 1, "-sample_reps", &minreps );
    n_stable = minreps;
    SYArgGetInt( &argc, argv, 1, "-n_stable", &n_stable );

    SYArgGetDouble( &argc, argv, 1, "-max_run_time", &max_run_time );
    if (SYArgHasName( &argc, argv, 1, "-quick" ) || 
	SYArgHasName( &argc, argv, 1, "-fast"  )) {
      /* This is a short cut for 
       -autoavg -n_stable 5 */
      autoavg  = 1;
      n_avg    = 5;
      n_stable = 5;
    }

    autosize = SYArgHasName( &argc, argv, 1, "-auto" );
    if (autosize) {
	autodx = 4;
	SYArgGetInt( &argc, argv, 1, "-autodx", &autodx );
	autorel = 0.02;
	SYArgGetDouble( &argc, argv, 1, "-autorel", &autorel );
    }

/* Pick the general test based on the presence of an -gop, -overlap, -bisect
   or no arg */
    SetPattern( &argc, argv );
    if (SYArgHasName( &argc, argv, 1, "-gop")) {
	/* we need to fix this cast eventually */
	BasicCommTest = (double (*)(int,int,void*))
			 GetGOPFunction( &argc, argv, protocol_name, units );
	MsgCtx = GOPInit( &argc, argv );
    }
    else if (SYArgHasName( &argc, argv, 1, "-halo" )) {
	int local_partners, max_partners;
        BasicCommTest = GetHaloFunction( &argc, argv, &MsgCtx, protocol_name );
	TimeScale = 1.0;  /* Halo time, not half round trip */
	local_partners = GetHaloPartners( MsgCtx );
	MPI_Allreduce( &local_partners, &max_partners, 1, MPI_INT, MPI_MAX,
		       MPI_COMM_WORLD );
	RateScale = (double) max_partners;  /* Since each sends len data */
	/* I.e., gives total rate per byte */
    }
    else if (SYArgHasName( &argc, argv, 1, "-bisect" )) {
	BasicCommTest = GetPairFunction( &argc, argv, protocol_name );
	dist = 1;
	SYArgGetInt( &argc, argv, 1, "-bisectdist", &dist );
	MsgCtx     = BisectInit( dist );
	ChangeDist = BisectChange;
	strcat( protocol_name, "-bisect" );
	if (SYArgHasName( &argc, argv, 1, "-debug" ))
	    PrintPairInfo( MsgCtx );
	TimeScale = 0.5;
	RateScale = (double) __NUMNODES; /* * (2 * 0.5) */
    }
    else if (SYArgHasName( &argc, argv, 1, "-overlap" )) {
	int MsgSize;
	char cbuf[32];
	if (SYArgHasName( &argc, argv, 1, "-sync" )) {
	    BasicCommTest = round_trip_b_overlap;
	    strcpy( protocol_name, "blocking" );
	}
	else {  /* Assume -async */
	    BasicCommTest = round_trip_nb_overlap;
	    strcpy( protocol_name, "nonblocking" );
	}
	MsgSize = 0;
	SYArgGetInt( &argc, argv, 1, "-overlapmsgsize", &MsgSize );
	MsgCtx  = OverlapInit( proc1, proc2, MsgSize );
	/* Compute floating point lengths if requested */
	if (SYArgHasName( &argc, argv, 1, "-overlapauto")) {
	    OverlapSizes( MsgSize >= 0 ? MsgSize : 0, svals, MsgCtx );
	}
	strcat( protocol_name, "-overlap" );
	if (MsgSize >= 0) {
	    sprintf( cbuf, "-%d bytes", MsgSize );
	}
	else {
	    strcpy( cbuf, "-no msgs" );
	}
	strcat( protocol_name, cbuf );
	TimeScale = 0.5;
	RateScale = 2.0;
    }
    else if (SYArgHasName( &argc, argv, 1, "-memcpy" )) {
	int use_vector = 0;
	MsgCtx     = 0;
	ChangeDist = 0;
	TimeScale = 1.0;
	RateScale = 1.0;
	use_vector = SYArgHasName( &argc, argv, 1, "-vector" );
	/* memcpy_rate_int, memcpy_rate_double */
	if (SYArgHasName( &argc, argv, 1, "-int" )) {
	    if (use_vector) {
	    }
	    else {
		BasicCommTest = memcpy_rate_int;
		strcpy( protocol_name, "memcpy-int" );
	    }
	}
	else if (SYArgHasName( &argc, argv, 1, "-double" )) {
	    if (use_vector) {
		BasicCommTest = memcpy_rate_double_vector;
		strcpy( protocol_name, "memcpy-double-vector" );
	    }
	    else {
		BasicCommTest = memcpy_rate_double;
		strcpy( protocol_name, "memcpy-double" );
	    }
	}
#ifdef HAVE_LONG_LONG
	else if (SYArgHasName( &argc, argv, 1, "-longlong" )) {
	    if (use_vector) {
		BasicCommTest = memcpy_rate_long_long_vector;
		strcpy( protocol_name, "memcpy-longlong-vector" );
	    }
	    else {
		BasicCommTest = memcpy_rate_long_long;
		strcpy( protocol_name, "memcpy-longlong" );
	    }
	}
#endif
	else {
	  BasicCommTest = memcpy_rate;
	  strcpy( protocol_name, "memcpy" );
	}
    }
    else {
	/* Pair by default */
	BasicCommTest = GetPairFunction( &argc, argv, protocol_name );
	MsgCtx = PairInit( proc1, proc2 );
	ChangeDist = PairChange;
	if (SYArgHasName( &argc, argv, 1, "-debug" ))
	    PrintPairInfo( MsgCtx );
	TimeScale = 0.5;
	RateScale = 2.0;
    }
    first = svals[0];
    last  = svals[1];
    incr  = svals[2];
    if (incr == 0) incr = 1;

/*
   Finally, we are ready to run the tests.  We want to report times as
   the times for a single link, and rates as the aggregate rate.
   To do this, we need to know how to scale both the times and the rates.

   Times: scaled by the number of one-way trips measured by the base testing
   code.  This is often 2 trips, or a scaling of 1/2.

   Rates: scaled by the number of simultaneous participants (as well as
   the scaling in times).  Compute the rates based on the updated time, 
   then multiply by the number of participants.  Note that, for a single
   sender, time and rate are inversely proportional (that is, if TimeScale 
   is 0.5, RateScale is 2.0).

 */

    start_time = MPI_Wtime();

/* If the distance flag is set, we look at a range of distances.  Otherwise,
   we just use the first and last processor */
    if (doinfo && __MYPROCID == 0) {
	HeaderGraph( outctx, protocol_name, (char *)0, units );
    }
    if(distance_flag) {
	for(distance=1;distance<GetMaxIndex();distance++) {
	    proc2 = GetNeighbor( 0, distance, 0 );
	    if (ChangeDist)
		(*ChangeDist)( distance, MsgCtx );
	    time_function(n_avg,first,last,incr,proc1,proc2,
			  BasicCommTest,outctx,
			  autosize,autodx,autorel,MsgCtx);
	}
    }
    else{
	time_function(n_avg,first,last,incr,proc1,proc2,BasicCommTest,outctx,
		      autosize,autodx,autorel,MsgCtx);
    }

/* 
   Generate the "end of page".  This allows multiple distance graphs on the
   same plot 
 */
    if (doinfo && __MYPROCID == 0) 
	EndPageGraph( outctx );
    EndGraph( outctx );

    MPI_Finalize();
    return 0;
}

/* 
   This is the basic routine for timing an operation.

   Input Parameters:
.  n_avg - Basic number of times to run basic test (see below)
.  first,last,incr - length of data is first, first+incr, ... last
         (if last != first + k * incr, then actual last value is the 
         value of first + k * incr that is <= last and such that 
         first + (k+1) * incr > last, just as you'd expect)
.  proc1,proc2 - processors to participate in communication.  Note that
         all processors must call because we use global operations to 
         manage some operations, and we want to avoid using process-subset
         operations (supported in Chameleon) to simplify porting this code
.  CommTest  -  Routine to call to run a basic test.  This routine returns 
         the time that the test took in seconds.
.  outctx -  Pointer to output context
.  autosize - If true, the actual sizes are picked automatically.  That is
         instead of using first, first + incr, ... , the routine choses values
         of len such that first <= len <= last and other properties, given 
         by autodx and autorel, are satisfied.
.  autodx - Parameter for TST1dauto, used to set minimum distance between 
         test sizes.  4 (for 4 bytes) is good for small values of last
.  autorel - Relative error tolerance used by TST1dauto in determining the 
         message sizes used.
.  msgctx - Context to pass through to operation routine
 */
void time_function( int n_avg, int first, int last, int incr, 
		    int proc1, int proc2, double (*CommTest)(int,int,void*),
		    void *outctx, int autosize, int autodx, 
		    double autorel, void *msgctx)
{
    int    distance, myproc;
    int    n_without_change;  /* Number of times through the list without
				 changes */

    myproc   = __MYPROCID;
    distance = ((proc1)<(proc2)?(proc2)-(proc1):(proc1)-(proc2));

    /* Run test, using either the simple direct test or the automatic length
     test */
    if (autosize) {
	TwinResults *twin;
	int k;

	twin = AllocResultsArray( 1024 );
	SetResultsForStrided( first, last, (last-first)/8, twin );

	/* Run tests */
	SetRepsForList( twin, n_avg );
	for (k=0; k<minreps/5; k++) {
	    int kk;
	    for (kk=0; kk<5; kk++)
		(void)RunTestList( twin, CommTest, msgctx );
	    /* Don't refine on the last iteration */
	    if (k != minreps-1) 
	      RefineTestList( twin, CommTest, msgctx, autodx, autorel );
	}
	for (k=1; k<n_smooth; k++) {
	    if (!SmoothList( twin, CommTest, msgctx )) break;
	}
	/* Final output */
	if (myproc == 0) 
	    OutputTestList( twin, outctx, proc1, proc2, distance );
	FreeResults(twin);
    }
    else {
	TwinResults *twin;
	int k;
	if (nsizes) {
	    twin = AllocResultsArray( nsizes );
	    SetResultsForList( sizelist, nsizes, twin );
	}
	else {
	    nsizes = 1 + (last - first)/incr;
	    twin = AllocResultsArray( nsizes );
	    SetResultsForStrided( first, last, incr, twin );
	}

	/* Run tests */
	SetRepsForList( twin, n_avg );
	n_without_change = 0;
	for (k=1; k<minreps; k++) {
	    if (RunTestList( twin, CommTest, msgctx )) {
	        n_without_change = 0;
	    }
	    else
	        n_without_change++;
	    if (n_without_change > n_stable) {
#if DEBUG_AUTO
		printf( "Breaking because stable results reached\n" );
#endif		
		break; 
	    }
	}
	for (k=1; k<n_smooth; k++) {
	    if (!SmoothList( twin, CommTest, msgctx )) break;
	}
	/* Final output */
	if (myproc == 0) 
	    OutputTestList( twin, outctx, proc1, proc2, distance );
	FreeResults(twin);
    }
    if (myproc == 0) 
	DrawGraph( outctx, 0, 0, 0.0, 0.0 );
}



/*****************************************************************************
   Utility routines
 *****************************************************************************/

void PrintHelp( char *argv[] ) 
{
  if (__MYPROCID != 0) return;
  fprintf( stderr, "%s - test individual communication speeds\n", argv[0] );

  fprintf( stderr, 
"Test a single communication link by various methods.  The tests are \n\
combinations of\n\
  Protocol: \n\
  -sync        Blocking sends/receives    (default)\n\
  -async       NonBlocking sends/receives\n\
  -ssend       MPI Syncronous send (MPI_Ssend) and MPI_Irecv\n\
  -force       Ready-receiver (with a null message)\n\
  -persistant  Persistant communication\n\
  -put         MPI_Put (only on systems that support it)\n\
  -get         MPI_Get (only on systems that support it)\n\
  -vector      Data is separated by constant stride (only with MPI, using UBs)\n\
  -vectortype  Data is separated by constant stride (only with MPI, using \n\
               MPI_Type_vector)\n\
\n\
  Message data:\n\
  -cachesize n Perform test so that cached data is NOT reused\n\
\n\
  -vstride n   For -vector, set the stride between elements\n\
  Message pattern:\n\
  -roundtrip   Roundtrip messages         (default)\n\
  -head        Head-to-head messages\n\
  -halo        Halo Exchange (multiple head-to-head; limited options)\n\
    \n" );
PrintHaloHelp();

  fprintf( stderr, "\
  -memcpy      Memory copy performance (no communication)\n\
  -memcpy -int Memory copy using a for-loop with integers\n\
  -memcpy -double Memory copy using a for-loop with doubles\n\
  -memcpy -longlong Memory copy using a for-loop with long longs\n" );

  fprintf( stderr, 
"  Message test type:\n\
  (if not specified, only communication tests run)\n\
  -overlap     Overlap computation with communication (see -size)\n\
  -overlapmsgsize nn\n\
               Size of messages to overlap with is nn bytes.\n\
  -bisect      Bisection test (all processes participate)\n\
  -bisectdist n Distance between processes\n\
    \n" );

  fprintf( stderr, 
"  Message sizes:\n\
  -size start end stride                  (default 0 1024 32)\n\
               Messages of length (start + i*stride) for i=0,1,... until\n\
               the length is greater than end.\n\
  -sizelist n1,n2,...\n\
               Messages of length n1, n2, etc are used.  This overrides \n\
               -size\n\
  -logscale    Messages of length 2**i are used.  The -size argument\n\
               may be used to set the limits.  If -logscale is given,\n\
               the default limits are from sizeof(int) to 128 k.\n\
  -auto        Compute message sizes automatically (to create a smooth\n\
               graph.  Use -size values for lower and upper range\n\
  -autodx n    Minimum number of bytes between samples when using -auto\n\
  -autorel d   Relative error tolerance when using -auto (0.02 by default)\n");

  fprintf( stderr, "\n\
  Detailed control of tests:\n\
  -quick       Short hand for -autoavg -n_stable 5\n\
               this is a good choice for performing a relatively quick and\n\
               accurate assessment of communication performance\n\
  -n_avg n     Number of times a test is run; the time is averaged over this\n\
               number of tests (default %d)\n\
  -autoavg    Compute the number of times a message is sent automatically\n\
  -tgoal  d    Time that each test should take, in seconds.  Use with \n\
               -autoavg\n\
  -rthresh d   Fractional threshold used to determine when minimum time\n\
               has been found.  The default is 0.05.\n\
  -sample_reps n   Number of times a full test is run in order to find the\n\
               minimum average time.  The default is 30\n\
  -n_stable n  Number of full tests that must not change the minimum \n\
               average value before mpptest will stop testing.  By default,\n\
               the value of -sample_reps is used (i.e.,no early termination)\n\
  -max_run_time n  Maximum number of seconds for all tests.  The default\n\
               is %d\n\
\n", DEFAULT_AVG, (int)max_run_time );

fprintf( stderr, "\n\
  Collective operations may be tested with -gop [ options ]:\n" );
PrintGOPHelp();

PrintGraphHelp();
PrintPatternHelp(); 
fflush( stderr ); 
}

/****************************************************************************
 * New code that uses a list to manage all timing experiments

 ****************************************************************************/
/* Setup the results array */
static TwinResults *twin_avail = 0;

TwinResults *AllocResultsArray( int nsizes )
{
    TwinResults *new;
    int         i;

    new = (TwinResults *)calloc( nsizes+1, sizeof(TwinResults) );
    if (!new) MPI_Abort( MPI_COMM_WORLD, 1 );

    for (i=1; i<nsizes-1; i++) {
	new[i].next = &new[i+1];
	new[i].prev = &new[i-1];
    }
    new[0].next	       = &new[1];
    new[0].prev	       = 0;
    new[nsizes-1].next = 0;
    new[nsizes-1].prev = &new[nsizes-2];
    /* Note that the last member (new[nsizes]) has null prev and next */

    return new;
}

void FreeResults( TwinResults *twin_p )
{
    free( twin_p );
}

/* Initialize the results array for a strided set of data */
void SetResultsForStrided( int first, int last, int incr, TwinResults *twin )
{
    int i = 0, len;
    for (len=first; len<=last; len += incr) {
	twin[i].len = len;
	twin[i].t   = HUGE_VAL;
	i++;
    }
    /* Fixup list */
    twin[i-1].next = 0;
    twin[i].prev   = 0;

    /* Setup to the avail list */
    twin_avail = &twin[i];
}

/* Initialize the results array of a given list of data */
void SetResultsForList( int sizelist[], int nsizes, TwinResults *twin )
{
    int i = 0;
    for (i=0; i<nsizes; i++) {
	twin[i].len = sizelist[i];
	twin[i].t   = HUGE_VAL;
    }
    /* Fixup list */
    twin[i-1].next = 0;
    twin[i].prev   = 0;

    /* Setup to the avail list */
    twin_avail = &twin[i];
}

/* Run a test for a single entry in the list. Return 1 if the test
   was accepted, 0 otherwise */
int RunTest( TwinResults *twin_p, double (*CommTest)(int,int,void *),
	     void *msgctx, double wtick )
{
    double t;

    t = (*CommTest)( twin_p->n_avg, twin_p->len, msgctx );
    /* t is the time over all (n_avg tests) */

    /* Make sure that everyone has the same time value so that
       they'll make the same decisions.  
     */
    MPI_Bcast( &t, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD );
    CheckTimeLimit();

#if DEBUG_AUTO
    printf( "test(%d) for %d iterations took %f time\n", 
	    twin_p->len, twin_p->n_avg, t );
#endif    
    /* Accept times only if they are much longer than the clock resolution */
    if (t > 100*wtick) {
	twin_p->n_loop++;
	twin_p->sum_time += t;
	twin_p->ntests   += twin_p->n_avg;
	/* Now, convert t to a per-loop time */
	t = t / twin_p->n_avg;
	if (t < twin_p->t) {
	    twin_p->t = t;
	    /* This could set only when t < (1-repsThresh) (previous value) */
	    twin_p->new_min_found = 1;
	}
	else
	    twin_p->new_min_found = 0;
	if (t > twin_p->max_time) twin_p->max_time = t;
	return 1;
    }
    return 0;
}

/* For each message length in the list, run the experiement CommTest.
   Return the number of records that were updated */
int RunTestList( TwinResults *twin_p, double (*CommTest)(int,int,void *),
		  void *msgctx )
{
    int n_trials;  /* Used to bound the number of retries when total time
		      is too small relative to gwtick */
    int n_updated = 0;  /* Number of fields that were updated with a 
			 new minimum */

    while (twin_p) {
        n_trials = 0;
        while (n_trials++ < 10 && 
	       !RunTest( twin_p, CommTest, msgctx, gwtick )) {
	    /* This run failed to pass the test on wtick (time too short).
	       Update the #n_avg and try again 
	       Special needs: must ensure that all processes are informed */
	    twin_p->n_avg *= 2;
        }
	if (twin_p->new_min_found) n_updated++;
	twin_p = twin_p->next;
    }
#if DEBUG_AUTO
    printf( "Found %d new minimums\n", n_updated );
#endif
    return n_updated;
}

/* This estimates the time at twin_p using a linear interpolation from the
   surrounding entries */
double LinearTimeEst( TwinResults *twin_p, double min_dx )
{
    return LinearTimeEstBase( twin_p->prev, twin_p, twin_p->next, min_dx );
}

double LinearTimeEstBase( TwinResults *prev, TwinResults *cur, 
			  TwinResults *next, double min_dx )
{
    double t_prev, t_next, t_est, dn_prev, dn_next;

    /* Look at adjacent times */
    if (prev) {
	t_prev  = prev->t;
	dn_prev = cur->len - prev->len;
    }
    else {
	t_prev  = cur->t;
	dn_prev = min_dx;
    }
    if (next) {
	t_next  = next->t;
	dn_next = next->len - cur->len;
    }
    else {
	t_next  = cur->t;
	dn_next = min_dx;
    }
    /* Compute linear estimate, adjusting for the possibly unequal
       interval sizes, at twin_p->len. */
    t_est = t_prev + (dn_prev/(dn_next + dn_prev))*(t_next-t_prev);

    return t_est;
}

/* Add an entry to the list half way (in length) betwen prev and next */
TwinResults *InsertElm( TwinResults *prev, TwinResults *next )
{
    TwinResults *tnew;

    tnew = twin_avail;
    twin_avail = twin_avail->next;
    if (!twin_avail) {
	/* Now we have a problem.  I'm going to generate an error message and
	   exit */
	fprintf( stderr, 
"Exhausted memory for results while refining test interval\n\
Rerun with a smaller interval or without the -auto option\n" );
	fflush( stderr );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
    twin_avail->prev = 0;
    
    tnew->next  = next;
    tnew->prev  = prev;
    prev->next  = tnew;
    next->prev  = tnew;
    tnew->len   = (prev->len + next->len) / 2;
    tnew->n_avg = next->n_avg;
    tnew->t     = HUGE_VAL;

#if DEBUG_AUTO
    printf( "%d running test with n_avg=%d, len=%d\n", 
	    __MYPROCID, tnew->n_avg, (int)tnew->len );fflush(stdout); 
#endif

    return tnew;
}

/* This is a breadth-first refinement approach.  Each call to this routine
   adds one level of refinement. */
int RefineTestList( TwinResults *twin, double (*CommTest)(int,int,void*),
		    void *msgctx, int min_dx, double autorel )
{
    double t_offset, t_center;
    double abstol = 1.0e-10;
    int do_refine, n_refined = 0;
    int n_loop, k;
    TwinResults *twin_p = twin, *tprev, *tnext;

    /* There is a dummy empty entry at the end of the list */
    if (!twin_avail->next) return 0;
    
    if (min_dx < 1) min_dx = 1;

    /* We find the next pointer and set the current and previous from
       that to ensure that we only look at values that were already
       computed, not the newly inserted values */
    tprev = 0;
    n_loop = 0;
    while (twin_p && twin_avail) {
	if (twin_p->n_loop > n_loop) n_loop = twin_p->n_loop;
        tnext = twin_p->next;
	/* Compute error estimate, adjusting for the possibly unequal
	 interval sizes.  t_center is the linear interpolation at tnew_p->len,
	 t_offset is the difference with the computed value */
	t_center = LinearTimeEstBase( tprev, twin_p, tnext, min_dx );
	t_offset = fabs(twin_p->t - t_center);
	do_refine = t_offset > (autorel * t_center + abstol);
	MPI_Bcast( &do_refine, 1, MPI_INT, 0, MPI_COMM_WORLD );
	if (do_refine) {
#ifdef DEBUG_AUTO
	    printf( "Refining at %d because predicted time %f far from %f\n",
		    twin_p->len, t_center, twin_p->t );
#endif
	    /* update the list by refining both segments */
	    if (twin_p->prev && twin_avail &&
		min_dx < twin_p->len - twin_p->prev->len) {
		(void)InsertElm( twin_p->prev, twin_p );
		n_refined ++; 
	    }
	    if (twin_p->next && twin_avail && 
		min_dx < twin_p->next->len - twin_p->len) {
		(void)InsertElm( twin_p, twin_p->next );
		n_refined ++;
	    }
	}
	tprev  = twin_p;
	twin_p = tnext;
    }
    MPI_Bcast( &n_refined, 1, MPI_INT, 0, MPI_COMM_WORLD );
    MPI_Bcast( &n_loop, 1, MPI_INT, 0, MPI_COMM_WORLD );

    /* Now, catch up the inserted elements with the rest of the results */
    for (k=0; k<n_loop; k++) {
	twin_p = twin;
	while (twin_p) {
	    if (twin_p->n_loop < n_loop) {
		int n_trials = 0;
		while (n_trials++ < 5 && 
		       !RunTest( twin_p, CommTest, msgctx, gwtick )) {
		    twin_p->n_avg *= 2;
		}
	    }
	    twin_p = twin_p->next;
	}
    }
    return n_refined;
}

/* Initialize the number of tests to run over which the average time is 
   computed. */
void SetRepsForList( TwinResults *twin_p, int n_avg )
{
    while (twin_p) {
	twin_p->n_avg = n_avg;
	twin_p        = twin_p->next;
    }
}

/* Smooth the entries in the list by looking for anomolous results and 
   rerunning those tests */
int SmoothList( TwinResults *twin, double (*CommTest)(int,int,void*),
		void *msgctx )
{
    double t_est;
    int do_test;
    TwinResults *twin_p = twin;
    int n_smoothed = 0;
    
    while (twin_p) {
	/* Look at adjacent times */
	if (__MYPROCID == 0) {
	    t_est = LinearTimeEst( twin_p, 4.0 );
	    do_test = (twin_p->t > 1.1 * t_est);
	}
	MPI_Bcast( &do_test, 1, MPI_INT, 0, MPI_COMM_WORLD );
	if (do_test) {
	    n_smoothed += RunTest( twin_p, CommTest, msgctx, gwtick );
	}
	twin_p = twin_p->next;
    }
    MPI_Bcast( &n_smoothed, 1, MPI_INT, 0, MPI_COMM_WORLD );
    return n_smoothed;
}

/* Output the results using the chosen graphics output package */
void OutputTestList( TwinResults *twin, void *outctx, int proc1, int proc2, 
		     int distance )
{
    TwinResults *twin_p = twin;
    double rate;

    while (twin_p) {
	if (twin_p->n_loop < 1 || twin_p->ntests < 1) {
	    /* Skip any tests that we could not successfully run */
	    twin_p = twin_p->next;
	    continue;
	}

	/* Compute final quantities */
	if (twin_p->t > 0) 
	    rate = ((double)twin_p->len) / twin_p->t;
	else
	    rate = 0.0;

	DataoutGraph( outctx, proc1, proc2, distance, 
		      (int)twin_p->len, twin_p->t * TimeScale,
		      twin_p->t * TimeScale, 
		      rate * RateScale, 
		      twin_p->sum_time / twin_p->ntests * TimeScale, 
		      twin_p->max_time * TimeScale );
	twin_p = twin_p->next;
    }
}

void CheckTimeLimit( void )
{
    /* Check for max time exceeded */
    if (__MYPROCID == 0 && MPI_Wtime() - start_time > max_run_time) {
	fprintf( stderr, "Exceeded %f seconds, aborting\n", max_run_time );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
}


