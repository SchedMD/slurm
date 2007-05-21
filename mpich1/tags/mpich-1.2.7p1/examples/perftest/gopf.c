#include <stdio.h>

#include "mpi.h"
#include "mpptest.h"
#include "getopts.h"

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <string.h>
/*****************************************************************************

  Each collection of test routines contains:

  Initialization function (returns pointer to context to pass back to test
  functions

  Routine to return test function (and set parameters) based on command-line
  arguments

  Routine to output "help" text

 *****************************************************************************/

/* Forward ref prototypes */
void *malloc_check(size_t sz);
double
measure_latency(int reps, int root_proc, int proc, int my_pid);
double
measure_oper_latency_in_bcast(int len, int reps, int root_proc, int proc,
                              int my_pid);

/*****************************************************************************
 
 Here are the test functions for the collective (global) operations

 This is designed to allow testing of both "native" (vendor-supplied) and 
 Chameleon collective operations.  Note that Chameleon supports subsets of
 processes, which most vendor (non-MPI) systems do not.
 *****************************************************************************/

/* Allocate another item to ensure that nitem of 0 works */
#define GETMEM(type,nitem,p) {\
                          p = (type *)malloc((unsigned)((nitem+1) * sizeof(type) )) ;\
				  if (!p)return 0;;}
#define SETMEM(nitem,p) {int _i; for (_i=0; _i<nitem; _i++) p[_i] = 0; }

void *GOPInit( int *argc, char **argv )
{
    GOPctx *new;
    char psetname[51];

    new = (GOPctx  *)malloc(sizeof(GOPctx));   if (!new)return 0;;
    new->pset = MPI_COMM_WORLD;
    new->src  = 0;

    if (SYArgGetString( argc, argv, 1, "-pset", psetname, 50 )) {
	int range[3];
	MPI_Group group, world_group;
	sscanf( psetname, "%d-%d", &range[0], &range[1] );
	range[2] = 1;
	MPI_Comm_group( MPI_COMM_WORLD, &world_group );
	MPI_Group_range_incl( world_group, 1, &range, &group );
	MPI_Group_free( &world_group );
	MPI_Comm_create( MPI_COMM_WORLD, group, &new->pset );
	MPI_Group_free( &group );
    }
    return new;
}

typedef enum { GDOUBLE, GFLOAT, GINT, GCHAR } GDatatype;
typedef enum { 
    GOPSUM, GOPMIN, GOPMAX, GOPSYNC, GOPBCAST, GOPBCASTALT, 
    GOPCOL, GOPCOLX } GOperation; 

double TestGDSum( int, int, GOPctx * ), 
       TestGISum( int, int, GOPctx * ), 
       TestGCol( int, int, GOPctx * ), 
       TestGColx( int, int, GOPctx * ), 
       TestGScat( int, int, GOPctx * ), 
       TestGScatAlt( int, int, GOPctx * ),
       TestGSync( int, int, GOPctx * );
double TestGDSumGlob( int, int, GOPctx * ), 
       TestGISumGlob( int, int, GOPctx * ), 
       TestGColGlob( int, int, GOPctx * ), 
       TestGColxGlob( int, int, GOPctx * ),
       TestGScatGlob( int, int, GOPctx * ), 
       TestGSyncGlob( int, int, GOPctx * );

