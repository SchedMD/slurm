/* 
 * Program to test all of the features of MPI_Send and MPI_Recv
 *
 * *** What is tested? ***
 * 1. Sending and receiving all basic types and many sizes - check
 * 2. Tag selectivity - check
 * 3. Error return codes for
 *    a. Invalid Communicator
 *    b. Invalid destination or source
 *    c. Count out of range
 *    d. Invalid type
 */

#include "test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"

#ifdef HAVE_MPICHCONF_H
#include "mpichconf.h"
#endif

static int src = 1;
static int dest = 0;

static int verbose = 0;

#define MAX_TYPES 12
static MPI_Datatype BasicTypes[MAX_TYPES];
#if defined(HAVE_LONG_DOUBLE) && (!defined HAS_XDR)
static int ntypes = 12;
#else
static int ntypes = 11;
#endif

static int maxbufferlen = 10000;
static int stdbufferlen = 300;

/* Prototypes to keep compilers quiet */
void AllocateBuffers ( void **, MPI_Datatype *, int, int );
void FreeBuffers ( void **, int );
void FillBuffers ( void **, MPI_Datatype *, int, int );
int CheckBuffer ( void *, MPI_Datatype, int );
void SetupBasicTypes (void);
void SenderTest1 (void);
void ReceiverTest1 (void);
void SenderTest2 (void);
void ReceiverTest2 (void);
void SenderTest3 (void);
void ReceiverTest3 (void);

void 
AllocateBuffers(void **bufferspace, MPI_Datatype *buffertypes, int num_types, 
		int bufferlen)
{
    int i;
    for (i = 0; i < ntypes; i++) {
	if (buffertypes[i] == MPI_CHAR)
	    bufferspace[i] = malloc(bufferlen * sizeof(char));
	else if (buffertypes[i] == MPI_SHORT)
	    bufferspace[i] = malloc(bufferlen * sizeof(short));
	else if (buffertypes[i] == MPI_INT)
	    bufferspace[i] = malloc(bufferlen * sizeof(int));
	else if (buffertypes[i] == MPI_LONG)
	    bufferspace[i] = malloc(bufferlen * sizeof(long));
	else if (buffertypes[i] == MPI_UNSIGNED_CHAR)
	    bufferspace[i] = malloc(bufferlen * sizeof(unsigned char));
	else if (buffertypes[i] == MPI_UNSIGNED_SHORT)
	    bufferspace[i] = malloc(bufferlen * sizeof(unsigned short));
	else if (buffertypes[i] == MPI_UNSIGNED)
	    bufferspace[i] = malloc(bufferlen * sizeof(unsigned int));
	else if (buffertypes[i] == MPI_UNSIGNED_LONG)
	    bufferspace[i] = malloc(bufferlen * sizeof(unsigned long));
	else if (buffertypes[i] == MPI_FLOAT)
	    bufferspace[i] = malloc(bufferlen * sizeof(float));
	else if (buffertypes[i] == MPI_DOUBLE)
	    bufferspace[i] = malloc(bufferlen * sizeof(double));
#if defined(HAVE_LONG_DOUBLE)  && (!defined HAS_XDR)
	else if (MPI_LONG_DOUBLE && buffertypes[i] == MPI_LONG_DOUBLE) {
	    int dlen;
	    MPI_Type_size( MPI_LONG_DOUBLE, &dlen );
	    bufferspace[i] = malloc(bufferlen * dlen);
	}
#endif
	else if (buffertypes[i] == MPI_BYTE)
	    bufferspace[i] = malloc(bufferlen * sizeof(unsigned char));
    }
}

void 
FreeBuffers(void **buffers, int nbuffers)
{
    int i;
    for (i = 0; i < nbuffers; i++)
	free(buffers[i]);
}

