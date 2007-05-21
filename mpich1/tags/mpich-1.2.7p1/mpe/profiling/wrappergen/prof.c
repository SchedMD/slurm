#include "mpi.h"


static int MPI_Send_nsends_0; 
static int MPI_Bsend_nsends_0; 
static int MPI_Isend_nsends_0; 

int MPI_Allgather_ncalls_0;
int MPI_Allgatherv_ncalls_0;
int MPI_Allreduce_ncalls_0;
int MPI_Alltoall_ncalls_0;
int MPI_Alltoallv_ncalls_0;
int MPI_Barrier_ncalls_0;
int MPI_Bcast_ncalls_0;
int MPI_Gather_ncalls_0;
int MPI_Gatherv_ncalls_0;
int MPI_Op_create_ncalls_0;
int MPI_Op_free_ncalls_0;
int MPI_Reduce_scatter_ncalls_0;
int MPI_Reduce_ncalls_0;
int MPI_Scan_ncalls_0;
int MPI_Scatter_ncalls_0;
int MPI_Scatterv_ncalls_0;
int MPI_Attr_delete_ncalls_0;
int MPI_Attr_get_ncalls_0;
int MPI_Attr_put_ncalls_0;
int MPI_Comm_compare_ncalls_0;
int MPI_Comm_create_ncalls_0;
int MPI_Comm_dup_ncalls_0;
int MPI_Comm_free_ncalls_0;
int MPI_Comm_group_ncalls_0;
int MPI_Comm_rank_ncalls_0;
int MPI_Comm_remote_group_ncalls_0;
int MPI_Comm_remote_size_ncalls_0;
int MPI_Comm_size_ncalls_0;
int MPI_Comm_split_ncalls_0;
int MPI_Comm_test_inter_ncalls_0;
int MPI_Group_compare_ncalls_0;
int MPI_Group_difference_ncalls_0;
int MPI_Group_excl_ncalls_0;
int MPI_Group_free_ncalls_0;
int MPI_Group_incl_ncalls_0;
int MPI_Group_intersection_ncalls_0;
int MPI_Group_rank_ncalls_0;
int MPI_Group_range_excl_ncalls_0;
int MPI_Group_range_incl_ncalls_0;
int MPI_Group_size_ncalls_0;
int MPI_Group_translate_ranks_ncalls_0;
int MPI_Group_union_ncalls_0;
int MPI_Intercomm_create_ncalls_0;
int MPI_Intercomm_merge_ncalls_0;
int MPI_Keyval_create_ncalls_0;
int MPI_Keyval_free_ncalls_0;
int MPI_Abort_ncalls_0;
int MPI_Error_class_ncalls_0;
int MPI_Errhandler_create_ncalls_0;
int MPI_Errhandler_free_ncalls_0;
int MPI_Errhandler_get_ncalls_0;
int MPI_Error_string_ncalls_0;
int MPI_Errhandler_set_ncalls_0;
int MPI_Finalize_ncalls_0;
int MPI_Get_processor_name_ncalls_0;
int MPI_Init_ncalls_0;
int MPI_Initialized_ncalls_0;
int MPI_Wtick_ncalls_0;
int MPI_Wtime_ncalls_0;
int MPI_Address_ncalls_0;
int MPI_Bsend_ncalls_0;
int MPI_Bsend_init_ncalls_0;
int MPI_Buffer_attach_ncalls_0;
int MPI_Buffer_detach_ncalls_0;
int MPI_Cancel_ncalls_0;
int MPI_Request_free_ncalls_0;
int MPI_Recv_init_ncalls_0;
int MPI_Send_init_ncalls_0;
int MPI_Get_elements_ncalls_0;
int MPI_Get_count_ncalls_0;
int MPI_Ibsend_ncalls_0;
int MPI_Iprobe_ncalls_0;
int MPI_Irecv_ncalls_0;
int MPI_Irsend_ncalls_0;
int MPI_Isend_ncalls_0;
int MPI_Issend_ncalls_0;
int MPI_Pack_ncalls_0;
int MPI_Pack_size_ncalls_0;
int MPI_Probe_ncalls_0;
int MPI_Recv_ncalls_0;
int MPI_Rsend_ncalls_0;
int MPI_Rsend_init_ncalls_0;
int MPI_Send_ncalls_0;
int MPI_Sendrecv_ncalls_0;
int MPI_Sendrecv_replace_ncalls_0;
int MPI_Ssend_ncalls_0;
int MPI_Ssend_init_ncalls_0;
int MPI_Start_ncalls_0;
int MPI_Startall_ncalls_0;
int MPI_Test_ncalls_0;
int MPI_Testall_ncalls_0;
int MPI_Testany_ncalls_0;
int MPI_Test_cancelled_ncalls_0;
int MPI_Testsome_ncalls_0;
int MPI_Type_commit_ncalls_0;
int MPI_Type_contiguous_ncalls_0;
int MPI_Type_count_ncalls_0;
int MPI_Type_extent_ncalls_0;
int MPI_Type_free_ncalls_0;
int MPI_Type_hindexed_ncalls_0;
int MPI_Type_hvector_ncalls_0;
int MPI_Type_indexed_ncalls_0;
int MPI_Type_lb_ncalls_0;
int MPI_Type_size_ncalls_0;
int MPI_Type_struct_ncalls_0;
int MPI_Type_ub_ncalls_0;
int MPI_Type_vector_ncalls_0;
int MPI_Unpack_ncalls_0;
int MPI_Wait_ncalls_0;
int MPI_Waitall_ncalls_0;
int MPI_Waitany_ncalls_0;
int MPI_Waitsome_ncalls_0;
int MPI_Cart_coords_ncalls_0;
int MPI_Cart_create_ncalls_0;
int MPI_Cart_get_ncalls_0;
int MPI_Cart_map_ncalls_0;
int MPI_Cart_rank_ncalls_0;
int MPI_Cart_shift_ncalls_0;
int MPI_Cart_sub_ncalls_0;
int MPI_Cartdim_get_ncalls_0;
int MPI_Dims_create_ncalls_0;
int MPI_Graph_create_ncalls_0;
int MPI_Graph_get_ncalls_0;
int MPI_Graph_map_ncalls_0;
int MPI_Graph_neighbors_ncalls_0;
int MPI_Graph_neighbors_count_ncalls_0;
int MPI_Graphdims_get_ncalls_0;
int MPI_Topo_test_ncalls_0;