/* Determine the function from the arguments */
double ((*GetGOPFunction( int *argc, char *argv[], char *test_name, char *units )) (int,int,void*))
{
    GOperation op    = GOPSYNC;
    GDatatype  dtype = GDOUBLE;
    double     (*f)(int,int,GOPctx *);

/* Default choice */
    strcpy( test_name, "sync" );

/* Get information on the actual problem to run */

/* Choose the operations */
    if (SYArgHasName( argc, argv, 1, "-dsum" )) {
	op    = GOPSUM;
	dtype = GDOUBLE;
	strcpy( test_name, "dsum" );
	strcpy( units, "(doubles)" );
    }
    if (SYArgHasName( argc, argv, 1, "-isum" )) {
	op    = GOPSUM;
	dtype = GINT;
	strcpy( test_name, "isum" );
	strcpy( units, "(ints)" );
    }
    if (SYArgHasName( argc, argv, 1, "-sync" )) {
	op    = GOPSYNC;
	strcpy( test_name, "sync" );
    }
    if (SYArgHasName( argc, argv, 1, "-scatter" ) ||
	SYArgHasName( argc, argv, 1, "-bcast" )) {
	op    = GOPBCAST;
	dtype = GINT;
	strcpy( test_name, "scatter" );
	strcpy( units, "(ints)" );
    }
    if (SYArgHasName( argc, argv, 1, "-bcastalt" )) {
	op    = GOPBCASTALT;
	dtype = GINT;
	strcpy( test_name, "Bcast (alternate)" );
	strcpy( units, "(ints)" );
    }
    if (SYArgHasName( argc, argv, 1, "-col" )) {
	op    = GOPCOL;
	dtype = GINT;
	strcpy( test_name, "col" );
	strcpy( units, "(ints)" );
    }
    if (SYArgHasName( argc, argv, 1, "-colx" )) {
	op    = GOPCOLX;
	dtype = GINT;
	strcpy( test_name, "colx" );
	strcpy( units, "(ints)" );
    }
    if (SYArgHasName( argc, argv, 1, "-colxex" )) {
	op    = GOPCOLX;
	dtype = GINT;
	strcpy( test_name, "colxex" );
	strcpy( units, "(ints)" );
    }

/* Convert operation and dtype to routine */
    f = 0;
    switch (op) {
    case GOPSUM:
        switch (dtype) {
	case GDOUBLE: 
	    f = TestGDSum;
	    break;
	case GINT:
	    f = TestGISum;
	    break;
	default:
	    break;
	}
	break;
    case GOPMIN:
    case GOPMAX:
	f = 0;
	break;
    case GOPCOL:
	f = TestGCol;
	break;
    case GOPCOLX:
	f = TestGColx;
	break;
    case GOPBCAST:
	f = TestGScat;
	break;
    case GOPBCASTALT:
	f = TestGScatAlt;
	break;
    case GOPSYNC:
	f = TestGSync;
	break;
    }
    return  (double (*)(int,int,void *)) f;
}

void PrintGOPHelp( void )
{
  fprintf( stderr, "\nCollective Tests:\n" );
  fprintf( stderr, "-dsum     : reduction (double precision)\n" );
  fprintf( stderr, "-isum     : reduction (integer)\n" );
  fprintf( stderr, "-sync     : synchronization (MPI_Barrier)\n" );
  fprintf( stderr, "-colx     : collect with known sizes\n" );
  fprintf( stderr, 
                "-colxex   : collect with known sizes with exchange alg.\n" );
  fprintf( stderr, "-scatter  : scatter\n" );
  fprintf( stderr, "-bcast    : another name for -scatter\n" );
  fprintf( stderr, "-bcastalt : -bcast with a different measurement approach\n" );

/* NOT YET IMPLEMENTED
  fprintf( stderr, "\nCollective test control:\n" );
  fprintf( stderr, 
	  "-pset n-m            : processor set consisting of nodes n to m" );
*/
}

/*****************************************************************************
 Here are the actual routines
 *****************************************************************************/
/* First are the Chameleon versions */
double TestGDSum( int reps, int len, GOPctx *ctx )
{
    int    i;
    double *lval, *work, time;
    double t0, t1;

    GETMEM(double,len,lval);
    GETMEM(double,len,work);
    SETMEM(len,lval);
    SETMEM(len,work);

    MPI_Allreduce(lval, work, len, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD );
    MPI_Barrier(MPI_COMM_WORLD );
    t0=MPI_Wtime();
    for (i=0; i<reps; i++) {
	MPI_Allreduce(lval, work, len, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD );
    }
    t1=MPI_Wtime();
    MPI_Barrier(MPI_COMM_WORLD );
    time = t1-t0;
    MPI_Bcast(&time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD );
    free(lval );
    free(work );
    return time;
}

double TestGISum( int reps, int len, GOPctx *ctx )
{
    int     i;
    int     *lval, *work;
    double  time;
    double t0, t1;

    GETMEM(int,len,lval);
    GETMEM(int,len,work);
    SETMEM(len,lval);
    SETMEM(len,work);

    MPI_Allreduce(lval, work, len, MPI_INT,MPI_SUM,MPI_COMM_WORLD );
    MPI_Barrier(MPI_COMM_WORLD );
    t0=MPI_Wtime();
    for (i=0; i<reps; i++) {
	MPI_Allreduce(lval, work, len, MPI_INT,MPI_SUM,MPI_COMM_WORLD );
    }
    t1=MPI_Wtime();
    MPI_Barrier(MPI_COMM_WORLD );
    time = t1-t0;
    MPI_Bcast(&time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD );
    free(lval );
    free(work );
    return time;
}