void 
FillBuffers(void **bufferspace, MPI_Datatype *buffertypes, int num_types, 
	    int bufferlen)
{
    int i, j;
    for (i = 0; i < ntypes; i++) {
	for (j = 0; j < bufferlen; j++) {
	    if (buffertypes[i] == MPI_CHAR)
		((char *)bufferspace[i])[j] = (char)(j & 0x7f);
	    else if (buffertypes[i] == MPI_SHORT)
		((short *)bufferspace[i])[j] = (short)j;
	    else if (buffertypes[i] == MPI_INT)
		((int *)bufferspace[i])[j] = (int)j;
	    else if (buffertypes[i] == MPI_LONG)
		((long *)bufferspace[i])[j] = (long)j;
	    else if (buffertypes[i] == MPI_UNSIGNED_CHAR)
		((unsigned char *)bufferspace[i])[j] = (unsigned char)j;
	    else if (buffertypes[i] == MPI_UNSIGNED_SHORT)
		((unsigned short *)bufferspace[i])[j] = (unsigned short)j;
	    else if (buffertypes[i] == MPI_UNSIGNED)
		((unsigned int *)bufferspace[i])[j] = (unsigned int)j;
	    else if (buffertypes[i] == MPI_UNSIGNED_LONG)
		((unsigned long *)bufferspace[i])[j] = (unsigned long)j;
	    else if (buffertypes[i] == MPI_FLOAT)
		((float *)bufferspace[i])[j] = (float)j;
	    else if (buffertypes[i] == MPI_DOUBLE)
		((double *)bufferspace[i])[j] = (double)j;
#if defined(HAVE_LONG_DOUBLE)  && (!defined HAS_XDR)
	    else if (MPI_LONG_DOUBLE && buffertypes[i] == MPI_LONG_DOUBLE)
		((long double *)bufferspace[i])[j] = (long double)j;
#endif
	    else if (buffertypes[i] == MPI_BYTE)
		((unsigned char *)bufferspace[i])[j] = (unsigned char)j;
	}
    }
}

int
CheckBuffer(bufferspace, buffertype, bufferlen)
void *bufferspace; 
MPI_Datatype buffertype; 
int bufferlen;
{
    int j;
    for (j = 0; j < bufferlen; j++) {
	if (buffertype == MPI_CHAR) {
	    if (((char *)bufferspace)[j] != (char)(j & 0x7f))
		return 1;
	} else if (buffertype == MPI_SHORT) {
	    if (((short *)bufferspace)[j] != (short)j)
		return 1;
	} else if (buffertype == MPI_INT) {
	    if (((int *)bufferspace)[j] != (int)j)
		return 1;
	} else if (buffertype == MPI_LONG) {
	    if (((long *)bufferspace)[j] != (long)j)
		return 1;
	} else if (buffertype == MPI_UNSIGNED_CHAR) {
	    if (((unsigned char *)bufferspace)[j] != (unsigned char)j)
		return 1;
	} else if (buffertype == MPI_UNSIGNED_SHORT) {
       	    if (((unsigned short *)bufferspace)[j] != (unsigned short)j)
		return 1;
	} else if (buffertype == MPI_UNSIGNED) {
	    if (((unsigned int *)bufferspace)[j] != (unsigned int)j)
		return 1;
	} else if (buffertype == MPI_UNSIGNED_LONG) {
	    if (((unsigned long *)bufferspace)[j] != (unsigned long)j)
		return 1;
	} else if (buffertype == MPI_FLOAT) {
	    if (((float *)bufferspace)[j] != (float)j)
		return 1;
	} else if (buffertype == MPI_DOUBLE) {
	    if (((double *)bufferspace)[j] != (double)j)
		return 1;
#if defined(HAVE_LONG_DOUBLE)  && (!defined HAS_XDR)
	} else if (MPI_LONG_DOUBLE && buffertype == MPI_LONG_DOUBLE) {
	    if (((long double *)bufferspace)[j] != (long double)j)
		return 1;
#endif
	} else if (buffertype == MPI_BYTE) {
	    if (((unsigned char *)bufferspace)[j] != (unsigned char)j)
		return 1;
	}
    }
    return 0;
}

void SetupBasicTypes( void )
{
    BasicTypes[0] = MPI_CHAR;
    BasicTypes[1] = MPI_SHORT;
    BasicTypes[2] = MPI_INT;
    BasicTypes[3] = MPI_LONG;
    BasicTypes[4] = MPI_UNSIGNED_CHAR;
    BasicTypes[5] = MPI_UNSIGNED_SHORT;
    BasicTypes[6] = MPI_UNSIGNED;
    BasicTypes[7] = MPI_UNSIGNED_LONG;
    BasicTypes[8] = MPI_FLOAT;
    BasicTypes[9] = MPI_DOUBLE;

    /* Define the last few elements as null just in case */
    BasicTypes[11] = MPI_DATATYPE_NULL;
#if defined (HAVE_LONG_DOUBLE) && (!defined HAS_XDR)
    if (MPI_LONG_DOUBLE) {
	BasicTypes[10] = MPI_LONG_DOUBLE;
	BasicTypes[11] = MPI_BYTE;
	}
    else {
	ntypes = 11;
	BasicTypes[10] = MPI_BYTE;
	}
#else
    BasicTypes[10] = MPI_BYTE;
#endif
}

