int __NUMNODES, __MYPROCID  ;

/*
   This program is intended to be used in testing the SCALABILITY of the
   collective operations as a function of the number of processes;
   mpptest looks at the SIZE of the messages.
 */
#include <stdio.h>

#include "mpi.h"
#include "mpptest.h"
#include "getopts.h"

extern int __NUMNODES, __MYPROCID;


#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifndef DEFAULT_REPS
#define DEFAULT_REPS 50
#endif

double (*GetGOPFunction(int*, char *[], char *, char *))(int,int,void *);
void RunAGOPTest( int len, int *Len1, int *Len2, double *T1, double *T2, 
		  int *reps, double (*f)(int,int,void*), 
		  int myproc, void *outctx, void *msgctx );
void time_gop_function( int reps, int first, int last, int incr, 
			double (*f)(int,int,void*),
			void *outctx, void *msgctx);
double RunSingleGOPTest( double (*f)(int,int,void*), 
			 int reps, int len, void *msgctx );
int PrintHelp( char *argv[] );
int ComputeGoodReps( double t1, int len1, double t2, int len2, int len );
int GetRepititions( double, double, int, int, int, int );

/* These statics (globals) are used to estimate the parameters in the
   basic (s + rn) complexity model */
static double sumtime = 0.0, sumlentime = 0.0;
static double sumlen  = 0,  sumlen2 = 0;
static double sumtime2 = 0.0;
static int    ntest   = 0;

/* If doinfo is 0, don't write out the various text lines */
static int    doinfo   = 1;
/* Having separate head and tail commands allows us to make multiple  
   runs and concatenate the output */
static int    doheader = 1;
static int    dotail   = 1;

/* Scaling of time and rate */
static double TimeScale = 1.0;
static double RateScale = 1.0;

/* This is the number of times to run a test, taking as time the minimum
   achieve timing.  This uses an adaptive approach that also stops when
   minThreshTest values are within a few percent of the current minimum */
static int    minreps       = 30;
static int    minThreshTest = 3;
static double repsThresh    = 0.05;
static int    NatThresh     = 3;
char   test_name[256];
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
/* If less than Tgoalmin is spent, increase the number of repititions */
static double TgoalMin = 0.5;
static int    AutoReps = 0;

/* This structure allows a collection of arbitray sizes to be specified */
#define MAX_SIZE_LIST 256
static int sizelist[MAX_SIZE_LIST];
static int nsizes = 0;

int main( int argc, char *argv[])
{
    double (* f)(int,int,void*);
    void *MsgCtx = 0; /* This is the context of the message-passing operation */
    void *outctx;
    int  reps;
    int  first,last,incr, svals[3];
    char     units[32];         /* Name of units of length */

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &__NUMNODES );
    MPI_Comm_rank( MPI_COMM_WORLD, &__MYPROCID );
    strcpy( units, "" );

    if (SYArgHasName( &argc, argv, 1, "-help" )) {
	return PrintHelp( argv );
    }

    if (__NUMNODES < 2) {
	fprintf( stderr, "Must run goptest with at least 2 nodes\n" );
	return 1;
    }

/* Get the output context */
    outctx = SetupGraph( &argc, argv );
    if (SYArgHasName( &argc, argv, 1, "-noinfo" ))    doinfo    = 0;
    if (SYArgHasName( &argc, argv, 1, "-nohead" ))    doheader  = 0;
    if (SYArgHasName( &argc, argv, 1, "-notail" ))    dotail    = 0;

    reps          = DEFAULT_REPS;
    if (SYArgHasName( &argc, argv, 0, "-sync") ) {
	svals[0] = svals[1] = svals[2] = 0;
    }
    else {
	/* We use fewer values because we are generating them on the same line. */
	svals[0]      = 0;
	svals[1]      = 1024;
	svals[2]      = 256;
    }

    SYArgGetIntVec( &argc, argv, 1, "-size", 3, svals );
    nsizes = SYArgGetIntList( &argc, argv, 1, "-sizelist", MAX_SIZE_LIST, 
			      sizelist );
