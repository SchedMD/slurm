/*
 * Program to test that the "no overtaking messages" semantics
 * of point to point communications in MPI is satisfied. 
 * A long message is sent using MPI_BSend and received using MPI_Recv,
 * followed by lots of short messages sent the same way.
 *
 *				Patrick Bridges
 *				bridges@mcs.anl.gov
 *				patrick@CS.MsState.Edu
 */

#include <stdio.h>
/* Needed for malloc declaration */
#include <stdlib.h>
#include "test.h"
#include "mpi.h"

#define SIZE 10000

static int src  = 0;
static int dest = 1;

/* Which tests to perform (not yet implemented) */
/* static int Do_Buffer = 1; */
/* static int Do_Standard = 1; */

/* Prototypes for picky compilers */
void Generate_Data ( double *, int );
void Normal_Test_Recv ( double *, int );
void Buffered_Test_Send ( double *, int );
void Buffered_Test_Ibsend ( double *, int );
int Check_Data ( double *, int );
void Clear_Buffer ( double *, int );

void Generate_Data(buffer, buff_size)
double *buffer;
int buff_size;
{
    int i;

    for (i = 0; i < buff_size; i++)
	buffer[i] = (double)i+1;
}

void Normal_Test_Recv(buffer, buff_size)
double *buffer;
int buff_size;
{
    int i, j;
    MPI_Status Stat;
    double     *b;

    b = buffer;
    for (j = 0; j < 2; j++) {
	/* Receive a long message */
	MPI_Recv(b, (buff_size/2 - 10), MPI_DOUBLE, src, 
		 2000, MPI_COMM_WORLD, &Stat);
	b += buff_size/2 - 10;
	/* Followed by 10 short ones */
	for (i = 0; i < 10; i++) {
	    MPI_Recv(b++, 1, MPI_DOUBLE, src, 2000, MPI_COMM_WORLD, &Stat);
	}
    }
}

void Buffered_Test_Send(buffer, buff_size)
double *buffer;
int buff_size;
{
    int i, j;
    void *bbuffer;
    int  size;

    for (j = 0; j < 2; j++) {
	/* send a long message */
	MPI_Bsend(buffer, (buff_size/2 - 10), MPI_DOUBLE, dest, 2000, 
		 MPI_COMM_WORLD);
	buffer += buff_size/2 - 10;
	/* Followed by 10 short ones */
	for (i = 0; i < 10; i++)
	    MPI_Bsend(buffer++, 1, MPI_DOUBLE, 
		      dest, 2000, MPI_COMM_WORLD);
        /* Force this set of Bsends to complete */
        MPI_Buffer_detach( &bbuffer, &size );
        MPI_Buffer_attach( bbuffer, size );
    }
}

void Buffered_Test_Ibsend(buffer, buff_size)
double *buffer;
int buff_size;
{
    int i, j;
    void *bbuffer;
    int  size;
    int  cnt;
    MPI_Request req[20];
    MPI_Status  statuses[20];

    for (j = 0; j < 2; j++) {
	/* send a long message */
	cnt = 0;
	MPI_Ibsend(buffer, (buff_size/2 - 10), MPI_DOUBLE, dest, 2000, 
		 MPI_COMM_WORLD, &req[cnt++]);
	buffer += buff_size/2 - 10;
	/* Followed by 10 short ones */
	for (i = 0; i < 10; i++)
	    MPI_Ibsend(buffer++, 1, MPI_DOUBLE, 
		      dest, 2000, MPI_COMM_WORLD, &req[cnt++]);
	/* Wait for these to finish (should finish immediately) */
	MPI_Waitall( cnt, req, statuses );

        /* Force this set of Bsends to complete; this may take longer than
	   the Waitall */
        MPI_Buffer_detach( &bbuffer, &size );
        MPI_Buffer_attach( bbuffer, size );
    }
}

int Check_Data(buffer, buff_size)
double *buffer;
int buff_size;
{
    int i;
    int err = 0;

    for (i = 0; i < buff_size; i++)
	if (buffer[i] != (i + 1)) {
	    err++;
	    fprintf( stderr, "Value at %d is %f, should be %f\n", i, 
		    buffer[i], (double)(i+1) );
	    if (err > 10) return 1;
	    }
    return err;
}

void Clear_Buffer(buffer, buff_size)
double *buffer;
int buff_size;
{
    int i;
    for (i = 0; i < buff_size; i++)
	buffer[i] = -1;
}


int main(int argc, char **argv)
{
    int rank; /* My Rank (0 or 1) */
    double buffer[SIZE], *tmpbuffer, *tmpbuf;
    int tsize, bsize;
    char *Current_Test = NULL;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == src) { 
	Generate_Data(buffer, SIZE);
	MPI_Pack_size( SIZE, MPI_DOUBLE, MPI_COMM_WORLD, &bsize );
	tmpbuffer = (double *) malloc( bsize + 22*MPI_BSEND_OVERHEAD );
	if (!tmpbuffer) {
	    fprintf( stderr, "Could not allocate bsend buffer of size %d\n",
		     bsize );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	    }
        MPI_Buffer_attach( tmpbuffer, bsize + 22*MPI_BSEND_OVERHEAD );
	Buffered_Test_Send(buffer, SIZE);
	Buffered_Test_Ibsend(buffer, SIZE);
	MPI_Buffer_detach( &tmpbuf, &tsize );
	Test_Waitforall( );
	MPI_Finalize();

    } else if (rank == dest) {
	Test_Init("bsendtest", rank);
	/* Test 3 */
	Current_Test = "Overtaking Test (Buffered Send -> Normal Receive)";
	Clear_Buffer(buffer, SIZE);
	/* For Bsend */
	Normal_Test_Recv(buffer, SIZE);
	if (Check_Data(buffer, SIZE))
	    Test_Failed(Current_Test);
	else
	    Test_Passed(Current_Test);

	/* For Ibsend */
	Current_Test = "Overtaking Test (Buffered Isend -> Normal Receive)";
	Clear_Buffer(buffer, SIZE);
	Normal_Test_Recv(buffer, SIZE);
	if (Check_Data(buffer, SIZE))
	    Test_Failed(Current_Test);
	else
	    Test_Passed(Current_Test);

	Test_Waitforall( );
	{
	    int rval = Summarize_Test_Results(); /* Returns number of tests;
						    that failed */
	    Test_Finalize();
	    MPI_Finalize();
	    return rval;
	}
    } else {
	fprintf(stderr, "*** This program uses exactly 2 processes! ***\n");
        MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    return 0;
}



