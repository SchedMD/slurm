#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include <stdio.h>
#include "mpeconf.h"
#include "mpi.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int MPE_Trace_hasBeenInit = 0;
int MPE_Trace_hasBeenFinished = 0;     

#define TRACE_PRINTF(msg) \
if ( (MPE_Trace_hasBeenInit) && (!MPE_Trace_hasBeenFinished) ) {\
  PMPI_Comm_rank( MPI_COMM_WORLD, &llrank ); \
  printf( "[%d] %s\n", llrank, msg ); \
  fflush( stdout ); \
}

/*
   This was originally built with wrappergen, then modified to 
   print out some of the int values.
 */



int   MPI_Allgather( sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm )
void * sendbuf;
int sendcount;
MPI_Datatype sendtype;
void * recvbuf;
int recvcount;
MPI_Datatype recvtype;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Allgather - prototyping replacement for MPI_Allgather
    Trace the beginning and ending of MPI_Allgather.
*/

  TRACE_PRINTF( "Starting MPI_Allgather..." );

  returnVal = PMPI_Allgather( sendbuf, sendcount, sendtype, recvbuf, 
			      recvcount, recvtype, comm );
  
  TRACE_PRINTF( "Ending MPI_Allgather" );

  return returnVal;
}

int   MPI_Allgatherv( sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm )
void * sendbuf;
int sendcount;
MPI_Datatype sendtype;
void * recvbuf;
int * recvcounts;
int * displs;
MPI_Datatype recvtype;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Allgatherv - prototyping replacement for MPI_Allgatherv
    Trace the beginning and ending of MPI_Allgatherv.
*/

  TRACE_PRINTF( "Starting MPI_Allgatherv..." );
  
  returnVal = PMPI_Allgatherv( sendbuf, sendcount, sendtype, recvbuf, 
			       recvcounts, displs, recvtype, comm );

  TRACE_PRINTF( "Ending MPI_Allgatherv" );

  return returnVal;
}

int   MPI_Allreduce( sendbuf, recvbuf, count, datatype, op, comm )
void * sendbuf;
void * recvbuf;
int count;
MPI_Datatype datatype;
MPI_Op op;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Allreduce - prototyping replacement for MPI_Allreduce
    Trace the beginning and ending of MPI_Allreduce.
*/

  TRACE_PRINTF( "Starting MPI_Allreduce..." );
  
  returnVal = PMPI_Allreduce( sendbuf, recvbuf, count, datatype, op, comm );

  TRACE_PRINTF( "Ending MPI_Allreduce" );

  return returnVal;
}

int  MPI_Alltoall( sendbuf, sendcount, sendtype, recvbuf, recvcnt, recvtype, comm )
void * sendbuf;
int sendcount;
MPI_Datatype sendtype;
void * recvbuf;
int recvcnt;
MPI_Datatype recvtype;
MPI_Comm comm;
{
  int  returnVal;
  int llrank;

/*
    MPI_Alltoall - prototyping replacement for MPI_Alltoall
    Trace the beginning and ending of MPI_Alltoall.
*/

  TRACE_PRINTF( "Starting MPI_Alltoall..." );
  
  returnVal = PMPI_Alltoall( sendbuf, sendcount, sendtype, recvbuf, recvcnt, 
			     recvtype, comm );

  TRACE_PRINTF( "Ending MPI_Alltoall" );

  return returnVal;
}

int   MPI_Alltoallv( sendbuf, sendcnts, sdispls, sendtype, recvbuf, recvcnts, rdispls, recvtype, comm )
void * sendbuf;
int * sendcnts;
int * sdispls;
MPI_Datatype sendtype;
void * recvbuf;
int * recvcnts;
int * rdispls;
MPI_Datatype recvtype;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Alltoallv - prototyping replacement for MPI_Alltoallv
    Trace the beginning and ending of MPI_Alltoallv.
*/

  TRACE_PRINTF( "Starting MPI_Alltoallv..." );
  
  returnVal = PMPI_Alltoallv( sendbuf, sendcnts, sdispls, sendtype, recvbuf, 
			      recvcnts, rdispls, recvtype, comm );

  TRACE_PRINTF( "Ending MPI_Alltoallv" );

  return returnVal;
}

int   MPI_Barrier( comm )
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Barrier - prototyping replacement for MPI_Barrier
    Trace the beginning and ending of MPI_Barrier.
*/

  TRACE_PRINTF( "Starting MPI_Barrier..." );
  
  returnVal = PMPI_Barrier( comm );

  TRACE_PRINTF( "Ending MPI_Barrier" );

  return returnVal;
}

int   MPI_Bcast( buffer, count, datatype, root, comm )
void * buffer;
int count;
MPI_Datatype datatype;
int root;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Bcast - prototyping replacement for MPI_Bcast
    Trace the beginning and ending of MPI_Bcast.
*/

  TRACE_PRINTF( "Starting MPI_Bcast..." );
  
  returnVal = PMPI_Bcast( buffer, count, datatype, root, comm );

  TRACE_PRINTF( "Ending MPI_Bcast" );

  return returnVal;
}

int   MPI_Gather( sendbuf, sendcnt, sendtype, recvbuf, recvcount, recvtype, root, comm )
void * sendbuf;
int sendcnt;
MPI_Datatype sendtype;
void * recvbuf;
int recvcount;
MPI_Datatype recvtype;
int root;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Gather - prototyping replacement for MPI_Gather
    Trace the beginning and ending of MPI_Gather.
*/

  TRACE_PRINTF( "Starting MPI_Gather..." );
  
  returnVal = PMPI_Gather( sendbuf, sendcnt, sendtype, recvbuf, recvcount, 
			   recvtype, root, comm );

  TRACE_PRINTF( "Ending MPI_Gather" );

  return returnVal;
}

int   MPI_Gatherv( sendbuf, sendcnt, sendtype, recvbuf, recvcnts, displs, recvtype, root, comm )
void * sendbuf;
int sendcnt;
MPI_Datatype sendtype;
void * recvbuf;
int * recvcnts;
int * displs;
MPI_Datatype recvtype;
int root;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Gatherv - prototyping replacement for MPI_Gatherv
    Trace the beginning and ending of MPI_Gatherv.
*/

  TRACE_PRINTF( "Starting MPI_Gatherv..." );
  
  returnVal = PMPI_Gatherv( sendbuf, sendcnt, sendtype, recvbuf, recvcnts, 
			    displs, recvtype, root, comm );

  TRACE_PRINTF( "Ending MPI_Gatherv" );

  return returnVal;
}

int  MPI_Op_create( function, commute, op )
MPI_User_function * function;
int commute;
MPI_Op * op;
{
  int  returnVal;
  int llrank;

/*
    MPI_Op_create - prototyping replacement for MPI_Op_create
    Trace the beginning and ending of MPI_Op_create.
*/

  TRACE_PRINTF( "Starting MPI_Op_create..." );
  
  returnVal = PMPI_Op_create( function, commute, op );

  TRACE_PRINTF( "Ending MPI_Op_create" );

  return returnVal;
}

int  MPI_Op_free( op )
MPI_Op * op;
{
  int  returnVal;
  int llrank;

/*
    MPI_Op_free - prototyping replacement for MPI_Op_free
    Trace the beginning and ending of MPI_Op_free.
*/

  TRACE_PRINTF( "Starting MPI_Op_free..." );

  returnVal = PMPI_Op_free( op );

  TRACE_PRINTF( "Ending MPI_Op_free" );

  return returnVal;
}

int   MPI_Reduce_scatter( sendbuf, recvbuf, recvcnts, datatype, op, comm )
void * sendbuf;
void * recvbuf;
int * recvcnts;
MPI_Datatype datatype;
MPI_Op op;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Reduce_scatter - prototyping replacement for MPI_Reduce_scatter
    Trace the beginning and ending of MPI_Reduce_scatter.
*/

  TRACE_PRINTF( "Starting MPI_Reduce_scatter..." );
  
  returnVal = PMPI_Reduce_scatter( sendbuf, recvbuf, recvcnts, datatype, op, 
				   comm );

  TRACE_PRINTF( "Ending MPI_Reduce_scatter" );

  return returnVal;
}

int   MPI_Reduce( sendbuf, recvbuf, count, datatype, op, root, comm )
void * sendbuf;
void * recvbuf;
int count;
MPI_Datatype datatype;
MPI_Op op;
int root;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Reduce - prototyping replacement for MPI_Reduce
    Trace the beginning and ending of MPI_Reduce.
*/

  TRACE_PRINTF( "Starting MPI_Reduce..." );
  
  returnVal = PMPI_Reduce( sendbuf, recvbuf, count, datatype, op, root, comm );

  TRACE_PRINTF( "Ending MPI_Reduce" );

  return returnVal;
}

int   MPI_Scan( sendbuf, recvbuf, count, datatype, op, comm )
void * sendbuf;
void * recvbuf;
int count;
MPI_Datatype datatype;
MPI_Op op;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Scan - prototyping replacement for MPI_Scan
    Trace the beginning and ending of MPI_Scan.
