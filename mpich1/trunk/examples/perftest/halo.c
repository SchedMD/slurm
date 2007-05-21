#include <stdio.h>

#include "mpi.h"
#include "mpptest.h"
#include "getopts.h"
/* For sqrt */
#include <math.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
/*
 * This file provides a communication pattern similar to "halo" or ghost
 * cell exchanges.
 *
 */

/* Local structure */
#define MAX_PARTNERS 64
typedef enum { WAITALL, WAITANY } HaloWaitKind;
typedef struct _HaloData {
    int          n_partners;
    int          partners[MAX_PARTNERS];
    HaloWaitKind kind;
    int          debug_flag;
} HaloData;

static void halo_set_buffers( int len, HaloData *ctx, 
			      char *sbuffer[], char *rbuffer[] );
static void halo_free_buffers( HaloData *ctx, 
			       char *sbuffer[], char *rbuffer[] );
double halo_nb( int reps, int len, HaloData *ctx );
#ifdef HAVE_MPI_PUT
double halo_put( int reps, int len, HaloData *ctx );
#endif
int HaloSent( int, HaloData * );

/* Set up the initial buffers */
static void halo_set_buffers( int len, HaloData *ctx, 
			      char *sbuffer[], char *rbuffer[] )
{
    int  i;

    if (ctx->debug_flag) { 
	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	printf( "[%d] len = %d, npartners = %d:",
		rank, len, ctx->n_partners );
	for (i=0; i<ctx->n_partners; i++) {
	    printf( ",%d", ctx->partners[i] );
	}
	puts( "" ); fflush( stdout );
    }

    /* Allocate send and receive buffers */
    if (len == 0) len += sizeof(int); 
    for (i=0; i<ctx->n_partners; i++) {
	sbuffer[i] = (char *)malloc( len );
	rbuffer[i] = (char *)malloc( len );
	if (!sbuffer[i] || !rbuffer[i]) {
	    fprintf( stderr, "Could not allocate %d bytes\n", len );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
	memset( sbuffer[i], -1, len );
	memset( rbuffer[i], 0, len );
    }
}
static void halo_free_buffers( HaloData *ctx, 
			       char *sbuffer[], char *rbuffer[] )
{
    int i;

    for (i=0; i<ctx->n_partners; i++) {
	free(sbuffer[i]);
	free(rbuffer[i]);
    }
}

double halo_nb( int reps, int len, HaloData *ctx )
{
    double elapsed_time;
    int    i, j, n_partners, n2_partners;
    double t0, t1;
    MPI_Request req[2*MAX_PARTNERS], *rq;
    MPI_Status  status[2*MAX_PARTNERS];
    char *(sbuffer[MAX_PARTNERS]), *(rbuffer[MAX_PARTNERS]);

    halo_set_buffers( len, ctx, sbuffer, rbuffer );

    elapsed_time = 0;
    n_partners   = ctx->n_partners;
    n2_partners  = 2 * n_partners; 
    MPI_Barrier( MPI_COMM_WORLD );
    t0 = MPI_Wtime();
    for(i=0;i<reps;i++){
	/*printf( "rep %d\n", i ); fflush(stdout);  */
	rq = req;
	for (j=0; j<n_partners; j++) {
	    MPI_Irecv( rbuffer[j], len, MPI_BYTE, ctx->partners[j], i,
		       MPI_COMM_WORLD, rq++ );
	    MPI_Isend( sbuffer[j], len, MPI_BYTE, ctx->partners[j], i, 
		       MPI_COMM_WORLD, rq++ );
	}
	if (ctx->kind == WAITALL) 
	    MPI_Waitall( n2_partners, req, status );
	else {
	    int idx;
	    for (j=0; j<n2_partners; j++) 
		MPI_Waitany( n2_partners, req, &idx, status );
	}
    }
    t1 = MPI_Wtime();
    elapsed_time = t1 - t0;
    /* Use the max since in the non-periodic case, not all processes have the
       same number of partners (and rate is scaled by max # or partners) */
    MPI_Allreduce( &elapsed_time, &t1, 1, MPI_DOUBLE, MPI_MAX, 
		   MPI_COMM_WORLD );
    t1 = elapsed_time;
    halo_free_buffers( ctx, sbuffer, rbuffer );

    return(elapsed_time);
}

#ifdef HAVE_MPI_PUT
double halo_put( int reps, int len, HaloData *ctx )
{
    double  elapsed_time;
    int     i, j, n_partners;
    double  t0, t1;
    char    *sbuffer, *rbuffer;
    MPI_Win win;
    MPI_Aint offset;
    int      alloc_len;

    alloc_len = len * ctx->n_partners;
    if (alloc_len == 0) alloc_len = sizeof(double);

#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    sbuffer = (char *)shmalloc((unsigned)(alloc_len));
    rbuffer = (char *)shmalloc((unsigned)(alloc_len));
#else
    sbuffer = (char *)malloc((unsigned)(alloc_len));
    rbuffer = (char *)malloc((unsigned)(alloc_len));
#endif
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", alloc_len );
	exit(1 );
    }
    MPI_Win_create( rbuffer, alloc_len, 1, MPI_INFO_NULL, MPI_COMM_WORLD, 
		    &win );
    memset( sbuffer, 0, alloc_len );
    memset( rbuffer, 0, alloc_len );

    elapsed_time = 0;
    n_partners   = ctx->n_partners;
    MPI_Barrier( MPI_COMM_WORLD );
    MPI_Win_fence( 0, win );
    t0 = MPI_Wtime();
    for(i=0;i<reps;i++){
	/*printf( "rep %d\n", i ); fflush(stdout);  */
	offset = 0;
	for (j=0; j<n_partners; j++) {
	    if (ctx->partners[j] != MPI_PROC_NULL) {
		/* Fix for broken MPI implementations */
		MPI_Put( sbuffer+offset, len, MPI_BYTE, ctx->partners[j], 
			 offset, len, MPI_BYTE, win );
	    }
	    offset += len;
	}
	MPI_Win_fence( 0, win );
    }
    t1 = MPI_Wtime();
    elapsed_time = t1 - t0;
    /* Use the max since in the non-periodic case, not all processes have the
       same number of partners (and rate is scaled by max # or partners) */
    MPI_Allreduce( &elapsed_time, &t1, 1, MPI_DOUBLE, MPI_MAX, 
		   MPI_COMM_WORLD );
    t1 = elapsed_time;

#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    shfree( sbuffer );
    shfree( rbuffer );
#else
    free(sbuffer );
    free(rbuffer );
#endif
    MPI_Win_free( &win );
    return(elapsed_time);
}
#endif