int MPI_Allgather( sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm )
void * sendbuf ;
int sendcount ;
MPI_Datatype sendtype ;
void * recvbuf ;
int recvcount ;
MPI_Datatype recvtype ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Allgather is being called.\n" );

  
  returnVal = PMPI_Allgather( sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm );


  MPI_Allgather_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Allgatherv( sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm )
void * sendbuf ;
int sendcount ;
MPI_Datatype sendtype ;
void * recvbuf ;
int * recvcounts ;
int * displs ;
MPI_Datatype recvtype ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Allgatherv is being called.\n" );

  
  returnVal = PMPI_Allgatherv( sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm );


  MPI_Allgatherv_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Allreduce( sendbuf, recvbuf, count, datatype, op, comm )
void * sendbuf ;
void * recvbuf ;
int count ;
MPI_Datatype datatype ;
MPI_Op op ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Allreduce is being called.\n" );

  
  returnVal = PMPI_Allreduce( sendbuf, recvbuf, count, datatype, op, comm );


  MPI_Allreduce_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Alltoall( sendbuf, sendcount, sendtype, recvbuf, recvcnt, recvtype, comm )
void * sendbuf ;
int sendcount ;
MPI_Datatype sendtype ;
void * recvbuf ;
int recvcnt ;
MPI_Datatype recvtype ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Alltoall is being called.\n" );

  
  returnVal = PMPI_Alltoall( sendbuf, sendcount, sendtype, recvbuf, recvcnt, recvtype, comm );


  MPI_Alltoall_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Alltoallv( sendbuf, sendcnts, sdispls, sendtype, recvbuf, recvcnts, rdispls, recvtype, comm )
void * sendbuf ;
int * sendcnts ;
int * sdispls ;
MPI_Datatype sendtype ;
void * recvbuf ;
int * recvcnts ;
int * rdispls ;
MPI_Datatype recvtype ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Alltoallv is being called.\n" );

  
  returnVal = PMPI_Alltoallv( sendbuf, sendcnts, sdispls, sendtype, recvbuf, recvcnts, rdispls, recvtype, comm );


  MPI_Alltoallv_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Barrier( comm )
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Barrier is being called.\n" );

  
  returnVal = PMPI_Barrier( comm );


  MPI_Barrier_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Bcast( buffer, count, datatype, root, comm )
void * buffer ;
int count ;
MPI_Datatype datatype ;
int root ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Bcast is being called.\n" );

  
  returnVal = PMPI_Bcast( buffer, count, datatype, root, comm );


  MPI_Bcast_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Gather( sendbuf, sendcnt, sendtype, recvbuf, recvcount, recvtype, root, comm )
void * sendbuf ;
int sendcnt ;
MPI_Datatype sendtype ;
void * recvbuf ;
int recvcount ;
MPI_Datatype recvtype ;
int root ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Gather is being called.\n" );

  
  returnVal = PMPI_Gather( sendbuf, sendcnt, sendtype, recvbuf, recvcount, recvtype, root, comm );


  MPI_Gather_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Gatherv( sendbuf, sendcnt, sendtype, recvbuf, recvcnts, displs, recvtype, root, comm )
void * sendbuf ;
int sendcnt ;
MPI_Datatype sendtype ;
void * recvbuf ;
int * recvcnts ;
int * displs ;
MPI_Datatype recvtype ;
int root ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Gatherv is being called.\n" );

  
  returnVal = PMPI_Gatherv( sendbuf, sendcnt, sendtype, recvbuf, recvcnts, displs, recvtype, root, comm );


  MPI_Gatherv_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Op_create( function, commute, op )
MPI_Uop * function ;
int commute ;
MPI_Op * op ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Op_create is being called.\n" );

  
  returnVal = PMPI_Op_create( function, commute, op );


  MPI_Op_create_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Op_free( op )
MPI_Op * op ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Op_free is being called.\n" );

  
  returnVal = PMPI_Op_free( op );


  MPI_Op_free_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Reduce_scatter( sendbuf, recvbuf, recvcnts, datatype, op, comm )
void * sendbuf ;
void * recvbuf ;
int * recvcnts ;
MPI_Datatype datatype ;
MPI_Op op ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Reduce_scatter is being called.\n" );

  
  returnVal = PMPI_Reduce_scatter( sendbuf, recvbuf, recvcnts, datatype, op, comm );


  MPI_Reduce_scatter_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Reduce( sendbuf, recvbuf, count, datatype, op, root, comm )