*/

  TRACE_PRINTF( "Starting MPI_Scan..." );
  
  returnVal = PMPI_Scan( sendbuf, recvbuf, count, datatype, op, comm );

  TRACE_PRINTF( "Ending MPI_Scan" );

  return returnVal;
}

int   MPI_Scatter( sendbuf, sendcnt, sendtype, recvbuf, recvcnt, recvtype, root, comm )
void * sendbuf;
int sendcnt;
MPI_Datatype sendtype;
void * recvbuf;
int recvcnt;
MPI_Datatype recvtype;
int root;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Scatter - prototyping replacement for MPI_Scatter
    Trace the beginning and ending of MPI_Scatter.
*/

  TRACE_PRINTF( "Starting MPI_Scatter..." );

  returnVal = PMPI_Scatter( sendbuf, sendcnt, sendtype, recvbuf, recvcnt, 
			    recvtype, root, comm );

  TRACE_PRINTF( "Ending MPI_Scatter" );

  return returnVal;
}

int   MPI_Scatterv( sendbuf, sendcnts, displs, sendtype, recvbuf, recvcnt, recvtype, root, comm )
void * sendbuf;
int * sendcnts;
int * displs;
MPI_Datatype sendtype;
void * recvbuf;
int recvcnt;
MPI_Datatype recvtype;
int root;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Scatterv - prototyping replacement for MPI_Scatterv
    Trace the beginning and ending of MPI_Scatterv.
*/

  TRACE_PRINTF( "Starting MPI_Scatterv..." );
  
  returnVal = PMPI_Scatterv( sendbuf, sendcnts, displs, sendtype, recvbuf, 
			     recvcnt, recvtype, root, comm );
  
  TRACE_PRINTF( "Ending MPI_Scatterv" );

  return returnVal;
}

int   MPI_Attr_delete( comm, keyval )
MPI_Comm comm;
int keyval;
{
  int   returnVal;
  int llrank;

/*
    MPI_Attr_delete - prototyping replacement for MPI_Attr_delete
    Trace the beginning and ending of MPI_Attr_delete.
*/

  TRACE_PRINTF( "Starting MPI_Attr_delete..." );
  
  returnVal = PMPI_Attr_delete( comm, keyval );

  TRACE_PRINTF( "Ending MPI_Attr_delete..." );

  return returnVal;
}

int   MPI_Attr_get( comm, keyval, attr_value, flag )
MPI_Comm comm;
int keyval;
void * attr_value;
int * flag;
{
  int   returnVal;
  int llrank;

/*
    MPI_Attr_get - prototyping replacement for MPI_Attr_get
    Trace the beginning and ending of MPI_Attr_get.
*/

  TRACE_PRINTF( "Starting MPI_Attr_get..." );
  
  returnVal = PMPI_Attr_get( comm, keyval, attr_value, flag );

  TRACE_PRINTF( "Ending MPI_Attr_get" );

  return returnVal;
}

int   MPI_Attr_put( comm, keyval, attr_value )
MPI_Comm comm;
int keyval;
void * attr_value;
{
  int   returnVal;
  int llrank;

/*
    MPI_Attr_put - prototyping replacement for MPI_Attr_put
    Trace the beginning and ending of MPI_Attr_put.
*/

  TRACE_PRINTF( "Starting MPI_Attr_put..." );
  
  returnVal = PMPI_Attr_put( comm, keyval, attr_value );

  TRACE_PRINTF( "Ending MPI_Attr_put" );

  return returnVal;
}

int   MPI_Comm_compare( comm1, comm2, result )
MPI_Comm comm1;
MPI_Comm comm2;
int * result;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_compare - prototyping replacement for MPI_Comm_compare
    Trace the beginning and ending of MPI_Comm_compare.
*/

  TRACE_PRINTF( "Starting MPI_Comm_compare..." );
  
  returnVal = PMPI_Comm_compare( comm1, comm2, result );

  TRACE_PRINTF( "Ending MPI_Comm_compare" );

  return returnVal;
}

int   MPI_Comm_create( comm, group, comm_out )
MPI_Comm comm;
MPI_Group group;
MPI_Comm * comm_out;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_create - prototyping replacement for MPI_Comm_create
    Trace the beginning and ending of MPI_Comm_create.
*/

  TRACE_PRINTF( "Starting MPI_Comm_create..." );
  
  returnVal = PMPI_Comm_create( comm, group, comm_out );

  TRACE_PRINTF( "Ending MPI_Comm_create" );

  return returnVal;
}

int   MPI_Comm_dup( comm, comm_out )
MPI_Comm comm;
MPI_Comm * comm_out;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_dup - prototyping replacement for MPI_Comm_dup
    Trace the beginning and ending of MPI_Comm_dup.
*/

  TRACE_PRINTF( "Starting MPI_Comm_dup..." );
  
  returnVal = PMPI_Comm_dup( comm, comm_out );

  TRACE_PRINTF( "Ending MPI_Comm_dup" );

  return returnVal;
}

int   MPI_Comm_free( comm )
MPI_Comm * comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_free - prototyping replacement for MPI_Comm_free
    Trace the beginning and ending of MPI_Comm_free.
*/

  TRACE_PRINTF( "Starting MPI_Comm_free..." );

  returnVal = PMPI_Comm_free( comm );

  TRACE_PRINTF( "Ending MPI_Comm_free" );

  return returnVal;
}

int   MPI_Comm_group( comm, group )
MPI_Comm comm;
MPI_Group * group;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_group - prototyping replacement for MPI_Comm_group
    Trace the beginning and ending of MPI_Comm_group.
*/

  TRACE_PRINTF( "Starting MPI_Comm_group..." );  
  
  returnVal = PMPI_Comm_group( comm, group );

  TRACE_PRINTF( "Ending MPI_Comm_group" );  

  return returnVal;
}

int   MPI_Comm_rank( comm, rank )
MPI_Comm comm;
int * rank;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_rank - prototyping replacement for MPI_Comm_rank
    Trace the beginning and ending of MPI_Comm_rank.
*/

  TRACE_PRINTF( "Starting MPI_Comm_rank..." );
  
  returnVal = PMPI_Comm_rank( comm, rank );

  TRACE_PRINTF( "Ending MPI_Comm_rank" );    

  return returnVal;
}

int   MPI_Comm_remote_group( comm, group )
MPI_Comm comm;
MPI_Group * group;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_remote_group - prototyping replacement for MPI_Comm_remote_group
    Trace the beginning and ending of MPI_Comm_remote_group.
*/

  TRACE_PRINTF( "Starting MPI_Comm_remote_group..." );  
  
  returnVal = PMPI_Comm_remote_group( comm, group );

  TRACE_PRINTF( "Ending MPI_Comm_remote_group" );  

  return returnVal;
}

int   MPI_Comm_remote_size( comm, size )
MPI_Comm comm;
int * size;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_remote_size - prototyping replacement for MPI_Comm_remote_size
    Trace the beginning and ending of MPI_Comm_remote_size.
*/

  TRACE_PRINTF( "Starting MPI_Comm_remote_size..." );    
  
  returnVal = PMPI_Comm_remote_size( comm, size );

  TRACE_PRINTF( "Ending MPI_Comm_remote_size" );    

  return returnVal;
}

int   MPI_Comm_size( comm, size )
MPI_Comm comm;
int * size;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_size - prototyping replacement for MPI_Comm_size
    Trace the beginning and ending of MPI_Comm_size.
*/

  TRACE_PRINTF( "Starting MPI_Comm_size..." );      
  
  returnVal = PMPI_Comm_size( comm, size );

  TRACE_PRINTF( "Ending MPI_Comm_size" );      

  return returnVal;
}

int   MPI_Comm_split( comm, color, key, comm_out )
MPI_Comm comm;
int color;
int key;
MPI_Comm * comm_out;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_split - prototyping replacement for MPI_Comm_split
    Trace the beginning and ending of MPI_Comm_split.
*/

  TRACE_PRINTF( "Starting MPI_Comm_split..." );        
  
  returnVal = PMPI_Comm_split( comm, color, key, comm_out );

  TRACE_PRINTF( "Ending MPI_Comm_split" );        

  return returnVal;
}

int   MPI_Comm_test_inter( comm, flag )
MPI_Comm comm;
int * flag;
{
  int   returnVal;
  int llrank;

/*
    MPI_Comm_test_inter - prototyping replacement for MPI_Comm_test_inter
    Trace the beginning and ending of MPI_Comm_test_inter.
*/

  TRACE_PRINTF( "Starting MPI_Comm_test_inter..." );          
  
  returnVal = PMPI_Comm_test_inter( comm, flag );

  TRACE_PRINTF( "Ending MPI_Comm_test_inter" );          

  return returnVal;
}

