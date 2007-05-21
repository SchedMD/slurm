/*
    hvectest2 - test program that sends an array of floats from the first 
                process of a group to the last, using send and recv and the
	        struct datatype for variable length vectors
*/

#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include "test.h"
/* #define SHOWMSG */

/* Prototypes for picky compilers */
void ClearArray ( double *, int, double );
void SetArray ( double *, int );

#ifdef VERBOSE
static int verbose = 1;
#else
static int verbose = 0;
#endif

void ClearArray( a, n, v )
double *a, v;
int    n;
{
    int i;
    for (i=0; i<n; i++) a[i] = v;
}

void SetArray( a, n )
double *a;
int    n;
{
    int i;
    for (i=0; i<n; i++) a[i] = (double)i;
}

/* 
   This test requires that the MPI implementation support predefined 
   MPI_Datatypes in static initializers (i.e., they must be compile time
   constants).  This was voted as a clarification on 4/26/95.
 */
int main( int argc, char **argv )
{
    int rank, size, to, from, tag, count, i;
    int src, dest;
    int st_source, st_tag, st_count;
    int errcnt = 0;
    MPI_Request handle;
    MPI_Status status;
    double data[100];
    MPI_Datatype rowtype;
    static int blens[2] = { 1, 1 };
    static MPI_Datatype types[2] = { MPI_DOUBLE, MPI_UB };
    MPI_Aint displs[2];

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    /* dest writes out the received stats; for the output to be
       consistant (with the final check), it should be procees 0 */
    if (argc > 1 && argv[1] && strcmp( "-alt", argv[1] ) == 0) {
	dest = size - 1;
	src  = 0;
	}
    else {
	src  = size - 1;
	dest = 0;
	}

    displs[0] = 0;
    displs[1] = 10*sizeof(double);
/*
    blens[0]  = 1;
    blens[1]  = 1;
    types[0]  = MPI_DOUBLE;
    types[1]  = MPI_UB;
 */
    MPI_Type_struct( 2, blens, displs, types, &rowtype );
    MPI_Type_commit( &rowtype );
    /* First test: send a row */
    if (rank == src)
    {
	to     = dest;
	count  = 10;
	tag    = 2001;
	SetArray( data, 100 );
	/* Send a row */
	MPI_Send( data, count, rowtype, to, tag, MPI_COMM_WORLD );
#ifdef SHOWMSG
	printf("%d sent", rank );
	for (i = 0; i < 10; i++) printf(" %f",data[i*10]);printf("\n");
#endif
    }

    if (rank == dest)
    {
	tag   = MPI_ANY_TAG;
	count = 10;		
	from  = MPI_ANY_SOURCE;
	
	ClearArray( data, 100, -1.0 );
	MPI_Recv(data, count, MPI_DOUBLE, from, tag, MPI_COMM_WORLD,
		 &status ); 

	st_source = status.MPI_SOURCE;
	st_tag    = status.MPI_TAG;
	MPI_Get_count( &status, MPI_DOUBLE, &st_count );

	if (st_source != src || st_tag != 2001 || st_count != 10 || verbose) {
	    printf( "Status info: source = %d, tag = %d, count = %d\n",
		    st_source, st_tag, st_count );
	}
#ifdef SHOWMSG
	printf( "%d received", rank);
	for (i = 0; i < 10; i++) printf(" %f",data[i]);	printf("\n");
#endif
	for (i = 0; i < 10; i++) if (data[i] != 10*i) { 
	    errcnt++;
	    fprintf( stderr, 
		    "[%d](rcv double) %d'th element = %f, should be %f\n",
		     rank, i, data[i], 10.0*i );
	    }
    }

    /* Second test: receive a column into row */
    if (rank == src)
    {
	to     = dest;
	count  = 10;
	tag    = 2001;
	SetArray( data, 100 );
	/* Send a row */
	/* MPE_Print_datatype_pack_action( stdout, count, 
	                                   MPI_DOUBLE, 0, 0 ); */
	MPI_Send( data, count, MPI_DOUBLE, to, tag, MPI_COMM_WORLD );
#ifdef SHOWMSG
	printf("%d sent", rank );
	for (i = 0; i < 10; i++) printf(" %f",data[i]);printf("\n");
#endif
    }
    if (rank == dest)
    {
	tag   = MPI_ANY_TAG;
	count = 10;		
	from  = MPI_ANY_SOURCE;
	ClearArray( data, 100, -1.0 );
	MPI_Recv(data, count, rowtype, from, tag, MPI_COMM_WORLD,
		 &status ); 
	/* MPE_Print_datatype_unpack_action( stdout, count, rowtype, 0, 0 ); */

	st_source = status.MPI_SOURCE;
	st_tag    = status.MPI_TAG;
	MPI_Get_count( &status, MPI_DOUBLE, &st_count );

	if (st_source != src || st_tag != 2001 || st_count != 10 || verbose) {
	    printf( "Status info: source = %d, tag = %d, count = %d\n",
	        st_source, st_tag, st_count );
	}
#ifdef SHOWMSG
	printf( "%d received", rank);
	for (i = 0; i < 10; i++) printf(" %f",data[i*10]);printf("\n");
#endif
	for (i = 0; i < 10; i++) if (data[i*10] != i) {
	    errcnt++;
	    fprintf( stderr, 
		    "[%d](rcv row) %d'th element = %f, should be %f\n",
		     rank, i, data[i*10], 1.0*i );
	    }
    }

    /* Third test: send AND receive a row */
    if (rank == src)
    {
	to     = dest;
	count  = 10;
	tag    = 2001;
	SetArray( data, 100 );
	/* Send a row */
	MPI_Send( data, count, rowtype, to, tag, MPI_COMM_WORLD );
#ifdef SHOWMSG
	printf("%d sent", rank );
	for (i = 0; i < 10; i++) printf(" %f",data[i*10]);printf("\n");
#endif
    }
    if (rank == dest)
    {
	tag   = MPI_ANY_TAG;
	count = 10;		
	from  = MPI_ANY_SOURCE;
	ClearArray( data, 100, -1.0 );
	MPI_Recv(data, count, rowtype, from, tag, MPI_COMM_WORLD,
		 &status ); 

	st_source = status.MPI_SOURCE;
	st_tag    = status.MPI_TAG;
	MPI_Get_count( &status, MPI_DOUBLE, &st_count );

	if (st_source != src || st_tag != 2001 || st_count != 10 || verbose) {
	    printf( "Status info: source = %d, tag = %d, count = %d\n",
		    st_source, st_tag, st_count );
	}
#ifdef SHOWMSG
	printf( "%d received", rank);
	for (i = 0; i < 10; i++) printf(" %f",data[i*10]);printf("\n");
#endif
	for (i = 0; i < 10; i++) if (data[i*10] != i*10) {
	    errcnt++;
	    fprintf( stderr, 
		    "[%d](rcv row-row) %d'th element = %f, should be %f\n",
		     rank, i, data[i*10], 10.0*i );
	    }
    }

    /* Second Set of Tests: Use Isend and Irecv instead of Send and Recv */
    /* First test: send a row */
    if (rank == src)
    {
	to     = dest;
	count  = 10;
	tag    = 2001;
	SetArray( data, 100 );
	/* Send a row */
	MPI_Isend( data, count, rowtype, to, tag, MPI_COMM_WORLD, &handle );
	MPI_Wait( &handle, &status );
#ifdef SHOWMSG
	printf("%d sent", rank );
	for (i = 0; i < 10; i++) printf(" %f",data[i*10]);printf("\n");
#endif
    }

    if (rank == dest)
    {
	tag   = MPI_ANY_TAG;
	count = 10;		
	from  = MPI_ANY_SOURCE;
	ClearArray( data, 100, -1.0 );
	MPI_Irecv(data, count, MPI_DOUBLE, from, tag, MPI_COMM_WORLD,
		 &handle ); 
	MPI_Wait( &handle, &status );

	st_source = status.MPI_SOURCE;
	st_tag    = status.MPI_TAG;
	MPI_Get_count( &status, MPI_DOUBLE, &st_count );

	if (st_source != src || st_tag != 2001 || st_count != 10 || verbose) {
	    printf( "Status info: source = %d, tag = %d, count = %d\n",
		    st_source, st_tag, st_count );
	}
#ifdef SHOWMSG
	printf( "%d received", rank);
	for (i = 0; i < 10; i++) printf(" %f",data[i]);	printf("\n");
#endif
	for (i = 0; i < 10; i++) if (data[i] != 10*i) {
	    errcnt++;
	    fprintf( stderr, 
		    "[%d](ircv double) %d'th element = %f, should be %f\n",
		     rank, i, data[i], 10.0*i );
	    }
    }

    /* Second test: receive a column into row */
    if (rank == src)
    {
	to     = dest;
	count  = 10;
	tag    = 2001;
	SetArray( data, 100 );
	/* Send a row */
	MPI_Isend( data, count, MPI_DOUBLE, to, tag, MPI_COMM_WORLD, 
		   &handle );
	MPI_Wait( &handle, &status );
#ifdef SHOWMSG
	printf("%d sent", rank );
	for (i = 0; i < 10; i++) printf(" %f",data[i]);printf("\n");
#endif
    }
    if (rank == dest)
    {
	tag   = MPI_ANY_TAG;
	count = 10;		
	from  = MPI_ANY_SOURCE;
	ClearArray( data, 100, -1.0 );
	MPI_Irecv(data, count, rowtype, from, tag, MPI_COMM_WORLD,
		 &handle ); 
	MPI_Wait( &handle, &status );

	st_source = status.MPI_SOURCE;
	st_tag    = status.MPI_TAG;
	MPI_Get_count( &status, MPI_DOUBLE, &st_count );

	if (st_source != src || st_tag != 2001 || st_count != 10 || verbose) {
	    printf( "Status info: source = %d, tag = %d, count = %d\n",
		    st_source, st_tag, st_count );
	}
#ifdef SHOWMSG
	printf( "%d received", rank);
	for (i = 0; i < 10; i++) printf(" %f",data[i*10]);printf("\n");
#endif
	for (i = 0; i < 10; i++) if (data[i*10] != i) {
	    errcnt++;
	    fprintf( stderr, 
		    "[%d](ircv row) %d'th element = %f, should be %f\n",
		     rank, i, data[i*10], 1.0*i );
	    }
    }

    /* Third test: send AND receive a row */
    if (rank == src)
    {
	to     = dest;
	count  = 10;
	tag    = 2001;
	SetArray( data, 100 );
	/* Send a row */
	MPI_Isend( data, count, rowtype, to, tag, MPI_COMM_WORLD, &handle );
	MPI_Wait( &handle, &status );
#ifdef SHOWMSG
	printf("%d sent", rank );
	for (i = 0; i < 10; i++) printf(" %f",data[i*10]);printf("\n");
#endif
    }
    if (rank == dest)
    {
	tag   = MPI_ANY_TAG;
	count = 10;		
	from  = MPI_ANY_SOURCE;
	ClearArray( data, 100, -1.0 );
	MPI_Irecv(data, count, rowtype, from, tag, MPI_COMM_WORLD,
		 &handle ); 
	MPI_Wait( &handle, &status );

	st_source = status.MPI_SOURCE;
	st_tag    = status.MPI_TAG;
	MPI_Get_count( &status, MPI_DOUBLE, &st_count );

	if (st_source != src || st_tag != 2001 || st_count != 10 || verbose) {
	    printf( "Status info: source = %d, tag = %d, count = %d\n",
		    st_source, st_tag, st_count );
	}
#ifdef SHOWMSG
	printf( "%d received", rank);
	for (i = 0; i < 10; i++) printf(" %f",data[i*10]);printf("\n");
#endif
	for (i = 0; i < 10; i++) if (data[i*10] != i*10) {
	    errcnt++;
	    fprintf( stderr, 
		    "[%d](ircv row-row) %d'th element = %f, should be %f\n",
		     rank, i, data[i*10], 10.0*i );
	    }
    }

    i = errcnt;
    MPI_Allreduce( &i, &errcnt, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (errcnt > 0) {
	printf( "Found %d errors in the run \n", errcnt );
	}
    MPI_Type_free( &rowtype );
    Test_Waitforall( );
    MPI_Finalize();
    return 0;
}


