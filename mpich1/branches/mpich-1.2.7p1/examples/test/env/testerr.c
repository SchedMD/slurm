#include <stdio.h>
#include "mpi.h"

/* Test error handling.  This is MPICH specific */
void Test_Send( void );
void Test_Recv( void );
void Test_Datatype( void );

void Test_Errors_warn( MPI_Comm *comm, int *code, ... )
{  
  char buf[MPI_MAX_ERROR_STRING+1];
  int  myid, result_len; 
  static int in_handler = 0;

  if (in_handler) return;
  in_handler = 1;
  /* Convert code to message and print */
  MPI_Error_string( *code, buf, &result_len );
  printf( "%s\n", buf );
  in_handler = 0;
}  

static int errcount = 0;
void Test_Failed( char * msg )
{
    printf( "FAILED: %s\n", msg );
    errcount++;
}
void Test_Passed( char * msg )
{
    printf( "Passed: %s\n", msg );
}

int main( int argc, char *argv[] )
{
    MPI_Errhandler TEST_ERRORS_WARN;

    MPI_Init( &argc, &argv );

    MPI_Errhandler_create( Test_Errors_warn, &TEST_ERRORS_WARN );
    MPI_Errhandler_set(MPI_COMM_WORLD, TEST_ERRORS_WARN);

    Test_Send();

    Test_Recv();

    Test_Datatype();

    MPI_Finalize();

    return 0;
}

void Test_Send( void )
{
    int buffer[100];
    int dest;
    MPI_Datatype bogus_type = MPI_DATATYPE_NULL;
    MPI_Status status;
    int myrank, size;
    int large_tag, flag, small_tag;
    int *tag_ubp;

    MPI_Comm_rank( MPI_COMM_WORLD, &myrank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    dest = size - 1;

    if (MPI_Send(buffer, 20, MPI_INT, dest,
		 1, MPI_COMM_NULL) == MPI_SUCCESS){
	Test_Failed("NULL Communicator Test");
    }
    else
	Test_Passed("NULL Communicator Test");

    if (MPI_Send(buffer, -1, MPI_INT, dest,
		 1, MPI_COMM_WORLD) == MPI_SUCCESS){
	Test_Failed("Invalid Count Test");
    }
    else
	Test_Passed("Invalid Count Test");

    if (MPI_Send(buffer, 20, bogus_type, dest,
		 1, MPI_COMM_WORLD) == MPI_SUCCESS){
	Test_Failed("Invalid Type Test");
    }
    else
	Test_Passed("Invalid Type Test");

    small_tag = -1;
    if (small_tag == MPI_ANY_TAG) small_tag = -2;
    if (MPI_Send(buffer, 20, MPI_INT, dest, 
		 small_tag, MPI_COMM_WORLD) == MPI_SUCCESS) {
        Test_Failed("Invalid Tag Test");
    }
    else
	Test_Passed("Invalid Tag Test");

    /* Form a tag that is too large */
    MPI_Attr_get( MPI_COMM_WORLD, MPI_TAG_UB, (void **)&tag_ubp, &flag );
    if (!flag) Test_Failed("Could not get tag ub!" );
    large_tag = *tag_ubp + 1;
    if (large_tag > *tag_ubp) {
	if (MPI_Send(buffer, 20, MPI_INT, dest, 
		     -1, MPI_COMM_WORLD) == MPI_SUCCESS) {
	    Test_Failed("Invalid Tag Test");
	    }
	else
	    Test_Passed("Invalid Tag Test");
	}

    if (MPI_Send(buffer, 20, MPI_INT, 300,
		 1, MPI_COMM_WORLD) == MPI_SUCCESS) {
	Test_Failed("Invalid Destination Test");
    }
    else
	Test_Passed("Invalid Destination Test");

    if (MPI_Send((void *)0, 10, MPI_INT, dest,
		 1, MPI_COMM_WORLD) == MPI_SUCCESS){
	Test_Failed("Invalid Buffer Test (send)");
    }
    else
	Test_Passed("Invalid Buffer Test (send)");
}

void Test_Recv( void )
{
}

void Test_Datatype( void )
{
}
    
#ifdef FOO
void
ReceiverTest3()
{
    int buffer[20];
    MPI_Datatype bogus_type = MPI_DATATYPE_NULL;
    MPI_Status status;
    int myrank;
    int *tag_ubp;
    int large_tag, flag, small_tag;

    MPI_Comm_rank( MPI_COMM_WORLD, &myrank );

    if (myrank == 0) {
	fprintf( stderr, 
"There should be eight error messages about invalid communicator\n\
count argument, datatype argument, tag, rank, buffer send and buffer recv\n" );
	}

    /* A receive test might not fail until it is triggered... */
    if (MPI_Recv((void *)0, 10, MPI_INT, src,
		 15, MPI_COMM_WORLD, &status) == MPI_SUCCESS){
	Test_Failed("Invalid Buffer Test (recv)");
    }
    else
	Test_Passed("Invalid Buffer Test (recv)");

    /* Just to keep things happy, see if there is a message to receive */
    { int flag, ibuf[10];

    MPI_Iprobe( src, 15, MPI_COMM_WORLD, &flag, &status );
    if (flag) 
	MPI_Recv( ibuf, 10, MPI_INT, src, 15, MPI_COMM_WORLD, &status );
    }
    return;
#endif
