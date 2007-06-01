#include <stdio.h>

#include "mpi.h"
#include "mpptest.h"

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* 
   Test of single process memcpy.
   ctx is ignored for this test.

   Note that the test is run *once* before it is timed.  This
   is very important to ensure that all of the data is paged in and 
   ready to go.  Without this, a large number of repititions are needed
   to ensure that any "cold-start" cost is averaged out (almost) across 
   many operations (i.e., you need a large value of reps otherwise).
*/
double memcpy_rate(int reps, int len, void *ctx)
{
  double elapsed_time;
  int  i;
  char *sbuffer,*rbuffer;
  double t0;

  sbuffer = (char *)malloc(len+4);
  rbuffer = (char *)malloc(len+4);

  memcpy( rbuffer, sbuffer, len );
  t0 = MPI_Wtime();
  for(i=0;i<reps;i++){
      memcpy( rbuffer, sbuffer, len );
  }
  elapsed_time = MPI_Wtime() - t0;

  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

double memcpy_rate_int(int reps, int len, void *ctx)
{
  double elapsed_time;
  int  k, i, ilen;
  int * restrict sbuffer,* restrict rbuffer;
  double t0;

  ilen = 1 + len / sizeof(int);
  sbuffer = (int *)malloc(ilen*sizeof(int));
  rbuffer = (int *)malloc(ilen*sizeof(int));
  ilen--;

  for (k=0; k<ilen; k++)
      rbuffer[k] = sbuffer[k] = 3;
  t0 = MPI_Wtime();
  
  for(i=0;i<reps;i++){
    for (k=0; k<ilen; k++)
      rbuffer[k] = sbuffer[k];
  }
  elapsed_time = MPI_Wtime() - t0;

  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

double memcpy_rate_double(int reps, int len, void *ctx)
{
  double elapsed_time;
  int  k, i, ilen;
  double * restrict sbuffer,* restrict rbuffer;
  double t0;

  ilen = 1 + len / sizeof(double);
  sbuffer = (double *)malloc(ilen*sizeof(double));
  rbuffer = (double *)malloc(ilen*sizeof(double));
  ilen--;

  for (k=0; k<ilen; k++)
      rbuffer[k] = sbuffer[k] = 3.0;
  t0 = MPI_Wtime();
  for(i=0;i<reps;i++){
    for (k=0; k<ilen; k++)
      rbuffer[k] = sbuffer[k];
  }
  elapsed_time = MPI_Wtime() - t0;

  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}
#ifdef HAVE_LONG_LONG
double memcpy_rate_long_long(int reps, int len, void *ctx)
{
  double elapsed_time;
  int  k, i, ilen;
  long long * restrict sbuffer,* restrict rbuffer;
  double t0;

  ilen = 1 + len / sizeof(long long);
  sbuffer = (long long *)malloc(ilen*sizeof(long long));
  rbuffer = (long long *)malloc(ilen*sizeof(long long));
  ilen--;

  for (k=0; k<ilen; k++)
      rbuffer[k] = sbuffer[k] = 3;
  t0 = MPI_Wtime();
  for(i=0;i<reps;i++){
    for (k=0; k<ilen; k++)
      rbuffer[k] = sbuffer[k];
  }
  elapsed_time = MPI_Wtime() - t0;

  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}
#endif

double memcpy_rate_double_vector(int reps, int len, void *ctx)
{
  double elapsed_time;
  int  k, kk, i, ilen;
  int  stride = 24;
  double * restrict sbuffer,* restrict rbuffer;
  double t0;

  ilen = 1 + len / sizeof(double);
  sbuffer = (double *)malloc(ilen*stride*sizeof(double));
  rbuffer = (double *)malloc(ilen*stride*sizeof(double));
  ilen--;

  for (k=0; k<ilen*stride; k++)
      rbuffer[k] = sbuffer[k] = 3.0;
  t0 = MPI_Wtime();
  for(i=0;i<reps;i++){
      kk = 0;
      for (k=0; k<ilen; k++) {
	  rbuffer[kk] = sbuffer[kk];
	  kk += stride;
      }
  }
  elapsed_time = MPI_Wtime() - t0;

  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}
#ifdef HAVE_LONG_LONG
double memcpy_rate_long_long_vector(int reps, int len, void *ctx)
{
  double elapsed_time;
  int  k, kk, i, ilen;
  int  stride = 24;
  long long * restrict sbuffer,* restrict rbuffer;
  double t0;

  ilen = 1 + len / sizeof(long long);
  sbuffer = (long long *)malloc(ilen*stride*sizeof(long long));
  rbuffer = (long long *)malloc(ilen*stride*sizeof(long long));
  ilen--;

  for (k=0; k<ilen*stride; k++)
      rbuffer[k] = sbuffer[k] = 3;
  t0 = MPI_Wtime();
  for(i=0;i<reps;i++){
      kk = 0;
      for (k=0; k<ilen; k++) {
	  rbuffer[kk] = sbuffer[kk];
	  kk += stride; 
      }
  }
  elapsed_time = MPI_Wtime() - t0;

  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}
#endif