/* We ALWAYS use sizelist */
    if (nsizes == 0) {
	/* Generate the size list from the svals list */
	sizelist[0] = svals[0];
	for (nsizes=1; sizelist[nsizes-1] < svals[1] && nsizes<MAX_SIZE_LIST; 
	     nsizes++) {
	    sizelist[nsizes] = sizelist[nsizes-1] + svals[2];
	}
	if (sizelist[nsizes] > svals[1]) nsizes--;
    }

    SYArgGetInt(    &argc, argv, 1, "-reps", &reps );
    if (SYArgHasName( &argc, argv, 1, "-autoreps" ))  AutoReps  = 1;
    if (SYArgGetDouble( &argc, argv, 1, "-tgoal", &Tgoal )) {
	AutoReps = 1;
	if (TgoalMin > 0.1 * Tgoal) TgoalMin = 0.1 * Tgoal;
    }
    SYArgGetDouble( &argc, argv, 1, "-rthresh", &repsThresh );

    f      = GetGOPFunction( &argc, argv, test_name, units );
    MsgCtx = GOPInit( &argc, argv );
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

    if (doinfo && doheader &&__MYPROCID == 0) {
	HeaderForGopGraph( outctx, test_name, (char *)0, units );
    }
    time_gop_function(reps,first,last,incr,f,outctx,MsgCtx);

/* 
   Generate the "end of page".  This allows multiple graphs on the
   same plot 
   */
    if (doinfo && dotail && __MYPROCID == 0) 
	EndPageGraph( outctx );

    MPI_Finalize();
    return 0;
}

/* 
   This is the basic routine for timing an operation.

   Input Parameters:
.  reps - Basic number of times to run basic test (see below)
.  first,last,incr - length of data is first, first+incr, ... last
         (if last != first + k * incr, then actual last value is the 
         value of first + k * incr that is <= last and such that 
         first + (k+1) * incr > last, just as you'd expect)
.  f  -  Routine to call to run a basic test.  This routine returns the time
         that the test took in seconds.
.  outctx -  Pointer to output context
.  msgctx - Context to pass through to operation routine
 */
void time_gop_function( int reps, int first, int last, int incr, 
			double (*f)(int,int,void*),
			void *outctx, void *msgctx)
{
    int    len, myproc, np;
    double s, r;
    double T1, T2;
    int    Len1, Len2;
    int i;

    myproc   = __MYPROCID;
    np       = __NUMNODES;

    /* Run test, using either the simple direct test or the automatic length
       test */
    ntest = 0;
    T1 = 0;
    T2 = 0;
    if(myproc==0) {
	DatabeginForGop( outctx, np );
    }

    for (i=0; i<nsizes; i++) {
	len = sizelist[i];
	RunAGOPTest( len, &Len1, &Len2, &T1, &T2, &reps, f, 
		     myproc, outctx, msgctx );
    }

    /* Generate C.It output */
    if (myproc == 0) {
	DataendForGop( outctx );
	if (doinfo && dotail) {
	    RateoutputGraph( outctx, 
			     sumlen, sumtime, sumlentime, sumlen2, sumtime2, 
			     ntest, &s, &r );
	    DrawGraphGop( outctx, first, last, s, r, nsizes, sizelist );
	}
    }
}



/*****************************************************************************
   Utility routines
 *****************************************************************************/

/* This runs a test for a given length.
   The output is formatted differently from mpptest; we put times on the
   same line for each length of message.
 */
void RunAGOPTest( int len, int *Len1, int *Len2, double *T1, double *T2, 
		  int *reps, double (*f)(int,int,void*), 
		  int myproc, void *outctx, void *msgctx ) 
{
    double mean_time, t, rate;
    double tmean = 0.0, tmax = 0.0;

    if (AutoReps) {
	*reps = GetRepititions( *T1, *T2, *Len1, *Len2, len, *reps );
    }
    t = RunSingleGOPTest( f, *reps, len, msgctx );
    mean_time = t;
    mean_time = mean_time / *reps;  /* take average over trials */
    if (mean_time > 0.0) 
	rate      = ((double)len)/mean_time;
    else 
	rate      = 0.0;
    if(myproc==0) {
	DataoutGraphForGop( outctx, len, 
			    t * TimeScale, mean_time * TimeScale, 
			    rate * RateScale, tmean, tmax );
    }

    *T1   = *T2;
    *Len1 = *Len2;
    *T2   = mean_time;
    *Len2 = len;
}

/*
   This routine computes a good number of repititions to use based on 
   previous computations
 */
int ComputeGoodReps( double t1, int len1, double t2, int len2, int len )
{
    double s, r;
    int    reps;

    r = (t2 - t1) / (len2 - len1);
    s = t1 - r * len1;

    if (s <= 0.0) s = 0.0;
    reps = Tgoal / (s + r * len );
 
    if (reps < 1) reps = 1;

/*
  printf( "Reps = %d (%d,%d,%d)\n", reps, len1, len2, len ); fflush( stdout );
  */
    return reps;
}


