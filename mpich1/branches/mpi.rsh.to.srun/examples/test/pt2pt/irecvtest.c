/*
 * Program to test that the "no overtaking messages" semantics
 * of point to point communications in MPI is satisfied, 
 * for a simple send/irecv operation.
 *
 * Derived from a program written by 
 *				Patrick Bridges
 *				bridges@mcs.anl.gov
 *				patrick@CS.MsState.Edu
 */

#include <stdio.h>
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
void Normal_Test_Send ( double *, int );
void Async_Test_Recv ( double *, int );
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

#define NSHORT 10
void Normal_Test_Send(buffer, buff_size)
double *buffer;
int buff_size;
{
    int i, j;

    for (j = 0; j < 2; j++) {
	/* send a long message */
	MPI_Send(buffer, (buff_size/2 - NSHORT), MPI_DOUBLE, dest, 2000, 
		 MPI_COMM_WORLD);
	buffer += buff_size/2 - NSHORT;
	/* Followed by NSHORT short ones */
	for (i = 0; i < NSHORT; i++)
	    MPI_Send(buffer++, 1, MPI_DOUBLE, dest, 2000, MPI_COMM_WORLD);
    }
}

void Async_Test_Recv(buffer, buff_size)
double *buffer;
int buff_size;
{
    int i, j, req = 0;
    MPI_Status Stat[22];
    MPI_Request Hand[22];
    
    for (j = 0; j < 2; j++) {
	/* Receive a long message */
	MPI_Irecv(buffer, (buff_size/2 - NSHORT), MPI_DOUBLE, src, 
		 2000, MPI_COMM_WORLD, &(Hand[req++]));
	buffer += buff_size/2 - NSHORT;
	/* Followed by NSHORT short ones */
	for (i = 0; i < NSHORT; i++)
	    MPI_Irecv(buffer++, 1, MPI_DOUBLE, src, 2000, 
		      MPI_COMM_WORLD, &(Hand[req++]));
    }
    MPI_Waitall(req, Hand, Stat);
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
	    fflush( stderr );
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


int main( int argc, char **argv)
{
    int rank; /* My Rank (0 or 1) */
    double buffer[SIZE];
    char *Current_Test = NULL;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == src) { 
	Generate_Data(buffer, SIZE);
	Normal_Test_Send(buffer, SIZE);
	Test_Waitforall( );
	MPI_Finalize();

    } else if (rank == dest) {
	Test_Init("irecvtest", rank);
	/* Test 2 */
	Clear_Buffer(buffer, SIZE);
	Current_Test = "Overtaking Test (Normal Send   ->  Async Receive)";
	Async_Test_Recv(buffer, SIZE);
	if (Check_Data(buffer, SIZE))
	    Test_Failed(Current_Test);
	else
	    Test_Passed(Current_Test);
	Test_Waitforall( );

	MPI_Finalize();
	{
	    int rval = Summarize_Test_Results(); /* Returns number of tests;
						    that failed */
	    Test_Finalize();
	    return rval;
	}
    } else {
	fprintf(stderr, "*** This program uses exactly 2 processes! ***\n");
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    return 0;
}



