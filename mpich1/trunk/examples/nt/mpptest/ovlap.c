#include <stdio.h>

#include "mpi.h"
#include "mpptest.h"

extern int __NUMNODES, __MYPROCID;


#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
/*****************************************************************************
   These routines attempt to overlap computation with communication.

   
   Only round trip available.  Note that even blocking operations
   may have effective overlap, since all "blocking" refers to is the state
   of the buffer, not whether the message has been delivered.

   Modeling of the overlap
   
   This is much more difficult that modeling the send and receive, in 
   part because two operations can be taking place at the same time.

   The easiest model assumes that the computation takes place only when
   the communication would be waiting.  In this case, the computation is
   free until it uses up the idle time, when it switches to the "usual"
   cost of a floating point computation.  This model, for fixed message
   length n, has constant time for m < Mcrit, and slope given by the 
   floating point speed for the operation. for m > Mcrit.

   A more realistic model assumes that both operations impact the other, 
   without requiring that the sum of the times have any particular
   relationship.  For example, the loads and stores of the two operations
   may take place in each others memory-miss cycles, possible causing 
   both to slow down a little.  In this model, there are two positive slopes
   for the time, with a change at m == Mcrit (that is, at a time when the
   floating point operation has not finished by the time that the message
   has been completely sent).

 *****************************************************************************/

void SetupOverlap( int, OverlapData *),
     OverlapComputation( int, OverlapData *);

void *OverlapInit( int proc1, int proc2, int size )
{
    OverlapData *new;

    new		 = (OverlapData *)malloc(sizeof(OverlapData));   
    if (!new) return 0;;
    new->proc1	 = proc1;
    new->proc2	 = proc2;
    new->MsgSize	 = size;
    new->Overlap1	 = 0;
    new->Overlap2	 = 0;
    new->OverlapSize = 0;
    new->OverlapLen	 = 0;
    new->OverlapPos	 = 0;

    return new;
}

/* Compute floating point lengths adaptively */
void OverlapSizes( int msgsize, int svals[3], void *vctx )
{
    double time_msg, time_float, tmp;
    int    float_len;
    int    saved_msgsize;
    OverlapData *ctx = (OverlapData *)vctx;

    if (msgsize < 0) {
	return;
    }

    saved_msgsize = ctx->MsgSize;
    ctx->MsgSize  = msgsize;
    
/* First, estimate the time to send a message */
    time_msg = round_trip_b_overlap(100,0,ctx) / 100.0;
    MPI_Allreduce(&time_msg, &tmp, 1, MPI_DOUBLE,MPI_MAX,MPI_COMM_WORLD );
    memcpy(&time_msg,&tmp,(1)*sizeof(double));;
/* printf( "Time_msg is %f\n", time_msg );  */
    float_len = msgsize;
    if (float_len <= 0) float_len = 32;
/* Include the time of the message in the test... */
    do {
	float_len *= 2;
	time_float = round_trip_b_overlap(100,float_len,ctx) / 100.0;
	MPI_Allreduce(&time_float, &tmp, 1, MPI_DOUBLE,MPI_MAX,MPI_COMM_WORLD );
	memcpy(&time_float,&tmp,(1)*sizeof(double));;
	/* printf( "Time_float(%d) is %f\n", float_len, time_float );  */
    } while (time_float < 2 * time_msg);
    svals[1]     = float_len;
    svals[2]     = (float_len - svals[0]) / 64;
    ctx->MsgSize = saved_msgsize;
}

/* 
   Nonblocking round trip with overlap.

   Note: unlike the round_trip routines, the "length" in this routine 
   is the number of floating point operations.
 */