double TestGScat( int reps, int len, GOPctx *ctx )
{
    int     i;
    int     root, comm_size;
    int     *lval;
    double  time;
    double t0, t1;

    GETMEM(int,len,lval);
    SETMEM(len,lval);

    MPI_Comm_size( MPI_COMM_WORLD, &comm_size );
    root = 0;
    MPI_Bcast(lval, len, MPI_BYTE, 0, MPI_COMM_WORLD );
    MPI_Barrier(MPI_COMM_WORLD );
    t0=MPI_Wtime();
    for (i=0; i<reps; i++) {
	MPI_Bcast(lval, len, MPI_BYTE, root, MPI_COMM_WORLD );
	root++;
	if (root >= comm_size) root = 0;
    }
    t1=MPI_Wtime();
    MPI_Barrier(MPI_COMM_WORLD );
    time = t1-t0;
    MPI_Bcast(&time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD );
    free(lval );
    return time;
}

double TestGColx( reps, len, ctx )
int    reps, len;
GOPctx *ctx;
{
    fprintf( stderr, "gcolx not supported\n" );
    MPI_Abort( MPI_COMM_WORLD, 1 );
    return -1.0;
}
double TestGCol( reps, len, ctx )
int    reps, len;
GOPctx *ctx;
{
    fprintf( stderr, "gcol not supported\n" );
    MPI_Abort( MPI_COMM_WORLD, 1 );
    return -1.0;
}

double TestGSync( int reps, int len, GOPctx *ctx )
{
    int     i;
    double  time;
    double t0, t1;
    
    MPI_Barrier(MPI_COMM_WORLD );
    t0=MPI_Wtime();
    for (i=0; i<reps; i++) {
	MPI_Barrier(MPI_COMM_WORLD );
    }
    t1=MPI_Wtime();
    MPI_Barrier(MPI_COMM_WORLD );
    time = t1-t0;
    MPI_Bcast(&time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD );
    return time;
}


/**********************************************************************/
/* implementation of the methodology described in:
 * "Accurately Measuring MPI Broadcasts in a Computational Grid",
 * B. de Supinski and N. Karonis,
 * Proc. 8th IEEE Symp. on High Performance Distributed Computing (HPDC-8)
 * Redondo Beach, CA, August 1999.  */

/**********************************************************************/
/* a utility function to allocate memory and check the returned pointer
 * or exit the program if memory is exhausted.  */
void *malloc_check(size_t sz)
{
   void *ptr = malloc(sz);

   if ( ptr == NULL )
   {
      perror("malloc");
      MPI_Finalize();
      exit(2);
   }
   return ptr;
}

/**********************************************************************/
/* step 1: measure empty-message 1-way
 * latency between root and process_i (root_latency array) */
double
measure_latency(int reps, int root_proc, int proc, int my_pid)
{
   char dummy;
   MPI_Status status;
   double time;

   MPI_Barrier(MPI_COMM_WORLD);
   if ( my_pid == root_proc )
   {
      int rep;

      time = MPI_Wtime();
      for (rep = 0; rep < reps; rep++)
      {
         MPI_Recv(&dummy, 0, MPI_BYTE, proc, 0, MPI_COMM_WORLD, &status);
         MPI_Send(&dummy, 0, MPI_BYTE, proc, 0, MPI_COMM_WORLD);
      }
      time = (MPI_Wtime() - time);   /* division by 'reps' occurs later */
   }
   else if ( my_pid == proc )
   {
      int rep;

      for (rep = 0; rep < reps; rep++)
      {
         MPI_Send(&dummy, 0, MPI_BYTE, root_proc, 0, MPI_COMM_WORLD);
         MPI_Recv(&dummy, 0, MPI_BYTE, root_proc, 0, MPI_COMM_WORLD, &status);
      }
   }

   MPI_Bcast(&time, 1, MPI_DOUBLE, root_proc, MPI_COMM_WORLD);
   return time;
}

