/* This file shows why you can't use simple timers to time latency in 
   a single call.  It does this by measuring the variation in the
   results of the timer call used in the Dongarra so-called benchmark.
 */
#include "mpi.h"
#include <stdio.h>
#include <math.h>

#define MAX_TIMES 16386
static double times[MAX_TIMES];
int main( argc, argv )
int argc;
char **argv;
{
int i;
int ntest = MAX_TIMES;
double minsep, maxsep, avesep, sep, sd, deltasep, mult;
int citoutput = 1, nmatch;

MPI_Init( &argc, &argv );
/* Warm up */
for (i=0; i<ntest; i++) 
    times[i] = MPI_Wtime();

/* Get actual data.  Note that this does nothing for measuring instruction
   cache misses (more microseconds!) */
for (i=0; i<ntest; i++) 
    times[i] = MPI_Wtime();

/* Look at variation */
minsep	= 1.0e6;
maxsep	= 0.0;
avesep	= 0.0;
for (i=1; i<ntest; i++) {
    sep = times[i] - times[i-1];
    if (sep < minsep) minsep = sep;
    if (sep > maxsep) maxsep = sep;
    avesep += sep;
    }
avesep /= (ntest-1);

/* Compute standard deviation of separations in a relatively stable way */
sd = 0.0;
for (i=1; i<ntest; i++) {
    sep = times[i] - times[i-1];
    sd += (sep - avesep) * (sep - avesep);
    }
/* Scale to usec */
sd *= 1.0e+12;
sd = sqrt(sd) / (ntest - 2);

/* An interesting question is whether all (or most) of the separations are 
   multiples of the minsep.  First, find the likely delta sep (time between
   first and second difference measurements)
 */
deltasep = maxsep;
for (i=1; i<ntest; i++) {
    sep = times[i] - times[i-1];
    if (sep > minsep && sep < deltasep) deltasep = sep;
    }
deltasep -= minsep;
/* Now, find out how many separations are multiples of deltasep */
nmatch = 0;
for (i=1; i<ntest; i++) {
    sep = times[i] - times[i-1];
    mult = (sep - minsep) / deltasep;
    if (fabs( mult - (int)mult) < 0.05) nmatch++;
    }

/* Print results */
printf( "#Variance in clock:\n\
#Minimum time between calls: %6.2f usec\n\
#Maximum time between calls: %6.2f usec\n\
#Average time between calls: %6.2f usec\n\
#Standard deviation:        %12.3e\n", 
       minsep * 1.e6, maxsep * 1.e6, avesep * 1.e6, sd );

if (nmatch > ntest/4) {
    printf( "#Apparent resolution of clock is: %6.2f usec\n", 
	    deltasep * 1.0e6 );
    }
printf( 
"# This program should be run multiple times for better understanding\n" );
/* Print C.It graphics stuff if requested */
if (citoutput) {
    for (i=1; i<ntest; i++) {
	sep = times[i] - times[i-1];
	printf( "%f\n", sep * 1.0e6 );
	}
    printf( "hist\nwait\nnew page\n" );
    }
MPI_Finalize();
return 0;
}