void * sendbuf ;
void * recvbuf ;
int count ;
MPI_Datatype datatype ;
MPI_Op op ;
int root ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Reduce is being called.\n" );

  
  returnVal = PMPI_Reduce( sendbuf, recvbuf, count, datatype, op, root, comm );


  MPI_Reduce_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Scan( sendbuf, recvbuf, count, datatype, op, comm )
void * sendbuf ;
void * recvbuf ;
int count ;
MPI_Datatype datatype ;
MPI_Op op ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Scan is being called.\n" );

  
  returnVal = PMPI_Scan( sendbuf, recvbuf, count, datatype, op, comm );


  MPI_Scan_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Scatter( sendbuf, sendcnt, sendtype, recvbuf, recvcnt, recvtype, root, comm )
void * sendbuf ;
int sendcnt ;
MPI_Datatype sendtype ;
void * recvbuf ;
int recvcnt ;
MPI_Datatype recvtype ;
int root ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Scatter is being called.\n" );

  
  returnVal = PMPI_Scatter( sendbuf, sendcnt, sendtype, recvbuf, recvcnt, recvtype, root, comm );


  MPI_Scatter_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Scatterv( sendbuf, sendcnts, displs, sendtype, recvbuf, recvcnt, recvtype, root, comm )
void * sendbuf ;
int * sendcnts ;
int * displs ;
MPI_Datatype sendtype ;
void * recvbuf ;
int recvcnt ;
MPI_Datatype recvtype ;
int root ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Scatterv is being called.\n" );

  
  returnVal = PMPI_Scatterv( sendbuf, sendcnts, displs, sendtype, recvbuf, recvcnt, recvtype, root, comm );


  MPI_Scatterv_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Attr_delete( comm, keyval )
MPI_Comm comm ;
int keyval ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Attr_delete is being called.\n" );

  
  returnVal = PMPI_Attr_delete( comm, keyval );


  MPI_Attr_delete_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Attr_get( comm, keyval, attr_value, flag )
MPI_Comm comm ;
int keyval ;
void ** attr_value ;
int * flag ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Attr_get is being called.\n" );

  
  returnVal = PMPI_Attr_get( comm, keyval, attr_value, flag );


  MPI_Attr_get_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Attr_put( comm, keyval, attr_value )
MPI_Comm comm ;
int keyval ;
void * attr_value ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Attr_put is being called.\n" );

  
  returnVal = PMPI_Attr_put( comm, keyval, attr_value );


  MPI_Attr_put_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_compare( comm1, comm2, result )
MPI_Comm comm1 ;
MPI_Comm comm2 ;
int * result ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_compare is being called.\n" );

  
  returnVal = PMPI_Comm_compare( comm1, comm2, result );


  MPI_Comm_compare_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_create( comm, group, comm_out )
MPI_Comm comm ;
MPI_Group group ;
MPI_Comm * comm_out ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_create is being called.\n" );

  
  returnVal = PMPI_Comm_create( comm, group, comm_out );


  MPI_Comm_create_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_dup( comm, comm_out )
MPI_Comm comm ;
MPI_Comm * comm_out ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_dup is being called.\n" );

  
  returnVal = PMPI_Comm_dup( comm, comm_out );


  MPI_Comm_dup_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_free( comm )
MPI_Comm * comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_free is being called.\n" );

  
  returnVal = PMPI_Comm_free( comm );


  MPI_Comm_free_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_group( comm, group )
MPI_Comm comm ;
MPI_Group * group ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_group is being called.\n" );

  
  returnVal = PMPI_Comm_group( comm, group );


  MPI_Comm_group_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_rank( comm, rank )
MPI_Comm comm ;
int * rank ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_rank is being called.\n" );

  
  returnVal = PMPI_Comm_rank( comm, rank );


  MPI_Comm_rank_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_remote_group( comm, group )
MPI_Comm comm ;
MPI_Group * group ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_remote_group is being called.\n" );

  
  returnVal = PMPI_Comm_remote_group( comm, group );


  MPI_Comm_remote_group_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_remote_size( comm, size )
MPI_Comm comm ;
int * size ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_remote_size is being called.\n" );

  
  returnVal = PMPI_Comm_remote_size( comm, size );


  MPI_Comm_remote_size_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_size( comm, size )
MPI_Comm comm ;
int * size ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_size is being called.\n" );

  
  returnVal = PMPI_Comm_size( comm, size );


  MPI_Comm_size_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_split( comm, color, key, comm_out )
MPI_Comm comm ;
int color ;
int key ;
MPI_Comm * comm_out ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_split is being called.\n" );

  
  returnVal = PMPI_Comm_split( comm, color, key, comm_out );


  MPI_Comm_split_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Comm_test_inter( comm, flag )
MPI_Comm comm ;
int * flag ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Comm_test_inter is being called.\n" );

  
  returnVal = PMPI_Comm_test_inter( comm, flag );


  MPI_Comm_test_inter_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_compare( group1, group2, result )
MPI_Group group1 ;
MPI_Group group2 ;
int * result ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_compare is being called.\n" );

  
  returnVal = PMPI_Group_compare( group1, group2, result );


  MPI_Group_compare_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_difference( group1, group2, group_out )