/*
  This runs the tests for a single size.  It adapts to the number of 
  tests necessary to get a reliable value for the minimum time.
  It also keeps track of the average and maximum times (which are unused
  for now).

  We can estimate the variance of the trials by using the following 
  formulas:

  variance = (1/N) sum (t(i) - (s+r n(i))**2
           = (1/N) sum (t(i)**2 - 2 t(i)(s + r n(i)) + (s+r n(i))**2)
	   = (1/N) (sum t(i)**2 - 2 s sum t(i) - 2 r sum t(i)n(i) + 
	      sum (s**2 + 2 r s n(i) + r**2 n(i)**2))
  Since we compute the parameters s and r, we need only maintain
              sum t(i)**2
              sum t(i)n(i)
              sum n(i)**2
  We already keep all of these in computing the (s,r) parameters; this is
  simply a different computation.

  In the case n == constant (that is, inside a single test), we can use
  a similar test to estimate the variance of the individual measurements.
  In this case, 

  variance = (1/N) sum (t(i) - s**2
           = (1/N) sum (t(i)**2 - 2 t(i)s + s**2)
	   = (1/N) (sum t(i)**2 - 2 s sum t(i) + sum s**2)
  Here, s = sum t(i)/N
  (For purists, the divison should be slightly different from (1/N) in the
  variance formula.  I'll deal with that later.)

 */
double RunSingleGOPTest( double (*f)(int,int,void*), int reps, int len, 
			 void *msgctx )
{
    int    flag, k, iwork, natmin;
    double t, tmin, mean_time, tmax, tsum;


    flag   = 0;
    tmin   = 1.0e+38;
    tmax   = tsum = 0.0;
    natmin = 0;

    for (k=0; k<minreps; k++) {
	t = (* f) (reps,len,msgctx);
	if (__MYPROCID == 0) {
	    tsum += t;
	    if (t > tmax) tmax = t;
	    if (t < tmin) {
		tmin   = t;
		natmin = 0;
	    }
	    else if (minThreshTest < k && tmin * (1.0 + repsThresh) > t) {
		/* This time is close to the minimum; use this to decide
		   that we've gotten close enough */
		natmin++;
		if (natmin >= NatThresh) 
		    flag = 1;
	    }
	}
	MPI_Allreduce(&flag, &iwork, 1, MPI_INT,MPI_SUM,MPI_COMM_WORLD );
	memcpy(&flag,&iwork,(1)*sizeof(int));;
	if (flag > 0) break;
    }

    mean_time  = tmin / reps;
    sumlen     += len;
    sumtime    += mean_time;
    sumlen2    += ((double)len)*((double)len);
    sumlentime += mean_time * len;
    sumtime2   += mean_time * mean_time;
    ntest      ++;

    return tmin;
}

int PrintHelp( char *argv[] ) 
{
    if (__MYPROCID != 0) return 0;
    fprintf( stderr, "%s - test individual communication speeds\n", argv[0] );
    fprintf( stderr, 
"Test a collective communication by various methods.  The tests are \n\
combinations of\n" );
    fprintf( stderr, 
"  Message sizes:\n\
  -size start end stride                  (default 0 1024 256)\n\
               Messages of length (start + i*stride) for i=0,1,... until\n\
               the length is greater than end.\n\
  -sizelist n1,n2,...\n\
               Messages of length n1, n2, etc are used.  This overrides \n\
               -size\n");

    fprintf( stderr, "\n\
  Number of tests\n\
  -reps n      Number of times message is sent (default %d)\n", DEFAULT_REPS );
    fprintf( stderr, "\
  -autoreps    Compute the number of times a message is sent automatically\n\
  -tgoal  d    Time that each test should take, in seconds.  Use with \n\
               -autoreps\n\
  -rthresh d   Fractional threshold used to determine when minimum time\n\
               has been found.  The default is 0.05.\n\
\n" );
    fprintf( stderr, "\n\
  Output options\n\
  -nohead      Do not print graphics header info\n\
  -notail      Do not print graphics tail info\n\
  -noinfo      Print neither head nor tail\n\
\n" );
    fprintf( stderr, "  -gop [ options ]:\n" );
    PrintGOPHelp();
    PrintGraphHelp();
    return 0;
}

/* 
   Re-initialize the variables used to estimate the time that it
   takes to send data
 */
void ClearTimes( void )
{
    sumtime	   = 0.0;
    sumlentime = 0.0;
    sumlen	   = 0.0;
    sumlen2	   = 0.0;
    sumtime2   = 0.0;
    ntest	   = 0;
}
int GetRepititions( double T1, double T2, int Len1, int Len2, int len, 
		    int reps )
{
    if (__MYPROCID == 0) {
	if (T1 > 0 && T2 > 0) 
	    reps = ComputeGoodReps( T1, Len1, T2, Len2, len );
    }
    MPI_Bcast(&reps, 1, MPI_INT, 0, MPI_COMM_WORLD );
    return reps;
}