int   MPI_Group_compare( group1, group2, result )
MPI_Group group1;
MPI_Group group2;
int * result;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_compare - prototyping replacement for MPI_Group_compare
    Trace the beginning and ending of MPI_Group_compare.
*/

  TRACE_PRINTF( "Starting MPI_Group_compare..." );            
  
  returnVal = PMPI_Group_compare( group1, group2, result );

  TRACE_PRINTF( "Ending MPI_Group_compare" );            

  return returnVal;
}

int   MPI_Group_difference( group1, group2, group_out )
MPI_Group group1;
MPI_Group group2;
MPI_Group * group_out;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_difference - prototyping replacement for MPI_Group_difference
    Trace the beginning and ending of MPI_Group_difference.
*/

  TRACE_PRINTF( "Starting MPI_Group_difference..." );              
  
  returnVal = PMPI_Group_difference( group1, group2, group_out );

  TRACE_PRINTF( "Ending MPI_Group_difference" );              

  return returnVal;
}

int   MPI_Group_excl( group, n, ranks, newgroup )
MPI_Group group;
int n;
int * ranks;
MPI_Group * newgroup;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_excl - prototyping replacement for MPI_Group_excl
    Trace the beginning and ending of MPI_Group_excl.
*/

  TRACE_PRINTF( "Starting MPI_Group_excl..." );  
  
  returnVal = PMPI_Group_excl( group, n, ranks, newgroup );

  TRACE_PRINTF( "Ending MPI_Group_excl" );  

  return returnVal;
}

int   MPI_Group_free( group )
MPI_Group * group;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_free - prototyping replacement for MPI_Group_free
    Trace the beginning and ending of MPI_Group_free.
*/

  TRACE_PRINTF( "Starting MPI_Group_free..." );    
  
  returnVal = PMPI_Group_free( group );

  TRACE_PRINTF( "Ending MPI_Group_free" );    

  return returnVal;
}

int   MPI_Group_incl( group, n, ranks, group_out )
MPI_Group group;
int n;
int * ranks;
MPI_Group * group_out;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_incl - prototyping replacement for MPI_Group_incl
    Trace the beginning and ending of MPI_Group_incl.
*/

  TRACE_PRINTF( "Starting MPI_Group_incl..." );      
  
  returnVal = PMPI_Group_incl( group, n, ranks, group_out );

  TRACE_PRINTF( "Ending MPI_Group_incl" );      

  return returnVal;
}

int   MPI_Group_intersection( group1, group2, group_out )
MPI_Group group1;
MPI_Group group2;
MPI_Group * group_out;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_intersection - prototyping replacement for MPI_Group_intersection
    Trace the beginning and ending of MPI_Group_intersection.
*/

  TRACE_PRINTF( "Starting MPI_Group_intersection..." );        
  
  returnVal = PMPI_Group_intersection( group1, group2, group_out );

  TRACE_PRINTF( "Ending MPI_Group_intersection" );        

  return returnVal;
}

int   MPI_Group_rank( group, rank )
MPI_Group group;
int * rank;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_rank - prototyping replacement for MPI_Group_rank
    Trace the beginning and ending of MPI_Group_rank.
*/

  TRACE_PRINTF( "Starting MPI_Group_rank..." );          
  
  returnVal = PMPI_Group_rank( group, rank );

  TRACE_PRINTF( "Ending MPI_Group_rank" );          

  return returnVal;
}

int   MPI_Group_range_excl( group, n, ranges, newgroup )
MPI_Group group;
int n;
int ranges[][3];
MPI_Group * newgroup;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_range_excl - prototyping replacement for MPI_Group_range_excl
    Trace the beginning and ending of MPI_Group_range_excl.
*/

  TRACE_PRINTF( "Starting MPI_Group_range_excl..." );            
  
  returnVal = PMPI_Group_range_excl( group, n, ranges, newgroup );

  TRACE_PRINTF( "Ending MPI_Group_range_excl" );            

  return returnVal;
}

int   MPI_Group_range_incl( group, n, ranges, newgroup )
MPI_Group group;
int n;
int ranges[][3];
MPI_Group * newgroup;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_range_incl - prototyping replacement for MPI_Group_range_incl
    Trace the beginning and ending of MPI_Group_range_incl.
*/

  TRACE_PRINTF( "Starting MPI_Group_range_incl..." );  
  
  returnVal = PMPI_Group_range_incl( group, n, ranges, newgroup );

  TRACE_PRINTF( "Ending MPI_Group_range_incl" );  

  return returnVal;
}

int   MPI_Group_size( group, size )
MPI_Group group;
int * size;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_size - prototyping replacement for MPI_Group_size
    Trace the beginning and ending of MPI_Group_size.
*/

  TRACE_PRINTF( "Starting MPI_Group_size..." );    
  
  returnVal = PMPI_Group_size( group, size );

  TRACE_PRINTF( "Ending MPI_Group_size" );    

  return returnVal;
}

int   MPI_Group_translate_ranks( group_a, n, ranks_a, group_b, ranks_b )
MPI_Group group_a;
int n;
int * ranks_a;
MPI_Group group_b;
int * ranks_b;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_translate_ranks - prototyping replacement for MPI_Group_translate_ranks
    Trace the beginning and ending of MPI_Group_translate_ranks.
*/

  TRACE_PRINTF( "Starting MPI_Group_translate_ranks..." );      
  
  returnVal = PMPI_Group_translate_ranks( group_a, n, ranks_a, group_b, 
					  ranks_b );

  TRACE_PRINTF( "Ending MPI_Group_translate_ranks" );      

  return returnVal;
}

int   MPI_Group_union( group1, group2, group_out )
MPI_Group group1;
MPI_Group group2;
MPI_Group * group_out;
{
  int   returnVal;
  int llrank;

/*
    MPI_Group_union - prototyping replacement for MPI_Group_union
    Trace the beginning and ending of MPI_Group_union.
*/

  TRACE_PRINTF( "Starting MPI_Group_union..." );        
  
  returnVal = PMPI_Group_union( group1, group2, group_out );

  TRACE_PRINTF( "Ending MPI_Group_union" );        

  return returnVal;
}

int   MPI_Intercomm_create( local_comm, local_leader, peer_comm, remote_leader, tag, comm_out )
MPI_Comm local_comm;
int local_leader;
MPI_Comm peer_comm;
int remote_leader;
int tag;
MPI_Comm * comm_out;
{
  int   returnVal;
  int llrank;

/*
    MPI_Intercomm_create - prototyping replacement for MPI_Intercomm_create
    Trace the beginning and ending of MPI_Intercomm_create.
*/

  TRACE_PRINTF( "Starting MPI_Intercomm_create..." );          
  
  returnVal = PMPI_Intercomm_create( local_comm, local_leader, peer_comm, 
				     remote_leader, tag, comm_out );

  TRACE_PRINTF( "Ending MPI_Intercomm_create" );          

  return returnVal;
}

int   MPI_Intercomm_merge( comm, high, comm_out )
MPI_Comm comm;
int high;
MPI_Comm * comm_out;
{
  int   returnVal;
  int llrank;

/*
    MPI_Intercomm_merge - prototyping replacement for MPI_Intercomm_merge
    Trace the beginning and ending of MPI_Intercomm_merge.
*/

  TRACE_PRINTF( "Starting MPI_Intercomm_merge..." );            
  
  returnVal = PMPI_Intercomm_merge( comm, high, comm_out );

  TRACE_PRINTF( "Ending MPI_Intercomm_merge" );            

  return returnVal;
}

int   MPI_Keyval_create( copy_fn, delete_fn, keyval, extra_state )
MPI_Copy_function * copy_fn;
MPI_Delete_function * delete_fn;
int * keyval;
void * extra_state;
{
  int   returnVal;
  int llrank;

/*
    MPI_Keyval_create - prototyping replacement for MPI_Keyval_create
    Trace the beginning and ending of MPI_Keyval_create.
*/

  TRACE_PRINTF( "Starting MPI_Keyval_create..." );

  returnVal = PMPI_Keyval_create( copy_fn, delete_fn, keyval, extra_state );

  TRACE_PRINTF( "Ending MPI_Keyval_create" );

  return returnVal;
}

int   MPI_Keyval_free( keyval )
int * keyval;
{
  int   returnVal;
  int llrank;

/*
    MPI_Keyval_free - prototyping replacement for MPI_Keyval_free
    Trace the beginning and ending of MPI_Keyval_free.
*/

  TRACE_PRINTF( "Starting MPI_Keyval_free..." );  
  
  returnVal = PMPI_Keyval_free( keyval );

  TRACE_PRINTF( "Ending MPI_Keyval_free" );  

  return returnVal;
}

int  MPI_Abort( comm, errorcode )
MPI_Comm comm;
int errorcode;
{
  int  returnVal;
  int llrank;

/*
    MPI_Abort - prototyping replacement for MPI_Abort
    Trace the beginning and ending of MPI_Abort.
*/

  TRACE_PRINTF( "Starting MPI_Abort..." );    
  
  returnVal = PMPI_Abort( comm, errorcode );

  TRACE_PRINTF( "Ending MPI_Abort" );    

  return returnVal;
}