/**********************************************************************/
/* step 2: measure the operation latency (oper_lat array) OL_i
 * of broadcast as ACKer */
double
measure_oper_latency_in_bcast(int len, int reps, int root_proc, int proc,
                              int my_pid)
{
   double time;
   int i;
   int acker_tag = 0;
   MPI_Status status;
   char dummy;
   int *lval;

   GETMEM(int, len, lval);
   SETMEM(len, lval);

   MPI_Barrier(MPI_COMM_WORLD);

   if ( my_pid == root_proc )   /* I am the root process */
   {
      /* priming the line */
      MPI_Bcast(lval, len, MPI_INT, root_proc, MPI_COMM_WORLD);
      MPI_Recv(&dummy, 0, MPI_BYTE, proc, acker_tag, MPI_COMM_WORLD, &status);

      /* do the actual measurement */
      time = MPI_Wtime();
      for (i = 0; i < reps; i++)
      {
         MPI_Bcast(lval, len, MPI_INT, root_proc, MPI_COMM_WORLD);
         MPI_Recv(&dummy, 0, MPI_BYTE, proc, acker_tag, MPI_COMM_WORLD,
                  &status);
      }
      time = MPI_Wtime() - time;   /* division by 'reps' occurs later */
   }
   else   /* I am the ACKer or any process other than root */
   {
      if ( my_pid == proc )   /* I am the ACKER */
         for (i = 0; i < reps + 1; i++)   /* "+ 1" because we primed the line */
         {
            MPI_Bcast(lval, len, MPI_INT, root_proc, MPI_COMM_WORLD);
            MPI_Send(&dummy, 0, MPI_BYTE, root_proc, acker_tag, MPI_COMM_WORLD);
         }
      else   /* I am neither root nor the ACKer */
         for (i = 0; i < reps + 1; i++)   /* "+ 1" because we primed the line */
            MPI_Bcast(lval, len, MPI_INT, root_proc, MPI_COMM_WORLD);
   }

   MPI_Bcast(&time, 1, MPI_DOUBLE, root_proc, MPI_COMM_WORLD);

   free(lval);
   return time;
}

/**********************************************************************/
double TestGScatAlt( int reps, int len, GOPctx *ctx )
{
   int proc_num;   /* the number of processes */
   int my_pid;
   double time = 0.;
   int proc;
   /* array of empty-msg 1-way latencies
    * has to be static in order not to re-measure again the same
    * thing each time this function is called! */
   static double *root_latency = NULL;
   int first_time;
   double *oper_lat;   /* operation latencies + acknowledgements */
   double *lower_bound;   /* taking advantage of pipeline effect */

   /* this function needs to be aware of the number of processes...
    * the following is not an efficient way of doing things.
    * That number should be given as a field in GOPctx *ctx.  */
   MPI_Comm_size(MPI_COMM_WORLD, &proc_num);
   MPI_Comm_rank(MPI_COMM_WORLD, &my_pid);

   if ( root_latency == NULL )
   {
      first_time = 1;
      root_latency = (double*) malloc_check(proc_num * sizeof(double));
   }
   else
      first_time = 0;
   oper_lat = (double*) malloc_check(proc_num * sizeof(double));
   lower_bound = (double*) malloc_check(proc_num * sizeof(double));

   for (proc = 0; proc < proc_num; proc++)
   {
      double this_time;

      /* Root does *not* broadcast to itself */
      if ( ctx->src == proc )
         continue;

      /* step 1: for each process, measure empty-message 1-way
       * latency between root and process_i (root_latency array) */
      if ( first_time )
         root_latency[proc] = measure_latency(reps, ctx->src, proc, my_pid);

      /* step 2: for each process, designating each one as ACKer one
       * at a time, measure the operation latency (oper_lat array)
       * OL_i of broadcast as ACKer */
      oper_lat[proc] = measure_oper_latency_in_bcast(len, reps, ctx->src,
                                                     proc, my_pid);
      this_time = oper_lat[proc] - root_latency[proc] / 2.;

      if ( time < this_time )
         time = this_time;
   }
   free(oper_lat);
   free(lower_bound);
   /* "root_latency" can never be freed from memory... */

   MPI_Bcast(&time, 1, MPI_DOUBLE, ctx->src, MPI_COMM_WORLD);

   return time;
}