void 
SenderTest1( void )
{
    void *bufferspace[MAX_TYPES];
    int i, j;
    int act_send;
    MPI_Request *requests = 
	(MPI_Request *)malloc(sizeof(MPI_Request) * ntypes * 
			      maxbufferlen/500);
    MPI_Status *statuses = 
	(MPI_Status *)malloc(sizeof(MPI_Status) * ntypes * 
			     maxbufferlen/500);

    AllocateBuffers(bufferspace, BasicTypes, ntypes, maxbufferlen);
    FillBuffers(bufferspace, BasicTypes, ntypes, maxbufferlen);
    act_send = 0;
    for (i = 0; i < ntypes; i++) {
	for (j = 0; j < maxbufferlen; j += 500) {
	    if (BasicTypes[i] == MPI_DATATYPE_NULL) continue;
	    MPI_Isend(bufferspace[i], j, BasicTypes[i], dest, 
		      2000, MPI_COMM_WORLD, 
		      &(requests[act_send++]));
	    }
    }
    MPI_Waitall( act_send, requests, statuses);
    free(requests);
    free(statuses);
    FreeBuffers(bufferspace, ntypes);
}

void
ReceiverTest1( void )
{
    void *bufferspace[MAX_TYPES];
    int i, j;
    char message[81];
    MPI_Status Stat;
    MPI_Request Req;
    int dummy, passed;

    AllocateBuffers(bufferspace, BasicTypes, ntypes, maxbufferlen);
    for (i = 0; i < ntypes; i++) {
	passed = 1;
	/* Try different sized messages */
	for (j = 0; j < maxbufferlen; j += 500) {
	    /* Skip null datatypes */
	    if (!BasicTypes[i]) continue;
	    MPI_Irecv(bufferspace[i], j, BasicTypes[i], src, 
		     2000, MPI_COMM_WORLD, &Req);
	    sprintf(message, "Send-Receive Test, Type %d, Count %d",
		    i, j);
	    MPI_Wait(&Req, &Stat);
	    if (Stat.MPI_SOURCE != src) {
		fprintf(stderr, "*** Incorrect Source returned. ***\n");
		Test_Failed(message);
		passed = 0;
	    } else if (Stat.MPI_TAG != 2000) {	
		fprintf(stderr, "*** Incorrect Tag returned. ***\n");	    
		Test_Failed(message);
		passed = 0;
	    } else if (MPI_Get_count(&Stat, BasicTypes[i], &dummy) ||
		       dummy != j) {
		fprintf(stderr, 
			"*** Incorrect Count returned, Count = %d. ***\n", 
			dummy);
		Test_Failed(message);
		passed = 0;
	    } else if(CheckBuffer(bufferspace[i], BasicTypes[i], j)) {
		fprintf(stderr, "*** Incorrect Message received. ***\n");
		Test_Failed(message);
		passed = 0;
	    } 
	}
	sprintf(message, "Send-Receive Test, Type %d",
		i);
	if (passed) 
	    Test_Passed(message);
	else 
	    Test_Failed(message);
    }
    FreeBuffers(bufferspace, ntypes);
}

/* Test Tag Selectivity */
void 
SenderTest2( void )
{
    int *buffer;
    int i;
    MPI_Request requests[10];
    MPI_Status statuses[10];

    buffer = (int *)malloc(stdbufferlen * sizeof(int));

    for (i = 0; i < stdbufferlen; i++)
	buffer[i] = i;
    
    for (i = 1; i <= 10; i++)
	MPI_Isend(buffer, stdbufferlen, MPI_INT, dest,
		 2000+i, MPI_COMM_WORLD, &(requests[i-1]));
    MPI_Waitall(10, requests, statuses);
    free(buffer);
    
    return;
}

void
ReceiverTest2( void )
{
    int *buffer;
    int i, j;
    char message[81];
    MPI_Status Stat;
    int dummy, passed;

    MPI_Request Req;

    buffer = (int *)malloc(stdbufferlen * sizeof(int));
    passed = 1;

    for (i = 2010; i >= 2001; i--) {
	MPI_Irecv(buffer, stdbufferlen, MPI_INT, src, 
		 i, MPI_COMM_WORLD, &Req);
	sprintf(message, "Tag Selectivity Test, Tag %d",
		i);
	MPI_Wait(&Req, &Stat);
	if (Stat.MPI_SOURCE != src) {
	    fprintf(stderr, "*** Incorrect Source returned. ***\n");
	    Test_Failed(message);
	} else if (Stat.MPI_TAG != i) {	
	    fprintf(stderr, "*** Incorrect Tag returned. ***\n");	    
	    Test_Failed(message);
	} else if (MPI_Get_count(&Stat, MPI_INT, &dummy) ||
		   dummy != stdbufferlen) {
	    fprintf(stderr, 
		    "*** Incorrect Count returned, Count = %d. ***\n", 
		    dummy);
	    Test_Failed(message);
	} else if(CheckBuffer( (void *)buffer, MPI_INT, stdbufferlen)) {
	    fprintf(stderr, "*** Incorrect Message received. ***\n");
	    Test_Failed(message);
	    passed = 0;
	}
	/* Clear out the buffer */
	for (j = 0; j < stdbufferlen; j++)
	    buffer[j] = -1;
    }
    strncpy(message, "Tag Selectivity Test", 81);
    if (passed)
	Test_Passed(message);
    else
	Test_Failed(message);
    free(buffer);
    return;
}