TimeFunction GetHaloFunction( int *argc_p, char *argv[], void *MsgCtx, 
			      char *title )
{
  HaloData *new;
  int      rank, size;
  int      i, s1;
  int      is_periodic;

  new = (HaloData *)malloc( sizeof(HaloData) );
  if (!new) return 0;

  new->n_partners = 2;
  SYArgGetInt( argc_p, argv, 1, "-npartner", &new->n_partners );

  is_periodic = SYArgHasName( argc_p, argv, 1, "-periodic" );
  if (new->n_partners > MAX_PARTNERS) {
    fprintf( stderr, "Too many halo partners specified (%d); max is %d\n",
	     new->n_partners, MAX_PARTNERS );
  }
  
  new->debug_flag = SYArgHasName( argc_p, argv, 0, "-debug" );
  new->kind = WAITALL;
  if (SYArgHasName( argc_p, argv, 1, "-waitany" )) {
      new->kind = WAITANY;
      sprintf( title, "halo exchange (%d) - waitany", new->n_partners );
  }
#ifdef HAVE_MPI_PUT
  else if (SYArgHasName( argc_p, argv, 0, "-put"  )) {
      sprintf( title, "halo exchange (%d) - put/fence", new->n_partners );
  }
#endif
  else {
      sprintf( title, "halo exchange (%d) - waitall", new->n_partners );
  }

  /* Compute partners.  We assume only exchanges here.  We use a simple rule
     to compute partners: we use
     rank (+-) 1, (+-)sqrt(size), (+-) sqrt(size) (+-)1, */

  MPI_Comm_size( MPI_COMM_WORLD, &size );
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );

  /* This will work as long as # partners is a multiple of 2 */
  if (new->n_partners > 1 && (new->n_partners & 0x1)) {
    fprintf( stderr, "Number of partners must be even\n" );
    return 0;
  }
  if (new->n_partners == 1) {
      if (rank & 0x1) new->partners[0] = rank - 1;
      else            new->partners[0] = rank + 1;
      if (is_periodic) {
	  if (new->partners[0] >= size) 
	      new->partners[0] -= size;
	  else if (new->partners[0] < 0) 
	      new->partners[0] += size;
      }
      else {
	  if (new->partners[0] >= size) 
	      new->partners[0] = MPI_PROC_NULL;
	  else if (new->partners[0] < 0)
	      new->partners[0] = MPI_PROC_NULL;
      }
  }
  else {
      /* First, load up partners with the distance to the partner */
      new->partners[0] = 1;
      new->partners[1] = -1;
      s1 = sqrt((double)size);
      new->partners[2] = s1;
      new->partners[3] = -s1;
      new->partners[4] = s1 + 1;
      new->partners[5] = s1 - 1;
      new->partners[6] = -s1 + 1;
      new->partners[7] = -s1 - 1;

      for (i=0; i<new->n_partners; i++) {
	  if (is_periodic) 
	      new->partners[i] = (rank + new->partners[i] + size) % size;
	  else {
	      int partner;
	      partner = rank + new->partners[i];
	      if (partner >= size || partner < 0) partner = MPI_PROC_NULL;
	      new->partners[i] = partner;
	  }
      }
  }
  *(void **)MsgCtx = (void *)new;

#ifdef HAVE_MPI_PUT
  if (SYArgHasName( argc_p, argv, 1, "-put"  )) {
      return (TimeFunction) halo_put;
  }
#endif
  return (TimeFunction) halo_nb;
}

/* This also needs a function to compute the amount of communication */
int HaloSent( int len, HaloData *ctx )
{
    int i, totlen = 0;

    /* This needs to be adjusted for partners that are PROC_NULL */
    for (i=0; i<ctx->n_partners; i++) {
	if (ctx->partners[i] != MPI_PROC_NULL) totlen += len;
    }
    return totlen;
}

/* This also needs a function to compute the amount of communication */
int GetHaloPartners( void *ctx )
{
    int i, totlen = 0;
    HaloData *hctx = (HaloData *)ctx;

    /* This needs to be adjusted for partners that are PROC_NULL */
    for (i=0; i<hctx->n_partners; i++) {
	if (hctx->partners[i] != MPI_PROC_NULL) totlen ++;
    }
    return totlen;
}

void PrintHaloHelp( void )
{
    fprintf( stderr, "\
   Special options for -halo:\n\
   -npartner n  - Specify the number of partners\n\
   -waitany     - Use a loop over waitany instead of a single waitall\n\
   -periodic    - Use periodic mesh partners\n\
   -debug       - Provide some debugging information\n\
" );
}