double round_trip_nb_overlap( int reps, int len, void *vctx)
{
    double elapsed_time;
    OverlapData *ctx = (OverlapData *)vctx;
    int  i,myproc,
	proc1=ctx->proc1,proc2=ctx->proc2,MsgSize=ctx->MsgSize;
    char *rbuffer,*sbuffer;
    double t0, t1;
    MPI_Request rid, sid;
    MPI_Status  status;

    /* If the MsgSize is negative, just do the floating point computation.
       This allows us to test for cache effects independant of the message
       passing code.  */
    if (MsgSize < 0) {
	SetupOverlap(len,ctx);
	elapsed_time = 0;
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    OverlapComputation(len,ctx);
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
	return elapsed_time;
    }

    myproc = __MYPROCID;
    sbuffer = (char *)malloc(MsgSize);
    rbuffer = (char *)malloc(MsgSize);
    SetupOverlap(len,ctx);
    elapsed_time = 0;
    if(myproc==proc1){
	MPI_Recv(rbuffer,MsgSize,MPI_BYTE,MPI_ANY_SOURCE,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Irecv(rbuffer,MsgSize,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&(rid));
	    MPI_Isend(sbuffer,MsgSize,MPI_BYTE,proc2,1,MPI_COMM_WORLD,&(sid));
	    OverlapComputation(len,ctx);
	    MPI_Wait(&(rid),&status);
	    MPI_Wait(&(sid),&status);
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
    }

    if(myproc==proc2){
	MPI_Irecv(rbuffer,MsgSize,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&(rid));
	MPI_Isend(sbuffer,MsgSize,MPI_BYTE,proc1,0,MPI_COMM_WORLD,&(sid));
	for(i=0;i<reps-1;i++){
	    OverlapComputation(len,ctx);
	    MPI_Wait(&(rid),&status);
	    MPI_Wait(&(sid),&status);
	    MPI_Irecv(rbuffer,MsgSize,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&(rid));
	    MPI_Isend(sbuffer,MsgSize,MPI_BYTE,proc1,1,MPI_COMM_WORLD,&(sid));
	}
	OverlapComputation(len,ctx);
	MPI_Wait(&(rid),&status);
	MPI_Wait(&(sid),&status);
	MPI_Send(sbuffer,MsgSize,MPI_BYTE,proc1,1,MPI_COMM_WORLD);
    }

    free(sbuffer);
    free(rbuffer);
    return(elapsed_time);
}

/* 
   Blocking round trip with overlap.

   Note: unlike the round_trip routines, the "length" in this routine 
   is the number of floating point operations.
 */
double round_trip_b_overlap(int reps, int len, void *vctx)
{
    double elapsed_time;
    OverlapData *ctx = (OverlapData *)vctx;
    int  i,myproc,
	proc1=ctx->proc1,proc2=ctx->proc2,MsgSize=ctx->MsgSize;
    char *rbuffer,*sbuffer;
    MPI_Status status;
    double t0, t1;

    /* If the MsgSize is negative, just do the floating point computation.
       This allows us to test for cache effects independant of the message
       passing code.  */
    if (MsgSize < 0) {
	SetupOverlap(len,ctx);
	elapsed_time = 0;
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    OverlapComputation(len,ctx);
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
	return elapsed_time;
    }

    myproc = __MYPROCID;
    sbuffer = (char *)malloc(MsgSize);
    rbuffer = (char *)malloc(MsgSize);
    SetupOverlap(len,ctx);
    elapsed_time = 0;
    if(myproc==proc1){
	MPI_Recv(rbuffer,MsgSize,MPI_BYTE,MPI_ANY_SOURCE,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Send(sbuffer,MsgSize,MPI_BYTE,proc2,1,MPI_COMM_WORLD);
	    OverlapComputation(len,ctx);
	    MPI_Recv(rbuffer,MsgSize,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&status);
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
    }

    if(myproc==proc2){
	MPI_Send(sbuffer,MsgSize,MPI_BYTE,proc1,0,MPI_COMM_WORLD);
	for(i=0;i<reps;i++){
	    OverlapComputation(len,ctx);
	    MPI_Recv(rbuffer,MsgSize,MPI_BYTE,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&status);
	    MPI_Send(sbuffer,MsgSize,MPI_BYTE,proc1,1,MPI_COMM_WORLD);
	}
    }

    free(sbuffer);
    free(rbuffer);
    return(elapsed_time);
}

/* 
   This is the routine that performs the computation to be overlapped.

   There should be several of these, including

   Ddot (2 reads)
   Daxpy (2 reads, 1 store)

   Sparse versions (do integer operations as well)   

   We make some attempt to minimize cache effects by 
 */
void SetupOverlap( int len, OverlapData *ctx )
{
    int i;
    double *p1, *p2;

    if (ctx->Overlap1) {
	free(ctx->Overlap1);
	free(ctx->Overlap2);
	ctx->Overlap1 = 0;
	ctx->Overlap2 = 0;
    }

/* Convert len to words */
    ctx->OverlapSize = len / sizeof(double);
    if (ctx->OverlapSize > 0) {
	/* Set len to exceed most cache sizes */
	ctx->OverlapLen = ctx->OverlapSize;
	if (ctx->OverlapLen < 65536) ctx->OverlapLen = 65536;
	ctx->Overlap1 = (double *)malloc((unsigned)(ctx->OverlapLen * sizeof(double) ));
	ctx->Overlap2 = (double *)malloc((unsigned)(ctx->OverlapLen * sizeof(double) ));
	if (!ctx->Overlap1 || !ctx->Overlap2) {
	    ctx->Overlap1 = 0;
	    ctx->Overlap2 = 0;
	    fprintf( stderr, 
		     "Error allocating space in SetupOverlap (2x%d bytes)\n",
		     (int)(ctx->OverlapLen * sizeof(double)) );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
    }
    else 
	ctx->OverlapLen = 0;

    p1 = ctx->Overlap1;
    p2 = ctx->Overlap2;
    for (i=0; i<ctx->OverlapLen; i++) {
	p1[i] = 1.0;
	p2[i] = 1.0;
    }
    ctx->OverlapPos = 0;
}

void OverlapComputation( int len, OverlapData *ctx )
{
    int i, n;
    double temp, *p1 = ctx->Overlap1, *p2 = ctx->Overlap2;

    n = ctx->OverlapSize;
    if (n == 0) return;
    ctx->Overlap1[0] = 0.0;
    temp             = 0.0;

/* Cycle through the memory to reduce cache effects */
    if (n + ctx->OverlapPos >= ctx->OverlapLen) 
	ctx->OverlapPos = 0;
    p1		+= ctx->OverlapPos;
    p2		+= ctx->OverlapPos;
    ctx->OverlapPos	+= ctx->OverlapSize;

    for (i=0;i<n;i++) {
	temp += p1[i] * p2[i];
    }

/* Defeat most optimizers from eliminating loop */
    ctx->Overlap1[0] = temp;
}