void
SenderTest3( void )
{
    return;
}

void
ReceiverTest3( void )
{
    int buffer[20];
    MPI_Datatype bogus_type = MPI_DATATYPE_NULL;
    MPI_Request Req;
#if 0
    MPI_Status Stat;
    int err_code;
#endif
    if (verbose)
	MPI_Errhandler_set(MPI_COMM_WORLD, TEST_ERRORS_WARN);
    else
	MPI_Errhandler_set(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

    if (MPI_Isend(buffer, 20, MPI_INT, dest,
		 1, MPI_COMM_NULL, &Req) == MPI_SUCCESS){
	Test_Failed("NULL Communicator Test");
    }
    else {
	Test_Passed("NULL Communicator Test");
#if 0
	/* If test passed (i.e. send failed, try waiting on the
	   request... */
	Test_Message("About to wait on failed request.");
	if (MPI_Wait(&Req, &Stat) == MPI_SUCCESS) {;
	    Test_Failed("Wait on failed isend Test");
        }
        else 
	    Test_Passed("Wait on failed isend Test");
	Test_Message("Done waiting on failed request.");
#endif
    }
/*
    if (MPI_Isend(NULL, 10, MPI_INT, dest,
		 1, MPI_COMM_WORLD, &Req) == MPI_SUCCESS){
	Test_Failed("Invalid Buffer Test");
    }
    else
	Test_Passed("Invalid Buffer Test");
*/
   if (MPI_Isend(buffer, -1, MPI_INT, dest,
		 1, MPI_COMM_WORLD, &Req) == MPI_SUCCESS){
	Test_Failed("Invalid Count Test");
    }
    else
	Test_Passed("Invalid Count Test");

   if (MPI_Isend(buffer, 20, bogus_type, dest,
		 1, MPI_COMM_WORLD, &Req) == MPI_SUCCESS){
	Test_Failed("Invalid Type Test");
    }
    else
	Test_Passed("Invalid Type Test");

   if (MPI_Isend(buffer, 20, MPI_INT, dest, 
		 -1, MPI_COMM_WORLD, &Req) == MPI_SUCCESS) {
        Test_Failed("Invalid Tag Test");
    }
    else
	Test_Passed("Invalid Tag Test");

   if (MPI_Isend(buffer, 20, MPI_INT, 300,
		 1, MPI_COMM_WORLD, &Req) == MPI_SUCCESS) {
	Test_Failed("Invalid Destination Test");
    }
    else
	Test_Passed("Invalid Destination Test");
    return;
}

int 
main( int argc, char **argv )
{
    int myrank, mysize;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
    MPI_Comm_size(MPI_COMM_WORLD, &mysize);

    /* dest writes out the received stats; for the output to be
       consistant (with the final check), it should be procees 0 */
    if (argc > 1 && argv[1] && strcmp( "-alt", argv[1] ) == 0) {
	dest = 1;
	src  = 0;
	}
    else {
	src  = 1;
	dest = 0;
	}

    Test_Init("isndrcv", myrank);
    SetupBasicTypes();

    if (mysize != 2) {
	fprintf(stderr, 
		"*** This test program requires exactly 2 processes.\n");
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
    
    /* Turn stdout's buffering to line buffered so it mixes right with
       stderr in output files. (hopefully) */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (myrank == src) {
	SenderTest1();
	SenderTest2();
	SenderTest3();  
    } else if (myrank == dest) {
	ReceiverTest1();
	ReceiverTest2();
	ReceiverTest3(); 
    } else {
	fprintf(stderr, "*** This program uses exactly 2 processes! ***\n");
	exit(-1);
    }
    Test_Waitforall( );
    if (myrank == dest) {
	int rval;
	rval = Summarize_Test_Results();
	Test_Finalize();
	MPI_Finalize();
	return rval;
    }
    else {
	Test_Finalize();
	MPI_Finalize();
	return 0;
    }
}