int  MPI_Error_class( errorcode, errorclass )
int errorcode;
int * errorclass;
{
  int  returnVal;
  int llrank;

/*
    MPI_Error_class - prototyping replacement for MPI_Error_class
    Trace the beginning and ending of MPI_Error_class.
*/

  TRACE_PRINTF( "Starting MPI_Error_class..." );      
  
  returnVal = PMPI_Error_class( errorcode, errorclass );

  TRACE_PRINTF( "Ending MPI_Error_class" );      

  return returnVal;
}

int  MPI_Errhandler_create( function, errhandler )
MPI_Handler_function * function;
MPI_Errhandler * errhandler;
{
  int  returnVal;
  int llrank;

/*
    MPI_Errhandler_create - prototyping replacement for MPI_Errhandler_create
    Trace the beginning and ending of MPI_Errhandler_create.
*/

  TRACE_PRINTF( "Starting MPI_Errhandler_create..." );        
  
  returnVal = PMPI_Errhandler_create( function, errhandler );

  TRACE_PRINTF( "Ending MPI_Errhandler_create" );        

  return returnVal;
}

int  MPI_Errhandler_free( errhandler )
MPI_Errhandler * errhandler;
{
  int  returnVal;
  int llrank;

/*
    MPI_Errhandler_free - prototyping replacement for MPI_Errhandler_free
    Trace the beginning and ending of MPI_Errhandler_free.
*/

  TRACE_PRINTF( "Starting MPI_Errhandler_free..." );          
  
  returnVal = PMPI_Errhandler_free( errhandler );

  TRACE_PRINTF( "Ending MPI_Errhandler_free" );          

  return returnVal;
}

int  MPI_Errhandler_get( comm, errhandler )
MPI_Comm comm;
MPI_Errhandler * errhandler;
{
  int  returnVal;
  int llrank;

/*
    MPI_Errhandler_get - prototyping replacement for MPI_Errhandler_get
    Trace the beginning and ending of MPI_Errhandler_get.
*/

  TRACE_PRINTF( "Starting MPI_Errhandler_get..." );            
  
  returnVal = PMPI_Errhandler_get( comm, errhandler );

  TRACE_PRINTF( "Ending MPI_Errhandler_get" );            

  return returnVal;
}

int  MPI_Error_string( errorcode, string, resultlen )
int errorcode;
char * string;
int * resultlen;
{
  int  returnVal;
  int llrank;

/*
    MPI_Error_string - prototyping replacement for MPI_Error_string
    Trace the beginning and ending of MPI_Error_string.
*/

  TRACE_PRINTF( "Starting MPI_Error_string..." );              
  
  returnVal = PMPI_Error_string( errorcode, string, resultlen );

  TRACE_PRINTF( "Ending MPI_Error_string" );              

  return returnVal;
}

int  MPI_Errhandler_set( comm, errhandler )
MPI_Comm comm;
MPI_Errhandler errhandler;
{
  int  returnVal;
  int llrank;

/*
    MPI_Errhandler_set - prototyping replacement for MPI_Errhandler_set
    Trace the beginning and ending of MPI_Errhandler_set.
*/

  TRACE_PRINTF( "Starting MPI_Errhandler_set..." );                
  
  returnVal = PMPI_Errhandler_set( comm, errhandler );

  TRACE_PRINTF( "Ending MPI_Errhandler_set" );                

  return returnVal;
}

int  MPI_Finalize(  )
{
  int  returnVal;
  int llrank;

/*
    MPI_Finalize - prototyping replacement for MPI_Finalize
    Trace the beginning and ending of MPI_Finalize.
*/

  PMPI_Comm_rank( MPI_COMM_WORLD, &llrank );
  TRACE_PRINTF( "Starting MPI_Finalize..." );                  

  MPE_Trace_hasBeenFinished = 1;  
  returnVal = PMPI_Finalize(  );
 
  printf( "[%d] Ending MPI_Finalize\n", llrank ); fflush( stdout );

  return returnVal;
}

int  MPI_Get_processor_name( name, resultlen )
char * name;
int * resultlen;
{
  int  returnVal;
  int llrank;

/*
    MPI_Get_processor_name - prototyping replacement for MPI_Get_processor_name
    Trace the beginning and ending of MPI_Get_processor_name.
*/

  TRACE_PRINTF( "Starting MPI_Get_processor_name..." );                    
  
  returnVal = PMPI_Get_processor_name( name, resultlen );

  TRACE_PRINTF( "Ending MPI_Get_processor_name" );                    

  return returnVal;
}

int  MPI_Init( argc, argv )
int * argc;
char *** argv;
{
  int  returnVal;
  int llrank;

/*
    MPI_Init - prototyping replacement for MPI_Init
    Trace the beginning and ending of MPI_Init.
*/

  printf( "Starting MPI_Init...\n" ); fflush( stdout );
  
  returnVal = PMPI_Init( argc, argv );

  MPE_Trace_hasBeenInit = 1;

  TRACE_PRINTF( "Ending MPI_Init" );

  return returnVal;
}

int  MPI_Initialized( flag )
int * flag;
{
  int  returnVal;
  int llrank;

/*
    MPI_Initialized - prototyping replacement for MPI_Initialized
    Trace the beginning and ending of MPI_Initialized.
*/

  TRACE_PRINTF( "Starting MPI_Initialized..." );  
  
  returnVal = PMPI_Initialized( flag );

  TRACE_PRINTF( "Ending MPI_Initialized" );  

  return returnVal;
}

#ifdef FOO
/* Don't trace the timer calls */
double  MPI_Wtick(  )
{
  double  returnVal;
  int llrank;

/*
    MPI_Wtick - prototyping replacement for MPI_Wtick
    Trace the beginning and ending of MPI_Wtick.
*/

  TRACE_PRINTF( "Starting MPI_Wtick..." );    
  
  returnVal = PMPI_Wtick(  );

  TRACE_PRINTF( "Ending MPI_Wtick" );    

  return returnVal;
}

double  MPI_Wtime(  )
{
  double  returnVal;
  int llrank;

/*
    MPI_Wtime - prototyping replacement for MPI_Wtime
    Trace the beginning and ending of MPI_Wtime.
*/

  TRACE_PRINTF( "Starting MPI_Wtime..." );      
  
  returnVal = PMPI_Wtime(  );

  TRACE_PRINTF( "Ending MPI_Wtime" );      

  return returnVal;
}
#endif

int  MPI_Address( location, address )
void * location;
MPI_Aint * address;
{
  int  returnVal;
  int llrank;

/*
    MPI_Address - prototyping replacement for MPI_Address
    Trace the beginning and ending of MPI_Address.
*/

  TRACE_PRINTF( "Starting MPI_Address..." );        
  
  returnVal = PMPI_Address( location, address );

  TRACE_PRINTF( "Ending MPI_Address" );        

  return returnVal;
}

int  MPI_Bsend( buf, count, datatype, dest, tag, comm )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Bsend - prototyping replacement for MPI_Bsend
    Trace the beginning and ending of MPI_Bsend.
*/

  sprintf( msg, "Starting MPI_Bsend with count = %d, dest = %d, tag = %d...",
	   count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Bsend( buf, count, datatype, dest, tag, comm );

  TRACE_PRINTF( "Ending MPI_Bsend" );          

  return returnVal;
}