MPI_Group group1 ;
MPI_Group group2 ;
MPI_Group * group_out ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_difference is being called.\n" );

  
  returnVal = PMPI_Group_difference( group1, group2, group_out );


  MPI_Group_difference_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_excl( group, n, ranks, newgroup )
MPI_Group group ;
int n ;
int * ranks ;
MPI_Group * newgroup ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_excl is being called.\n" );

  
  returnVal = PMPI_Group_excl( group, n, ranks, newgroup );


  MPI_Group_excl_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_free( group )
MPI_Group * group ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_free is being called.\n" );

  
  returnVal = PMPI_Group_free( group );


  MPI_Group_free_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_incl( group, n, ranks, group_out )
MPI_Group group ;
int n ;
int * ranks ;
MPI_Group * group_out ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_incl is being called.\n" );

  
  returnVal = PMPI_Group_incl( group, n, ranks, group_out );


  MPI_Group_incl_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_intersection( group1, group2, group_out )
MPI_Group group1 ;
MPI_Group group2 ;
MPI_Group * group_out ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_intersection is being called.\n" );

  
  returnVal = PMPI_Group_intersection( group1, group2, group_out );


  MPI_Group_intersection_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_rank( group, rank )
MPI_Group group ;
int * rank ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_rank is being called.\n" );

  
  returnVal = PMPI_Group_rank( group, rank );


  MPI_Group_rank_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_range_excl( group, n, ranges, newgroup )
MPI_Group group ;
int n ;
int ranges[][3];
MPI_Group * newgroup ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_range_excl is being called.\n" );

  
  returnVal = PMPI_Group_range_excl( group, n, ranges, newgroup );


  MPI_Group_range_excl_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_range_incl( group, n, ranges, newgroup )
MPI_Group group ;
int n ;
int ranges[][3];
MPI_Group * newgroup ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_range_incl is being called.\n" );

  
  returnVal = PMPI_Group_range_incl( group, n, ranges, newgroup );


  MPI_Group_range_incl_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_size( group, size )
MPI_Group group ;
int * size ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_size is being called.\n" );

  
  returnVal = PMPI_Group_size( group, size );


  MPI_Group_size_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_translate_ranks( group_a, n, ranks_a, group_b, ranks_b )
MPI_Group group_a ;
int n ;
int * ranks_a ;
MPI_Group group_b ;
int * ranks_b ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_translate_ranks is being called.\n" );

  
  returnVal = PMPI_Group_translate_ranks( group_a, n, ranks_a, group_b, ranks_b );


  MPI_Group_translate_ranks_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Group_union( group1, group2, group_out )
MPI_Group group1 ;
MPI_Group group2 ;
MPI_Group * group_out ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Group_union is being called.\n" );

  
  returnVal = PMPI_Group_union( group1, group2, group_out );


  MPI_Group_union_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Intercomm_create( local_comm, local_leader, peer_comm, remote_leader, tag, comm_out )
MPI_Comm local_comm ;
int local_leader ;
MPI_Comm peer_comm ;
int remote_leader ;
int tag ;
MPI_Comm * comm_out ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Intercomm_create is being called.\n" );

  
  returnVal = PMPI_Intercomm_create( local_comm, local_leader, peer_comm, remote_leader, tag, comm_out );


  MPI_Intercomm_create_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Intercomm_merge( comm, high, comm_out )
MPI_Comm comm ;
int high ;
MPI_Comm * comm_out ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Intercomm_merge is being called.\n" );

  
  returnVal = PMPI_Intercomm_merge( comm, high, comm_out );


  MPI_Intercomm_merge_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Keyval_create( copy_fn, delete_fn, keyval, extra_state )
MPI_Copy_function * copy_fn ;
MPI_Delete_function * delete_fn ;
int * keyval ;
void * extra_state ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Keyval_create is being called.\n" );

  
  returnVal = PMPI_Keyval_create( copy_fn, delete_fn, keyval, extra_state );


  MPI_Keyval_create_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Keyval_free( keyval )
int * keyval ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Keyval_free is being called.\n" );

  
  returnVal = PMPI_Keyval_free( keyval );


  MPI_Keyval_free_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Abort( comm, errorcode )
MPI_Comm comm ;
int errorcode ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Abort is being called.\n" );

  
  returnVal = PMPI_Abort( comm, errorcode );


  MPI_Abort_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Error_class( errorcode, errorclass )
int errorcode ;
int * errorclass ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Error_class is being called.\n" );

  
  returnVal = PMPI_Error_class( errorcode, errorclass );


  MPI_Error_class_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Errhandler_create( function, errhandler )
MPI_Handler_function * function ;
MPI_Errhandler * errhandler ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Errhandler_create is being called.\n" );

  
  returnVal = PMPI_Errhandler_create( function, errhandler );


  MPI_Errhandler_create_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Errhandler_free( errhandler )
MPI_Errhandler * errhandler ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Errhandler_free is being called.\n" );

  
  returnVal = PMPI_Errhandler_free( errhandler );


  MPI_Errhandler_free_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Errhandler_get( comm, errhandler )
MPI_Comm comm ;
MPI_Errhandler * errhandler ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Errhandler_get is being called.\n" );

  
  returnVal = PMPI_Errhandler_get( comm, errhandler );


  MPI_Errhandler_get_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Error_string( errorcode, string, resultlen )