int  MPI_Bsend_init( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Bsend_init - prototyping replacement for MPI_Bsend_init
    Trace the beginning and ending of MPI_Bsend_init.
*/
  
  sprintf( msg, "Starting MPI_Bsend_init with count = %d, dest = %d, tag = %d...", count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Bsend_init( buf, count, datatype, dest, tag, comm, 
			       request );

  TRACE_PRINTF( "Ending MPI_Bsend_init" );          


  return returnVal;
}

int  MPI_Buffer_attach( buffer, size )
void * buffer;
int size;
{
  int  returnVal;
  int llrank;

/*
    MPI_Buffer_attach - prototyping replacement for MPI_Buffer_attach
    Trace the beginning and ending of MPI_Buffer_attach.
*/

  TRACE_PRINTF( "Starting MPI_Buffer_attach..." );
  
  returnVal = PMPI_Buffer_attach( buffer, size );

  TRACE_PRINTF( "Ending MPI_Buffer_attach" );

  return returnVal;
}

int  MPI_Buffer_detach( buffer, size )
void * buffer;
int * size;
{
  int  returnVal;
  int llrank;

/*
    MPI_Buffer_detach - prototyping replacement for MPI_Buffer_detach
    Trace the beginning and ending of MPI_Buffer_detach.
*/

  TRACE_PRINTF( "Starting MPI_Buffer_detach..." );  
  
  returnVal = PMPI_Buffer_detach( buffer, size );

  TRACE_PRINTF( "Ending MPI_Buffer_detach" );

  return returnVal;
}

int  MPI_Cancel( request )
MPI_Request * request;
{
  int  returnVal;
  int llrank;

/*
    MPI_Cancel - prototyping replacement for MPI_Cancel
    Trace the beginning and ending of MPI_Cancel.
*/

  TRACE_PRINTF( "Starting MPI_Cancel..." );
  
  returnVal = PMPI_Cancel( request );

  TRACE_PRINTF( "Ending MPI_Cancel" );

  return returnVal;
}

int  MPI_Request_free( request )
MPI_Request * request;
{
  int  returnVal;
  int llrank;

/*
    MPI_Request_free - prototyping replacement for MPI_Request_free
    Trace the beginning and ending of MPI_Request_free.
*/

  TRACE_PRINTF( "Starting MPI_Request_free..." );  
  
  returnVal = PMPI_Request_free( request );

  TRACE_PRINTF( "Ending MPI_Request_free" );  

  return returnVal;
}

int  MPI_Recv_init( buf, count, datatype, source, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int source;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Recv_init - prototyping replacement for MPI_Recv_init
    Trace the beginning and ending of MPI_Recv_init.
*/

  sprintf( msg, "Starting MPI_Recv_init with count = %d, source = %d, tag = %d ...", count, source, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Recv_init( buf, count, datatype, source, tag, comm, 
			      request );

  TRACE_PRINTF( "Ending MPI_Recv_init" );

  return returnVal;
}

int  MPI_Send_init( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Send_init - prototyping replacement for MPI_Send_init
    Trace the beginning and ending of MPI_Send_init.
*/

  sprintf( msg, "Starting MPI_Send_init with count = %d, dest = %d, tag = %d ...", count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Send_init( buf, count, datatype, dest, tag, comm, request );

  TRACE_PRINTF( "Ending MPI_Send_init" );

  return returnVal;
}

int   MPI_Get_elements( status, datatype, elements )
MPI_Status * status;
MPI_Datatype datatype;
int * elements;
{
  int   returnVal;
  int llrank;

/*
    MPI_Get_elements - prototyping replacement for MPI_Get_elements
    Trace the beginning and ending of MPI_Get_elements.
*/

  TRACE_PRINTF( "Starting MPI_Get_elements..." );

  returnVal = PMPI_Get_elements( status, datatype, elements );

  TRACE_PRINTF( "Ending MPI_Get_elements" );

  return returnVal;
}

int  MPI_Get_count( status, datatype, count )
MPI_Status * status;
MPI_Datatype datatype;
int * count;
{
  int  returnVal;
  int llrank;

/*
    MPI_Get_count - prototyping replacement for MPI_Get_count
    Trace the beginning and ending of MPI_Get_count.
*/

  TRACE_PRINTF( "Starting MPI_Get_count..." );  
  
  returnVal = PMPI_Get_count( status, datatype, count );

  TRACE_PRINTF( "Ending MPI_Get_count" );  

  return returnVal;
}

int  MPI_Ibsend( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Ibsend - prototyping replacement for MPI_Ibsend
    Trace the beginning and ending of MPI_Ibsend.
*/

  sprintf( msg, "Starting MPI_Ibsend with count - %d, dest = %d, tag = %d ...",
	   count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Ibsend( buf, count, datatype, dest, tag, comm, request );

  TRACE_PRINTF( "Ending MPI_Ibsend" );  

  return returnVal;
}

int  MPI_Iprobe( source, tag, comm, flag, status )
int source;
int tag;
MPI_Comm comm;
int * flag;
MPI_Status * status;
{
  int  returnVal;
  int llrank;

/*
    MPI_Iprobe - prototyping replacement for MPI_Iprobe
    Trace the beginning and ending of MPI_Iprobe.
*/

  TRACE_PRINTF( "Starting MPI_Iprobe..." );
  
  returnVal = PMPI_Iprobe( source, tag, comm, flag, status );

  TRACE_PRINTF( "Ending MPI_Iprobe" );

  return returnVal;
}

int  MPI_Irecv( buf, count, datatype, source, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int source;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Irecv - prototyping replacement for MPI_Irecv
    Trace the beginning and ending of MPI_Irecv.
*/

  sprintf( msg, "Starting MPI_Irecv with count = %d, source = %d, tag = %d ...", count, source, tag );
  TRACE_PRINTF( msg );

  returnVal = PMPI_Irecv( buf, count, datatype, source, tag, comm, request );

  TRACE_PRINTF( "Ending MPI_Irecv" );

  return returnVal;
}

int  MPI_Irsend( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Irsend - prototyping replacement for MPI_Irsend
    Trace the beginning and ending of MPI_Irsend.
*/

  sprintf( msg, "Starting MPI_Irsend with count = %d, dest = %d, tag = %d ...", count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Irsend( buf, count, datatype, dest, tag, comm, request );

  TRACE_PRINTF( "Ending MPI_Irsend" );

  return returnVal;
}

int  MPI_Isend( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Isend - prototyping replacement for MPI_Isend
    Trace the beginning and ending of MPI_Isend.
*/

  sprintf( msg, "Starting MPI_Isend with count = %d, dest = %d, tag = %d ...",
	   count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Isend( buf, count, datatype, dest, tag, comm, request );

  TRACE_PRINTF( "Ending MPI_Isend" );

  return returnVal;
}

int  MPI_Issend( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Issend - prototyping replacement for MPI_Issend
    Trace the beginning and ending of MPI_Issend.
*/

  sprintf( msg, "Starting MPI_Issend with count = %d, dest = %d, tag = %d ...",
	   count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Issend( buf, count, datatype, dest, tag, comm, request );

  TRACE_PRINTF( "Ending MPI_Issend" );

  return returnVal;
}

int   MPI_Pack( inbuf, incount, type, outbuf, outcount, position, comm )
void * inbuf;
int incount;
MPI_Datatype type;
void * outbuf;
int outcount;
int * position;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Pack - prototyping replacement for MPI_Pack
    Trace the beginning and ending of MPI_Pack.
*/

  TRACE_PRINTF( "Starting MPI_Pack..." );
  
  returnVal = PMPI_Pack( inbuf, incount, type, outbuf, outcount, position, 
			 comm );

  TRACE_PRINTF( "Ending MPI_Pack" );

  return returnVal;
}

int   MPI_Pack_size( incount, datatype, comm, size )
int incount;
MPI_Datatype datatype;
MPI_Comm comm;
int * size;
{
  int   returnVal;
  int llrank;

/*
    MPI_Pack_size - prototyping replacement for MPI_Pack_size
    Trace the beginning and ending of MPI_Pack_size.
*/

  TRACE_PRINTF( "Starting MPI_Pack_size..." );  

  returnVal = PMPI_Pack_size( incount, datatype, comm, size );

  TRACE_PRINTF( "Ending MPI_Pack_size" );  

  return returnVal;
}

int  MPI_Probe( source, tag, comm, status )
int source;
int tag;
MPI_Comm comm;
MPI_Status * status;
{
  int  returnVal;
  int llrank;

/*
    MPI_Probe - prototyping replacement for MPI_Probe
    Trace the beginning and ending of MPI_Probe.
*/

  TRACE_PRINTF( "Starting MPI_Probe..." );    
  
  returnVal = PMPI_Probe( source, tag, comm, status );

  TRACE_PRINTF( "Ending MPI_Probe" );    

  return returnVal;
}

int  MPI_Recv( buf, count, datatype, source, tag, comm, status )
void * buf;
int count;
MPI_Datatype datatype;
int source;
int tag;
MPI_Comm comm;
MPI_Status * status;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Recv - prototyping replacement for MPI_Recv
    Trace the beginning and ending of MPI_Recv.
*/

  sprintf( msg, "Starting MPI_Recv with count = %d, source = %d, tag = %d...", 
	   count, source, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Recv( buf, count, datatype, source, tag, comm, status );

  sprintf( msg, "Ending MPI_Recv from %d with tag %d", 
	   status->MPI_SOURCE, status->MPI_TAG ); 
  TRACE_PRINTF( msg );

  return returnVal;
}

int  MPI_Rsend( buf, count, datatype, dest, tag, comm )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Rsend - prototyping replacement for MPI_Rsend
    Trace the beginning and ending of MPI_Rsend.
*/

  sprintf( msg, "Starting MPI_Rsend with count = %d, dest = %d, tag = %d...", 
	   count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Rsend( buf, count, datatype, dest, tag, comm );

  TRACE_PRINTF( "Ending MPI_Rsend" );

  return returnVal;
}

int  MPI_Rsend_init( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Rsend_init - prototyping replacement for MPI_Rsend_init
    Trace the beginning and ending of MPI_Rsend_init.
*/

  sprintf( msg, "Starting MPI_Rsend_init with count = %d, dest = %d, tag = %d...", count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Rsend_init( buf, count, datatype, dest, tag, comm, 
			       request );

  TRACE_PRINTF( "Ending MPI_Rsend_init" );

  return returnVal;
}

int  MPI_Send( buf, count, datatype, dest, tag, comm )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Send - prototyping replacement for MPI_Send
    Trace the beginning and ending of MPI_Send.
*/

  sprintf( msg, "Starting MPI_Send with count = %d, dest = %d, tag = %d...", 
	   count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Send( buf, count, datatype, dest, tag, comm );

  TRACE_PRINTF( "Ending MPI_Send" );

  return returnVal;
}

int  MPI_Sendrecv( sendbuf, sendcount, sendtype, dest, sendtag, recvbuf, recvcount, recvtype, source, recvtag, comm, status )
void * sendbuf;
int sendcount;
MPI_Datatype sendtype;
int dest;
int sendtag;
void * recvbuf;
int recvcount;
MPI_Datatype recvtype;
int source;
int recvtag;
MPI_Comm comm;
MPI_Status * status;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Sendrecv - prototyping replacement for MPI_Sendrecv
    Trace the beginning and ending of MPI_Sendrecv.
*/

  sprintf( msg, "Starting MPI_Sendrecv with sendtag %d, recvtag %d, dest %d, source %d ...", sendtag, recvtag, dest, source );
  TRACE_PRINTF( msg );

  returnVal = PMPI_Sendrecv( sendbuf, sendcount, sendtype, dest, sendtag, 
			     recvbuf, recvcount, recvtype, source, recvtag, 
			     comm, status );

  TRACE_PRINTF( "Ending MPI_Sendrecv" );

  return returnVal;
}

int  MPI_Sendrecv_replace( buf, count, datatype, dest, sendtag, source, recvtag, comm, status )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int sendtag;
int source;
int recvtag;
MPI_Comm comm;
MPI_Status * status;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Sendrecv_replace - prototyping replacement for MPI_Sendrecv_replace
    Trace the beginning and ending of MPI_Sendrecv_replace.
*/

  sprintf( msg, "Starting MPI_Sendrecv_replace with sendtag %d, recvtag %d, dest %d, source %d ...", sendtag, recvtag, dest, source );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Sendrecv_replace( buf, count, datatype, dest, sendtag, 
				     source, recvtag, comm, status );

  TRACE_PRINTF( "Ending MPI_Sendrecv_replace" );

  return returnVal;
}

int  MPI_Ssend( buf, count, datatype, dest, tag, comm )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Ssend - prototyping replacement for MPI_Ssend
    Trace the beginning and ending of MPI_Ssend.
*/

  sprintf( msg, "Starting MPI_Ssend with count = %d, dest = %d, tag = %d...",
	   count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Ssend( buf, count, datatype, dest, tag, comm );

  TRACE_PRINTF( "Ending MPI_Ssend" );

  return returnVal;
}

int  MPI_Ssend_init( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  int  llrank;
  char msg[100];

/*
    MPI_Ssend_init - prototyping replacement for MPI_Ssend_init
    Trace the beginning and ending of MPI_Ssend_init.
*/

  sprintf( msg, "Starting MPI_Ssend_init with count = %d, dest = %d, tag = %d...", count, dest, tag );
  TRACE_PRINTF( msg );
  
  returnVal = PMPI_Ssend_init( buf, count, datatype, dest, tag, comm, 
			       request );

  TRACE_PRINTF( "Ending MPI_Ssend_init" );

  return returnVal;
}

int  MPI_Start( request )
MPI_Request * request;
{
  int  returnVal;
  int llrank;

/*
    MPI_Start - prototyping replacement for MPI_Start
    Trace the beginning and ending of MPI_Start.
*/

  TRACE_PRINTF( "Starting MPI_Start..." );
  
  returnVal = PMPI_Start( request );

  TRACE_PRINTF( "Ending MPI_Start" );

  return returnVal;
}

int  MPI_Startall( count, array_of_requests )
int count;
MPI_Request * array_of_requests;
{
  int  returnVal;
  int llrank;

/*
    MPI_Startall - prototyping replacement for MPI_Startall
    Trace the beginning and ending of MPI_Startall.
*/

  TRACE_PRINTF( "Starting MPI_Startall..." );  
  
  returnVal = PMPI_Startall( count, array_of_requests );

  TRACE_PRINTF( "Ending MPI_Startall" );  

  return returnVal;
}

int   MPI_Test( request, flag, status )
MPI_Request * request;
int * flag;
MPI_Status * status;
{
  int   returnVal;
  int llrank;

/*
    MPI_Test - prototyping replacement for MPI_Test
    Trace the beginning and ending of MPI_Test.
*/

  TRACE_PRINTF( "Starting MPI_Test..." );    
  
  returnVal = PMPI_Test( request, flag, status );

  TRACE_PRINTF( "Ending MPI_Test" );    

  return returnVal;
}

int  MPI_Testall( count, array_of_requests, flag, array_of_statuses )
int count;
MPI_Request * array_of_requests;
int * flag;
MPI_Status * array_of_statuses;
{
  int  returnVal;
  int llrank;

/*
    MPI_Testall - prototyping replacement for MPI_Testall
    Trace the beginning and ending of MPI_Testall.
*/

  TRACE_PRINTF( "Starting MPI_Testall..." );
  
  returnVal = PMPI_Testall( count, array_of_requests, flag, 
			    array_of_statuses );

  TRACE_PRINTF( "Ending MPI_Testall" );      

  return returnVal;
}

int  MPI_Testany( count, array_of_requests, index, flag, status )
int count;
MPI_Request * array_of_requests;
int * index;
int * flag;
MPI_Status * status;
{
  int  returnVal;
  int llrank;

/*
    MPI_Testany - prototyping replacement for MPI_Testany
    Trace the beginning and ending of MPI_Testany.
*/

  TRACE_PRINTF( "Starting MPI_Testany..." );  
  
  returnVal = PMPI_Testany( count, array_of_requests, index, flag, status );

  TRACE_PRINTF( "Ending MPI_Testany" );  

  return returnVal;
}

int  MPI_Test_cancelled( status, flag )
MPI_Status * status;
int * flag;
{
  int  returnVal;
  int llrank;

/*
    MPI_Test_cancelled - prototyping replacement for MPI_Test_cancelled
    Trace the beginning and ending of MPI_Test_cancelled.
*/

  TRACE_PRINTF( "Starting MPI_Test_cancelled..." );    
  
  returnVal = PMPI_Test_cancelled( status, flag );

  TRACE_PRINTF( "Ending MPI_Test_cancelled" );    

  return returnVal;
}

int  MPI_Testsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses )
int incount;
MPI_Request * array_of_requests;
int * outcount;
int * array_of_indices;
MPI_Status * array_of_statuses;
{
  int  returnVal;
  int llrank;

/*
    MPI_Testsome - prototyping replacement for MPI_Testsome
    Trace the beginning and ending of MPI_Testsome.
*/

  TRACE_PRINTF( "Starting MPI_Testsome..." );
  
  returnVal = PMPI_Testsome( incount, array_of_requests, outcount, 
			     array_of_indices, array_of_statuses );

  TRACE_PRINTF( "Ending MPI_Testsome" );      

  return returnVal;
}

int   MPI_Type_commit( datatype )
MPI_Datatype * datatype;
{
  int   returnVal;
  int llrank;

/*
    MPI_Type_commit - prototyping replacement for MPI_Type_commit
    Trace the beginning and ending of MPI_Type_commit.
*/

  TRACE_PRINTF( "Starting MPI_Type_commit..." );

  returnVal = PMPI_Type_commit( datatype );

  TRACE_PRINTF( "Ending MPI_Type_commit" );
    
  return returnVal;
}

int  MPI_Type_contiguous( count, old_type, newtype )
int count;
MPI_Datatype old_type;
MPI_Datatype * newtype;
{
  int  returnVal;
  int llrank;

/*
    MPI_Type_contiguous - prototyping replacement for MPI_Type_contiguous
    Trace the beginning and ending of MPI_Type_contiguous.
*/

  TRACE_PRINTF( "Starting MPI_Type_contiguous..." );

  returnVal = PMPI_Type_contiguous( count, old_type, newtype );
    
  TRACE_PRINTF( "Ending MPI_Type_contiguous" );

  return returnVal;

}

int  MPI_Type_extent( datatype, extent )
MPI_Datatype datatype;
MPI_Aint * extent;
{
  int  returnVal;
  int llrank;

/*
    MPI_Type_extent - prototyping replacement for MPI_Type_extent
    Trace the beginning and ending of MPI_Type_extent.
*/

  TRACE_PRINTF( "Starting MPI_Type_extent..." );
  
  returnVal = PMPI_Type_extent( datatype, extent );

  TRACE_PRINTF( "Ending MPI_Type_extent" );  

  return returnVal;
}

int   MPI_Type_free( datatype )
MPI_Datatype * datatype;
{
  int   returnVal;
  int llrank;

/*
    MPI_Type_free - prototyping replacement for MPI_Type_free
    Trace the beginning and ending of MPI_Type_free.
*/

  TRACE_PRINTF( "Starting MPI_Type_free..." );
  
  returnVal = PMPI_Type_free( datatype );

  TRACE_PRINTF( "Ending MPI_Type_free" );  

  return returnVal;
}

int  MPI_Type_hindexed( count, blocklens, indices, old_type, newtype )
int count;
int * blocklens;
MPI_Aint * indices;
MPI_Datatype old_type;
MPI_Datatype * newtype;
{
  int  returnVal;
  int llrank;

/*
    MPI_Type_hindexed - prototyping replacement for MPI_Type_hindexed
    Trace the beginning and ending of MPI_Type_hindexed.
*/

  TRACE_PRINTF( "Starting MPI_Type_hindexed..." );
  
  returnVal = PMPI_Type_hindexed( count, blocklens, indices, old_type, 
				  newtype );

  TRACE_PRINTF( "Ending MPI_Type_hindexed" );  

  return returnVal;
}

int  MPI_Type_hvector( count, blocklen, stride, old_type, newtype )
int count;
int blocklen;
MPI_Aint stride;
MPI_Datatype old_type;
MPI_Datatype * newtype;
{
  int  returnVal;
  int llrank;

/*
    MPI_Type_hvector - prototyping replacement for MPI_Type_hvector
    Trace the beginning and ending of MPI_Type_hvector.
*/

  TRACE_PRINTF( "Starting MPI_Type_hvector..." );
  
  returnVal = PMPI_Type_hvector( count, blocklen, stride, old_type, newtype );

  TRACE_PRINTF( "Ending MPI_Type_hvector" );  

  return returnVal;
}

int  MPI_Type_indexed( count, blocklens, indices, old_type, newtype )
int count;
int * blocklens;
int * indices;
MPI_Datatype old_type;
MPI_Datatype * newtype;
{
  int  returnVal;
  int llrank;

/*
    MPI_Type_indexed - prototyping replacement for MPI_Type_indexed
    Trace the beginning and ending of MPI_Type_indexed.
*/

  TRACE_PRINTF( "Starting MPI_Type_indexed..." );
  
  returnVal = PMPI_Type_indexed( count, blocklens, indices, old_type, 
				 newtype );

  TRACE_PRINTF( "Ending MPI_Type_indexed" );

  return returnVal;
}

int   MPI_Type_lb( datatype, displacement )
MPI_Datatype datatype;
MPI_Aint * displacement;
{
  int   returnVal;
  int llrank;

/*
    MPI_Type_lb - prototyping replacement for MPI_Type_lb
    Trace the beginning and ending of MPI_Type_lb.
*/

  TRACE_PRINTF( "Starting MPI_Type_lb..." );
  
  returnVal = PMPI_Type_lb( datatype, displacement );

  TRACE_PRINTF( "Ending MPI_Type_lb" );

  return returnVal;
}

int   MPI_Type_size( datatype, size )
MPI_Datatype datatype;
int          * size;
{
  int   returnVal;
  int llrank;

/*
    MPI_Type_size - prototyping replacement for MPI_Type_size
    Trace the beginning and ending of MPI_Type_size.
*/

  TRACE_PRINTF( "Starting MPI_Type_size..." );
  
  returnVal = PMPI_Type_size( datatype, size );

  TRACE_PRINTF( "Ending MPI_Type_size" );

  return returnVal;
}

int  MPI_Type_struct( count, blocklens, indices, old_types, newtype )
int count;
int * blocklens;
MPI_Aint * indices;
MPI_Datatype * old_types;
MPI_Datatype * newtype;
{
  int  returnVal;
  int llrank;

/*
    MPI_Type_struct - prototyping replacement for MPI_Type_struct
    Trace the beginning and ending of MPI_Type_struct.
*/

  TRACE_PRINTF( "Starting MPI_Type_struct..." );

  returnVal = PMPI_Type_struct( count, blocklens, indices, old_types, 
				newtype );

  TRACE_PRINTF( "Ending MPI_Type_struct" );

  return returnVal;
}

int   MPI_Type_ub( datatype, displacement )
MPI_Datatype datatype;
MPI_Aint * displacement;
{
  int   returnVal;
  int llrank;

/*
    MPI_Type_ub - prototyping replacement for MPI_Type_ub
    Trace the beginning and ending of MPI_Type_ub.
*/

  TRACE_PRINTF( "Starting MPI_Type_ub..." );

  returnVal = PMPI_Type_ub( datatype, displacement );

  TRACE_PRINTF( "Ending MPI_Type_ub" );

  return returnVal;
}

int  MPI_Type_vector( count, blocklen, stride, old_type, newtype )
int count;
int blocklen;
int stride;
MPI_Datatype old_type;
MPI_Datatype * newtype;
{
  int  returnVal;
  int llrank;

/*
    MPI_Type_vector - prototyping replacement for MPI_Type_vector
    Trace the beginning and ending of MPI_Type_vector.
*/

  TRACE_PRINTF( "Starting MPI_Type_vector..." );
  
  returnVal = PMPI_Type_vector( count, blocklen, stride, old_type, newtype );

  TRACE_PRINTF( "Ending MPI_Type_vector" );

  return returnVal;
}

int   MPI_Unpack( inbuf, insize, position, outbuf, outcount, type, comm )
void * inbuf;
int insize;
int * position;
void * outbuf;
int outcount;
MPI_Datatype type;
MPI_Comm comm;
{
  int   returnVal;
  int llrank;

/*
    MPI_Unpack - prototyping replacement for MPI_Unpack
    Trace the beginning and ending of MPI_Unpack.
*/

  TRACE_PRINTF( "Starting MPI_Unpack..." );
  
  returnVal = PMPI_Unpack( inbuf, insize, position, outbuf, outcount, type, 
			   comm );

  TRACE_PRINTF( "Ending MPI_Unpack" );

  return returnVal;
}

int   MPI_Wait( request, status )
MPI_Request * request;
MPI_Status * status;
{
  int   returnVal;
  int llrank;

/*
    MPI_Wait - prototyping replacement for MPI_Wait
    Trace the beginning and ending of MPI_Wait.
*/

  TRACE_PRINTF( "Starting MPI_Wait..." );
  
  returnVal = PMPI_Wait( request, status );

  TRACE_PRINTF( "Ending MPI_Wait" );

  return returnVal;
}

int  MPI_Waitall( count, array_of_requests, array_of_statuses )
int count;
MPI_Request * array_of_requests;
MPI_Status * array_of_statuses;
{
  int  returnVal;
  int llrank;

/*
    MPI_Waitall - prototyping replacement for MPI_Waitall
    Trace the beginning and ending of MPI_Waitall.
*/

  TRACE_PRINTF( "Starting MPI_Waitall..." );
  
  returnVal = PMPI_Waitall( count, array_of_requests, array_of_statuses );

  TRACE_PRINTF( "Ending MPI_Waitall" );

  return returnVal;
}

int  MPI_Waitany( count, array_of_requests, index, status )
int count;
MPI_Request * array_of_requests;
int * index;
MPI_Status * status;
{
  int  returnVal;
  int llrank;

/*
    MPI_Waitany - prototyping replacement for MPI_Waitany
    Trace the beginning and ending of MPI_Waitany.
*/

  TRACE_PRINTF( "Starting MPI_Waitany..." );
  
  returnVal = PMPI_Waitany( count, array_of_requests, index, status );

  TRACE_PRINTF( "Ending MPI_Waitany" );

  return returnVal;
}

int  MPI_Waitsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses )
int incount;
MPI_Request * array_of_requests;
int * outcount;
int * array_of_indices;
MPI_Status * array_of_statuses;
{
  int  returnVal;
  int llrank;

/*
    MPI_Waitsome - prototyping replacement for MPI_Waitsome
    Trace the beginning and ending of MPI_Waitsome.
*/

  TRACE_PRINTF( "Starting MPI_Waitsome..." );
  
  returnVal = PMPI_Waitsome( incount, array_of_requests, outcount, 
			     array_of_indices, array_of_statuses );

  TRACE_PRINTF( "Ending MPI_Waitsome" );

  return returnVal;
}

int   MPI_Cart_coords( comm, rank, maxdims, coords )
MPI_Comm comm;
int rank;
int maxdims;
int * coords;
{
  int   returnVal;
  int llrank;

/*
    MPI_Cart_coords - prototyping replacement for MPI_Cart_coords
    Trace the beginning and ending of MPI_Cart_coords.
*/

  TRACE_PRINTF( "Starting MPI_Cart_coords..." );
  
  returnVal = PMPI_Cart_coords( comm, rank, maxdims, coords );

  TRACE_PRINTF( "Ending MPI_Cart_coords" );

  return returnVal;
}

int   MPI_Cart_create( comm_old, ndims, dims, periods, reorder, comm_cart )
MPI_Comm comm_old;
int ndims;
int * dims;
int * periods;
int reorder;
MPI_Comm * comm_cart;
{
  int   returnVal;
  int llrank;

/*
    MPI_Cart_create - prototyping replacement for MPI_Cart_create
    Trace the beginning and ending of MPI_Cart_create.
*/

  TRACE_PRINTF( "Starting MPI_Cart_create..." );
  
  returnVal = PMPI_Cart_create( comm_old, ndims, dims, periods, reorder, 
				comm_cart );

  TRACE_PRINTF( "Ending MPI_Cart_create" );

  return returnVal;
}

int   MPI_Cart_get( comm, maxdims, dims, periods, coords )
MPI_Comm comm;
int maxdims;
int * dims;
int * periods;
int * coords;
{
  int   returnVal;
  int llrank;

/*
    MPI_Cart_get - prototyping replacement for MPI_Cart_get
    Trace the beginning and ending of MPI_Cart_get.
*/

  TRACE_PRINTF( "Starting MPI_Cart_get..." );
  
  returnVal = PMPI_Cart_get( comm, maxdims, dims, periods, coords );

  TRACE_PRINTF( "Ending MPI_Cart_get" );

  return returnVal;
}

int   MPI_Cart_map( comm_old, ndims, dims, periods, newrank )
MPI_Comm comm_old;
int ndims;
int * dims;
int * periods;
int * newrank;
{
  int   returnVal;
  int llrank;

/*
    MPI_Cart_map - prototyping replacement for MPI_Cart_map
    Trace the beginning and ending of MPI_Cart_map.
*/

  TRACE_PRINTF( "Starting MPI_Cart_map..." );
  
  returnVal = PMPI_Cart_map( comm_old, ndims, dims, periods, newrank );

  TRACE_PRINTF( "Ending MPI_Cart_map" );

  return returnVal;
}

int   MPI_Cart_rank( comm, coords, rank )
MPI_Comm comm;
int * coords;
int * rank;
{
  int   returnVal;
  int llrank;

/*
    MPI_Cart_rank - prototyping replacement for MPI_Cart_rank
    Trace the beginning and ending of MPI_Cart_rank.
*/

  TRACE_PRINTF( "Starting MPI_Cart_rank..." );
  
  returnVal = PMPI_Cart_rank( comm, coords, rank );

  TRACE_PRINTF( "Ending MPI_Cart_rank" );

  return returnVal;
}

int   MPI_Cart_shift( comm, direction, displ, source, dest )
MPI_Comm comm;
int direction;
int displ;
int * source;
int * dest;
{
  int   returnVal;
  int llrank;

/*
    MPI_Cart_shift - prototyping replacement for MPI_Cart_shift
    Trace the beginning and ending of MPI_Cart_shift.
*/

  TRACE_PRINTF( "Starting MPI_Cart_shift..." );
  
  returnVal = PMPI_Cart_shift( comm, direction, displ, source, dest );

  TRACE_PRINTF( "Ending MPI_Cart_shift" );

  return returnVal;
}

int   MPI_Cart_sub( comm, remain_dims, comm_new )
MPI_Comm comm;
int * remain_dims;
MPI_Comm * comm_new;
{
  int   returnVal;
  int llrank;

/*
    MPI_Cart_sub - prototyping replacement for MPI_Cart_sub
    Trace the beginning and ending of MPI_Cart_sub.
*/

  TRACE_PRINTF( "Starting MPI_Cart_sub..." );
  
  returnVal = PMPI_Cart_sub( comm, remain_dims, comm_new );

  TRACE_PRINTF( "Ending MPI_Cart_sub" );

  return returnVal;
}

int   MPI_Cartdim_get( comm, ndims )
MPI_Comm comm;
int * ndims;
{
  int   returnVal;
  int llrank;

/*
    MPI_Cartdim_get - prototyping replacement for MPI_Cartdim_get
    Trace the beginning and ending of MPI_Cartdim_get.
*/

  TRACE_PRINTF( "Starting MPI_Cartdim_get..." );
  
  returnVal = PMPI_Cartdim_get( comm, ndims );

  TRACE_PRINTF( "Ending MPI_Cartdim_get" );

  return returnVal;
}

int  MPI_Dims_create( nnodes, ndims, dims )
int nnodes;
int ndims;
int * dims;
{
  int  returnVal;
  int llrank;

/*
    MPI_Dims_create - prototyping replacement for MPI_Dims_create
    Trace the beginning and ending of MPI_Dims_create.
*/

  TRACE_PRINTF( "Starting MPI_Dims_create..." );
  
  returnVal = PMPI_Dims_create( nnodes, ndims, dims );

  TRACE_PRINTF( "Ending MPI_Dims_create" );

  return returnVal;
}

int   MPI_Graph_create( comm_old, nnodes, index, edges, reorder, comm_graph )
MPI_Comm comm_old;
int nnodes;
int * index;
int * edges;
int reorder;
MPI_Comm * comm_graph;
{
  int   returnVal;
  int llrank;

/*
    MPI_Graph_create - prototyping replacement for MPI_Graph_create
    Trace the beginning and ending of MPI_Graph_create.
*/

  TRACE_PRINTF( "Starting MPI_Graph_create..." );
  
  returnVal = PMPI_Graph_create( comm_old, nnodes, index, edges, reorder, 
				 comm_graph );

  TRACE_PRINTF( "Ending MPI_Graph_create" );

  return returnVal;
}

int   MPI_Graph_get( comm, maxindex, maxedges, index, edges )
MPI_Comm comm;
int maxindex;
int maxedges;
int * index;
int * edges;
{
  int   returnVal;
  int llrank;

/*
    MPI_Graph_get - prototyping replacement for MPI_Graph_get
    Trace the beginning and ending of MPI_Graph_get.
*/

  TRACE_PRINTF( "Starting MPI_Graph_get..." );
  
  returnVal = PMPI_Graph_get( comm, maxindex, maxedges, index, edges );

  TRACE_PRINTF( "Ending MPI_Graph_get" );

  return returnVal;
}

int   MPI_Graph_map( comm_old, nnodes, index, edges, newrank )
MPI_Comm comm_old;
int nnodes;
int * index;
int * edges;
int * newrank;
{
  int   returnVal;
  int llrank;

/*
    MPI_Graph_map - prototyping replacement for MPI_Graph_map
    Trace the beginning and ending of MPI_Graph_map.
*/

  TRACE_PRINTF( "Starting MPI_Graph_map..." );
  
  returnVal = PMPI_Graph_map( comm_old, nnodes, index, edges, newrank );

  TRACE_PRINTF( "Ending MPI_Graph_map" );

  return returnVal;
}

int   MPI_Graph_neighbors( comm, rank, maxneighbors, neighbors )
MPI_Comm comm;
int rank;
int maxneighbors;
int * neighbors;
{
  int   returnVal;
  int llrank;

/*
    MPI_Graph_neighbors - prototyping replacement for MPI_Graph_neighbors
    Trace the beginning and ending of MPI_Graph_neighbors.
*/

  TRACE_PRINTF( "Starting MPI_Graph_neighbors..." );
  
  returnVal = PMPI_Graph_neighbors( comm, rank, maxneighbors, neighbors );

  TRACE_PRINTF( "Ending MPI_Graph_neighbors" );

  return returnVal;
}

int   MPI_Graph_neighbors_count( comm, rank, nneighbors )
MPI_Comm comm;
int rank;
int * nneighbors;
{
  int   returnVal;
  int llrank;

/*
    MPI_Graph_neighbors_count - prototyping replacement for MPI_Graph_neighbors_count
    Trace the beginning and ending of MPI_Graph_neighbors_count.
*/

  TRACE_PRINTF( "Starting MPI_Graph_neighbors_count..." );
  
  returnVal = PMPI_Graph_neighbors_count( comm, rank, nneighbors );

  TRACE_PRINTF( "Ending MPI_Graph_neighbors_count" );

  return returnVal;
}

int   MPI_Graphdims_get( comm, nnodes, nedges )
MPI_Comm comm;
int * nnodes;
int * nedges;
{
  int   returnVal;
  int llrank;

/*
    MPI_Graphdims_get - prototyping replacement for MPI_Graphdims_get
    Trace the beginning and ending of MPI_Graphdims_get.
*/

  TRACE_PRINTF( "Starting MPI_Graphdims_get..." );
  
  returnVal = PMPI_Graphdims_get( comm, nnodes, nedges );

  TRACE_PRINTF( "Ending MPI_Graphdims_get" );

  return returnVal;
}

int   MPI_Topo_test( comm, top_type )
MPI_Comm comm;
int * top_type;
{
  int   returnVal;
  int llrank;

/*
    MPI_Topo_test - prototyping replacement for MPI_Topo_test
    Trace the beginning and ending of MPI_Topo_test.
*/

  TRACE_PRINTF( "Starting MPI_Topo_test..." );
  
  returnVal = PMPI_Topo_test( comm, top_type );

  TRACE_PRINTF( "Ending MPI_Topo_test" );

  return returnVal;
}