int errorcode ;
char * string ;
int * resultlen ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Error_string is being called.\n" );

  
  returnVal = PMPI_Error_string( errorcode, string, resultlen );


  MPI_Error_string_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Errhandler_set( comm, errhandler )
MPI_Comm comm ;
MPI_Errhandler errhandler ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Errhandler_set is being called.\n" );

  
  returnVal = PMPI_Errhandler_set( comm, errhandler );


  MPI_Errhandler_set_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Finalize(  )
{
  int returnVal;
  int i;

  
  printf( "MPI_Finalize is being called.\n" );

  
  returnVal = PMPI_Finalize(  );


  MPI_Finalize_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Get_processor_name( name, resultlen )
char * name ;
int * resultlen ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Get_processor_name is being called.\n" );

  
  returnVal = PMPI_Get_processor_name( name, resultlen );


  MPI_Get_processor_name_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Init( argc, argv )
int * argc ;
char *** argv ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Init is being called.\n" );

  
  returnVal = PMPI_Init( argc, argv );


  MPI_Init_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Initialized( flag )
int * flag ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Initialized is being called.\n" );

  
  returnVal = PMPI_Initialized( flag );


  MPI_Initialized_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

double MPI_Wtick(  )
{
  double returnVal;
  int i;

  
  printf( "MPI_Wtick is being called.\n" );

  
  returnVal = PMPI_Wtick(  );


  MPI_Wtick_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

double MPI_Wtime(  )
{
  double returnVal;
  int i;

  
  printf( "MPI_Wtime is being called.\n" );

  
  returnVal = PMPI_Wtime(  );


  MPI_Wtime_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Address( location, address )
void * location ;
MPI_Aint * address ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Address is being called.\n" );

  
  returnVal = PMPI_Address( location, address );


  MPI_Address_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Bsend( buf, count, datatype, dest, tag, comm )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
{
  int returnVal;
  int i;
  double i1;
  int typesize;

  
  printf( "MPI_Bsend is being called.\n" );

  
  
  

  
  returnVal = PMPI_Bsend( buf, count, datatype, dest, tag, comm );


  MPI_Type_size( datatype, &typesize );
  MPE_Log_send( dest, tag, typesize*count );
  printf( "first argument is buf and i1 went unused (%lf)\n", i1 );
  MPI_Bsend_nsends_0++;



  MPI_Bsend_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Bsend_init( buf, count, datatype, dest, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Bsend_init is being called.\n" );

  
  returnVal = PMPI_Bsend_init( buf, count, datatype, dest, tag, comm, request );


  MPI_Bsend_init_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Buffer_attach( buffer, size )
void * buffer ;
int size ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Buffer_attach is being called.\n" );

  
  returnVal = PMPI_Buffer_attach( buffer, size );


  MPI_Buffer_attach_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Buffer_detach( buffer, size )
void ** buffer ;
int * size ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Buffer_detach is being called.\n" );

  
  returnVal = PMPI_Buffer_detach( buffer, size );


  MPI_Buffer_detach_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Cancel( request )
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Cancel is being called.\n" );

  
  returnVal = PMPI_Cancel( request );


  MPI_Cancel_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Request_free( request )
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Request_free is being called.\n" );

  
  returnVal = PMPI_Request_free( request );


  MPI_Request_free_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Recv_init( buf, count, datatype, source, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int source ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Recv_init is being called.\n" );

  
  returnVal = PMPI_Recv_init( buf, count, datatype, source, tag, comm, request );


  MPI_Recv_init_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Send_init( buf, count, datatype, dest, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Send_init is being called.\n" );

  
  returnVal = PMPI_Send_init( buf, count, datatype, dest, tag, comm, request );


  MPI_Send_init_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Get_elements( status, datatype, elements )
MPI_Status * status ;
MPI_Datatype datatype ;
int * elements ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Get_elements is being called.\n" );

  
  returnVal = PMPI_Get_elements( status, datatype, elements );


  MPI_Get_elements_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Get_count( status, datatype, count )
MPI_Status * status ;
MPI_Datatype datatype ;
int * count ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Get_count is being called.\n" );

  
  returnVal = PMPI_Get_count( status, datatype, count );


  MPI_Get_count_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Ibsend( buf, count, datatype, dest, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Ibsend is being called.\n" );

  
  returnVal = PMPI_Ibsend( buf, count, datatype, dest, tag, comm, request );


  MPI_Ibsend_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Iprobe( source, tag, comm, flag, status )
int source ;
int tag ;
MPI_Comm comm ;
int * flag ;
MPI_Status * status ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Iprobe is being called.\n" );

  
  returnVal = PMPI_Iprobe( source, tag, comm, flag, status );


  MPI_Iprobe_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Irecv( buf, count, datatype, source, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int source ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Irecv is being called.\n" );

  
  returnVal = PMPI_Irecv( buf, count, datatype, source, tag, comm, request );


  MPI_Irecv_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Irsend( buf, count, datatype, dest, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Irsend is being called.\n" );

  
  returnVal = PMPI_Irsend( buf, count, datatype, dest, tag, comm, request );


  MPI_Irsend_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Isend( buf, count, datatype, dest, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;
  double i1;
  int typesize;

  
  printf( "MPI_Isend is being called.\n" );

  
  
  

  
  returnVal = PMPI_Isend( buf, count, datatype, dest, tag, comm, request );


  MPI_Type_size( datatype, &typesize );
  MPE_Log_send( dest, tag, typesize*count );
  printf( "first argument is buf and i1 went unused (%lf)\n", i1 );
  MPI_Isend_nsends_0++;



  MPI_Isend_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Issend( buf, count, datatype, dest, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Issend is being called.\n" );

  
  returnVal = PMPI_Issend( buf, count, datatype, dest, tag, comm, request );


  MPI_Issend_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Pack( inbuf, incount, type, outbuf, outcount, position, comm )
void * inbuf ;
int incount ;
MPI_Datatype type ;
void * outbuf ;
int outcount ;
int * position ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Pack is being called.\n" );

  
  returnVal = PMPI_Pack( inbuf, incount, type, outbuf, outcount, position, comm );


  MPI_Pack_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Pack_size( incount, datatype, comm, size )
int incount ;
MPI_Datatype datatype ;
MPI_Comm comm ;
int * size ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Pack_size is being called.\n" );

  
  returnVal = PMPI_Pack_size( incount, datatype, comm, size );


  MPI_Pack_size_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Probe( source, tag, comm, status )
int source ;
int tag ;
MPI_Comm comm ;
MPI_Status * status ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Probe is being called.\n" );

  
  returnVal = PMPI_Probe( source, tag, comm, status );


  MPI_Probe_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Recv( buf, count, datatype, source, tag, comm, status )
void * buf ;
int count ;
MPI_Datatype datatype ;
int source ;
int tag ;
MPI_Comm comm ;
MPI_Status * status ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Recv is being called.\n" );

  
  returnVal = PMPI_Recv( buf, count, datatype, source, tag, comm, status );


  MPI_Recv_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Rsend( buf, count, datatype, dest, tag, comm )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Rsend is being called.\n" );

  
  returnVal = PMPI_Rsend( buf, count, datatype, dest, tag, comm );


  MPI_Rsend_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Rsend_init( buf, count, datatype, dest, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Rsend_init is being called.\n" );

  
  returnVal = PMPI_Rsend_init( buf, count, datatype, dest, tag, comm, request );


  MPI_Rsend_init_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Send( buf, count, datatype, dest, tag, comm )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
{
  int returnVal;
  int i;
  double i1;
  int typesize;

  
  printf( "MPI_Send is being called.\n" );

  
  
  

  
  returnVal = PMPI_Send( buf, count, datatype, dest, tag, comm );


  MPI_Type_size( datatype, &typesize );
  MPE_Log_send( dest, tag, typesize*count );
  printf( "first argument is buf and i1 went unused (%lf)\n", i1 );
  MPI_Send_nsends_0++;



  MPI_Send_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Sendrecv( sendbuf, sendcount, sendtype, dest, sendtag, recvbuf, recvcount, recvtype, source, recvtag, comm, status )
void * sendbuf ;
int sendcount ;
MPI_Datatype sendtype ;
int dest ;
int sendtag ;
void * recvbuf ;
int recvcount ;
MPI_Datatype recvtype ;
int source ;
int recvtag ;
MPI_Comm comm ;
MPI_Status * status ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Sendrecv is being called.\n" );

  
  returnVal = PMPI_Sendrecv( sendbuf, sendcount, sendtype, dest, sendtag, recvbuf, recvcount, recvtype, source, recvtag, comm, status );


  MPI_Sendrecv_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Sendrecv_replace( buf, count, datatype, dest, sendtag, source, recvtag, comm, status )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int sendtag ;
int source ;
int recvtag ;
MPI_Comm comm ;
MPI_Status * status ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Sendrecv_replace is being called.\n" );

  
  returnVal = PMPI_Sendrecv_replace( buf, count, datatype, dest, sendtag, source, recvtag, comm, status );


  MPI_Sendrecv_replace_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Ssend( buf, count, datatype, dest, tag, comm )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Ssend is being called.\n" );

  
  returnVal = PMPI_Ssend( buf, count, datatype, dest, tag, comm );


  MPI_Ssend_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Ssend_init( buf, count, datatype, dest, tag, comm, request )
void * buf ;
int count ;
MPI_Datatype datatype ;
int dest ;
int tag ;
MPI_Comm comm ;
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Ssend_init is being called.\n" );

  
  returnVal = PMPI_Ssend_init( buf, count, datatype, dest, tag, comm, request );


  MPI_Ssend_init_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Start( request )
MPI_Request * request ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Start is being called.\n" );

  
  returnVal = PMPI_Start( request );


  MPI_Start_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Startall( count, array_of_requests )
int count ;
MPI_Request * array_of_requests ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Startall is being called.\n" );

  
  returnVal = PMPI_Startall( count, array_of_requests );


  MPI_Startall_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Test( request, flag, status )
MPI_Request * request ;
int * flag ;
MPI_Status * status ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Test is being called.\n" );

  
  returnVal = PMPI_Test( request, flag, status );


  MPI_Test_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Testall( count, array_of_requests, flag, array_of_statuses )
int count ;
MPI_Request * array_of_requests ;
int * flag ;
MPI_Status * array_of_statuses ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Testall is being called.\n" );

  
  returnVal = PMPI_Testall( count, array_of_requests, flag, array_of_statuses );


  MPI_Testall_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Testany( count, array_of_requests, index, flag, status )
int count ;
MPI_Request * array_of_requests ;
int * index ;
int * flag ;
MPI_Status * status ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Testany is being called.\n" );

  
  returnVal = PMPI_Testany( count, array_of_requests, index, flag, status );


  MPI_Testany_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Test_cancelled( status, flag )
MPI_Status * status ;
int * flag ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Test_cancelled is being called.\n" );

  
  returnVal = PMPI_Test_cancelled( status, flag );


  MPI_Test_cancelled_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Testsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses )
int incount ;
MPI_Request * array_of_requests ;
int * outcount ;
int * array_of_indices ;
MPI_Status * array_of_statuses ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Testsome is being called.\n" );

  
  returnVal = PMPI_Testsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses );


  MPI_Testsome_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_commit( datatype )
MPI_Datatype * datatype ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_commit is being called.\n" );

  
  returnVal = PMPI_Type_commit( datatype );


  MPI_Type_commit_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_contiguous( count, old_type, newtype )
int count ;
MPI_Datatype old_type ;
MPI_Datatype * newtype ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_contiguous is being called.\n" );

  
  returnVal = PMPI_Type_contiguous( count, old_type, newtype );


  MPI_Type_contiguous_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_count( datatype, count )
MPI_Datatype datatype ;
int * count ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_count is being called.\n" );

  
  returnVal = PMPI_Type_count( datatype, count );


  MPI_Type_count_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_extent( datatype, extent )
MPI_Datatype datatype ;
MPI_Aint * extent ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_extent is being called.\n" );

  
  returnVal = PMPI_Type_extent( datatype, extent );


  MPI_Type_extent_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_free( datatype )
MPI_Datatype * datatype ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_free is being called.\n" );

  
  returnVal = PMPI_Type_free( datatype );


  MPI_Type_free_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_hindexed( count, blocklens, indices, old_type, newtype )
int count ;
int * blocklens ;
MPI_Aint * indices ;
MPI_Datatype old_type ;
MPI_Datatype * newtype ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_hindexed is being called.\n" );

  
  returnVal = PMPI_Type_hindexed( count, blocklens, indices, old_type, newtype );


  MPI_Type_hindexed_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_hvector( count, blocklen, stride, old_type, newtype )
int count ;
int blocklen ;
MPI_Aint stride ;
MPI_Datatype old_type ;
MPI_Datatype * newtype ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_hvector is being called.\n" );

  
  returnVal = PMPI_Type_hvector( count, blocklen, stride, old_type, newtype );


  MPI_Type_hvector_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_indexed( count, blocklens, indices, old_type, newtype )
int count ;
int * blocklens ;
int * indices ;
MPI_Datatype old_type ;
MPI_Datatype * newtype ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_indexed is being called.\n" );

  
  returnVal = PMPI_Type_indexed( count, blocklens, indices, old_type, newtype );


  MPI_Type_indexed_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_lb( datatype, displacement )
MPI_Datatype datatype ;
MPI_Aint * displacement ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_lb is being called.\n" );

  
  returnVal = PMPI_Type_lb( datatype, displacement );


  MPI_Type_lb_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_size( datatype, size )
MPI_Datatype datatype ;
int  * size ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_size is being called.\n" );

  
  returnVal = PMPI_Type_size( datatype, size );


  MPI_Type_size_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_struct( count, blocklens, indices, old_types, newtype )
int count ;
int * blocklens ;
MPI_Aint * indices ;
MPI_Datatype * old_types ;
MPI_Datatype * newtype ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_struct is being called.\n" );

  
  returnVal = PMPI_Type_struct( count, blocklens, indices, old_types, newtype );


  MPI_Type_struct_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_ub( datatype, displacement )
MPI_Datatype datatype ;
MPI_Aint * displacement ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_ub is being called.\n" );

  
  returnVal = PMPI_Type_ub( datatype, displacement );


  MPI_Type_ub_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Type_vector( count, blocklen, stride, old_type, newtype )
int count ;
int blocklen ;
int stride ;
MPI_Datatype old_type ;
MPI_Datatype * newtype ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Type_vector is being called.\n" );

  
  returnVal = PMPI_Type_vector( count, blocklen, stride, old_type, newtype );


  MPI_Type_vector_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Unpack( inbuf, insize, position, outbuf, outcount, type, comm )
void * inbuf ;
int insize ;
int * position ;
void * outbuf ;
int outcount ;
MPI_Datatype type ;
MPI_Comm comm ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Unpack is being called.\n" );

  
  returnVal = PMPI_Unpack( inbuf, insize, position, outbuf, outcount, type, comm );


  MPI_Unpack_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Wait( request, status )
MPI_Request * request ;
MPI_Status * status ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Wait is being called.\n" );

  
  returnVal = PMPI_Wait( request, status );


  MPI_Wait_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Waitall( count, array_of_requests, array_of_statuses )
int count ;
MPI_Request * array_of_requests ;
MPI_Status * array_of_statuses ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Waitall is being called.\n" );

  
  returnVal = PMPI_Waitall( count, array_of_requests, array_of_statuses );


  MPI_Waitall_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Waitany( count, array_of_requests, index, status )
int count ;
MPI_Request * array_of_requests ;
int * index ;
MPI_Status * status ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Waitany is being called.\n" );

  
  returnVal = PMPI_Waitany( count, array_of_requests, index, status );


  MPI_Waitany_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Waitsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses )
int incount ;
MPI_Request * array_of_requests ;
int * outcount ;
int * array_of_indices ;
MPI_Status * array_of_statuses ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Waitsome is being called.\n" );

  
  returnVal = PMPI_Waitsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses );


  MPI_Waitsome_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Cart_coords( comm, rank, maxdims, coords )
MPI_Comm comm ;
int rank ;
int maxdims ;
int * coords ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Cart_coords is being called.\n" );

  
  returnVal = PMPI_Cart_coords( comm, rank, maxdims, coords );


  MPI_Cart_coords_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Cart_create( comm_old, ndims, dims, periods, reorder, comm_cart )
MPI_Comm comm_old ;
int ndims ;
int * dims ;
int * periods ;
int reorder ;
MPI_Comm * comm_cart ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Cart_create is being called.\n" );

  
  returnVal = PMPI_Cart_create( comm_old, ndims, dims, periods, reorder, comm_cart );


  MPI_Cart_create_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Cart_get( comm, maxdims, dims, periods, coords )
MPI_Comm comm ;
int maxdims ;
int * dims ;
int * periods ;
int * coords ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Cart_get is being called.\n" );

  
  returnVal = PMPI_Cart_get( comm, maxdims, dims, periods, coords );


  MPI_Cart_get_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Cart_map( comm_old, ndims, dims, periods, newrank )
MPI_Comm comm_old ;
int ndims ;
int * dims ;
int * periods ;
int * newrank ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Cart_map is being called.\n" );

  
  returnVal = PMPI_Cart_map( comm_old, ndims, dims, periods, newrank );


  MPI_Cart_map_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Cart_rank( comm, coords, rank )
MPI_Comm comm ;
int * coords ;
int * rank ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Cart_rank is being called.\n" );

  
  returnVal = PMPI_Cart_rank( comm, coords, rank );


  MPI_Cart_rank_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Cart_shift( comm, direction, displ, source, dest )
MPI_Comm comm ;
int direction ;
int displ ;
int * source ;
int * dest ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Cart_shift is being called.\n" );

  
  returnVal = PMPI_Cart_shift( comm, direction, displ, source, dest );


  MPI_Cart_shift_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Cart_sub( comm, remain_dims, comm_new )
MPI_Comm comm ;
int * remain_dims ;
MPI_Comm * comm_new ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Cart_sub is being called.\n" );

  
  returnVal = PMPI_Cart_sub( comm, remain_dims, comm_new );


  MPI_Cart_sub_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Cartdim_get( comm, ndims )
MPI_Comm comm ;
int * ndims ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Cartdim_get is being called.\n" );

  
  returnVal = PMPI_Cartdim_get( comm, ndims );


  MPI_Cartdim_get_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Dims_create( nnodes, ndims, dims )
int nnodes ;
int ndims ;
int * dims ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Dims_create is being called.\n" );

  
  returnVal = PMPI_Dims_create( nnodes, ndims, dims );


  MPI_Dims_create_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Graph_create( comm_old, nnodes, index, edges, reorder, comm_graph )
MPI_Comm comm_old ;
int nnodes ;
int * index ;
int * edges ;
int reorder ;
MPI_Comm * comm_graph ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Graph_create is being called.\n" );

  
  returnVal = PMPI_Graph_create( comm_old, nnodes, index, edges, reorder, comm_graph );


  MPI_Graph_create_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Graph_get( comm, maxindex, maxedges, index, edges )
MPI_Comm comm ;
int maxindex ;
int maxedges ;
int * index ;
int * edges ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Graph_get is being called.\n" );

  
  returnVal = PMPI_Graph_get( comm, maxindex, maxedges, index, edges );


  MPI_Graph_get_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Graph_map( comm_old, nnodes, index, edges, newrank )
MPI_Comm comm_old ;
int nnodes ;
int * index ;
int * edges ;
int * newrank ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Graph_map is being called.\n" );

  
  returnVal = PMPI_Graph_map( comm_old, nnodes, index, edges, newrank );


  MPI_Graph_map_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Graph_neighbors( comm, rank, maxneighbors, neighbors )
MPI_Comm comm ;
int rank ;
int * maxneighbors ;
int * neighbors ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Graph_neighbors is being called.\n" );

  
  returnVal = PMPI_Graph_neighbors( comm, rank, maxneighbors, neighbors );


  MPI_Graph_neighbors_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Graph_neighbors_count( comm, rank, nneighbors )
MPI_Comm comm ;
int rank ;
int * nneighbors ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Graph_neighbors_count is being called.\n" );

  
  returnVal = PMPI_Graph_neighbors_count( comm, rank, nneighbors );


  MPI_Graph_neighbors_count_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Graphdims_get( comm, nnodes, nedges )
MPI_Comm comm ;
int * nnodes ;
int * nedges ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Graphdims_get is being called.\n" );

  
  returnVal = PMPI_Graphdims_get( comm, nnodes, nedges );


  MPI_Graphdims_get_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}

int MPI_Topo_test( comm, top_type )
MPI_Comm comm ;
int * top_type ;
{
  int returnVal;
  int i;

  
  printf( "MPI_Topo_test is being called.\n" );

  
  returnVal = PMPI_Topo_test( comm, top_type );


  MPI_Topo_test_ncalls_0++;
  printf( "i unused (%d).\n", i );

  return returnVal;
}
