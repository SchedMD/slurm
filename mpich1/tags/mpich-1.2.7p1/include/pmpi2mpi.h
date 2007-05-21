/* 
   This header file converts all PMPI_ names into MPI_ names
 */
#ifndef PMPI2MPI_H
#define PMPI2MPI_H

#undef PMPI_Abort
#define PMPI_Abort MPI_Abort
#undef PMPI_Address
#define PMPI_Address MPI_Address
#undef PMPI_Allgather
#define PMPI_Allgather MPI_Allgather
#undef PMPI_Allgatherv
#define PMPI_Allgatherv MPI_Allgatherv
#undef PMPI_Allreduce
#define PMPI_Allreduce MPI_Allreduce
#undef PMPI_Alltoall
#define PMPI_Alltoall MPI_Alltoall
#undef PMPI_Alltoallv
#define PMPI_Alltoallv MPI_Alltoallv
#undef PMPI_Attr_delete
#define PMPI_Attr_delete MPI_Attr_delete
#undef PMPI_Attr_get
#define PMPI_Attr_get MPI_Attr_get
#undef PMPI_Attr_put
#define PMPI_Attr_put MPI_Attr_put
#undef PMPI_Barrier
#define PMPI_Barrier MPI_Barrier
#undef PMPI_Bcast
#define PMPI_Bcast MPI_Bcast
#undef PMPI_Bsend
#define PMPI_Bsend MPI_Bsend
#undef PMPI_Bsend_init
#define PMPI_Bsend_init MPI_Bsend_init
#undef PMPI_Buffer_attach
#define PMPI_Buffer_attach MPI_Buffer_attach
#undef PMPI_Buffer_detach
#define PMPI_Buffer_detach MPI_Buffer_detach
#undef PMPI_Cancel
#define PMPI_Cancel MPI_Cancel
#undef PMPI_Cart_coords
#define PMPI_Cart_coords MPI_Cart_coords
#undef PMPI_Cart_create
#define PMPI_Cart_create MPI_Cart_create
#undef PMPI_Cart_get
#define PMPI_Cart_get MPI_Cart_get
#undef PMPI_Cart_map
#define PMPI_Cart_map MPI_Cart_map
#undef PMPI_Cart_rank
#define PMPI_Cart_rank MPI_Cart_rank
#undef PMPI_Cart_shift
#define PMPI_Cart_shift MPI_Cart_shift
#undef PMPI_Cart_sub
#define PMPI_Cart_sub MPI_Cart_sub
#undef PMPI_Cartdim_get
#define PMPI_Cartdim_get MPI_Cartdim_get
#undef PMPI_Comm_compare
#define PMPI_Comm_compare MPI_Comm_compare
#undef PMPI_Comm_create
#define PMPI_Comm_create MPI_Comm_create
#undef PMPI_Comm_dup
#define PMPI_Comm_dup MPI_Comm_dup
#undef PMPI_Comm_free
#define PMPI_Comm_free MPI_Comm_free
#undef PMPI_Comm_group
#define PMPI_Comm_group MPI_Comm_group
#undef PMPI_Comm_rank
#define PMPI_Comm_rank MPI_Comm_rank
#undef PMPI_Comm_remote_group
#define PMPI_Comm_remote_group MPI_Comm_remote_group
#undef PMPI_Comm_remote_size
#define PMPI_Comm_remote_size MPI_Comm_remote_size
#undef PMPI_Comm_size
#define PMPI_Comm_size MPI_Comm_size
#undef PMPI_Comm_split
#define PMPI_Comm_split MPI_Comm_split
#undef PMPI_Comm_test_inter
#define PMPI_Comm_test_inter MPI_Comm_test_inter
#undef PMPI_Dims_create
#define PMPI_Dims_create MPI_Dims_create
#undef PMPI_Errhandler_create
#define PMPI_Errhandler_create MPI_Errhandler_create
#undef PMPI_Errhandler_free
#define PMPI_Errhandler_free MPI_Errhandler_free
#undef PMPI_Errhandler_get
#define PMPI_Errhandler_get MPI_Errhandler_get
#undef PMPI_Errhandler_set
#define PMPI_Errhandler_set MPI_Errhandler_set
#undef PMPI_Error_class
#define PMPI_Error_class MPI_Error_class
#undef PMPI_Error_string
#define PMPI_Error_string MPI_Error_string
#undef PMPI_Finalize
#define PMPI_Finalize MPI_Finalize
#undef PMPI_Gather
#define PMPI_Gather MPI_Gather
#undef PMPI_Gatherv
#define PMPI_Gatherv MPI_Gatherv
#undef PMPI_Get_count
#define PMPI_Get_count MPI_Get_count
#undef PMPI_Get_elements
#define PMPI_Get_elements MPI_Get_elements
#undef PMPI_Get_processor_name
#define PMPI_Get_processor_name MPI_Get_processor_name
#undef PMPI_Get_version
#define PMPI_Get_version MPI_Get_version
#undef PMPI_Graph_create
#define PMPI_Graph_create MPI_Graph_create
#undef PMPI_Graph_get
#define PMPI_Graph_get MPI_Graph_get
#undef PMPI_Graph_map
#define PMPI_Graph_map MPI_Graph_map
#undef PMPI_Graph_neighbors
#define PMPI_Graph_neighbors MPI_Graph_neighbors
#undef PMPI_Graph_neighbors_count
#define PMPI_Graph_neighbors_count MPI_Graph_neighbors_count
#undef PMPI_Graphdims_get
#define PMPI_Graphdims_get MPI_Graphdims_get
#undef PMPI_Group_compare
#define PMPI_Group_compare MPI_Group_compare
#undef PMPI_Group_difference
#define PMPI_Group_difference MPI_Group_difference
#undef PMPI_Group_excl
#define PMPI_Group_excl MPI_Group_excl
#undef PMPI_Group_free
#define PMPI_Group_free MPI_Group_free
#undef PMPI_Group_incl
#define PMPI_Group_incl MPI_Group_incl
#undef PMPI_Group_intersection
#define PMPI_Group_intersection MPI_Group_intersection
#undef PMPI_Group_range_excl
#define PMPI_Group_range_excl MPI_Group_range_excl
#undef PMPI_Group_range_incl
#define PMPI_Group_range_incl MPI_Group_range_incl
#undef PMPI_Group_rank
#define PMPI_Group_rank MPI_Group_rank
#undef PMPI_Group_size
#define PMPI_Group_size MPI_Group_size
#undef PMPI_Group_translate_ranks
#define PMPI_Group_translate_ranks MPI_Group_translate_ranks
#undef PMPI_Group_union
#define PMPI_Group_union MPI_Group_union
#undef PMPI_Ibsend
#define PMPI_Ibsend MPI_Ibsend
#undef PMPI_Info_c2f
#define PMPI_Info_c2f MPI_Info_c2f
#undef PMPI_Info_create
#define PMPI_Info_create MPI_Info_create
#undef PMPI_Info_delete
#define PMPI_Info_delete MPI_Info_delete
#undef PMPI_Info_dup
#define PMPI_Info_dup MPI_Info_dup
#undef PMPI_Info_f2c
#define PMPI_Info_f2c MPI_Info_f2c
#undef PMPI_Info_free
#define PMPI_Info_free MPI_Info_free
#undef PMPI_Info_get
#define PMPI_Info_get MPI_Info_get
#undef PMPI_Info_get_nkeys
#define PMPI_Info_get_nkeys MPI_Info_get_nkeys
#undef PMPI_Info_get_nthkey
#define PMPI_Info_get_nthkey MPI_Info_get_nthkey
#undef PMPI_Info_get_valuelen
#define PMPI_Info_get_valuelen MPI_Info_get_valuelen
#undef PMPI_Info_set
#define PMPI_Info_set MPI_Info_set
#undef PMPI_Init
#define PMPI_Init MPI_Init
#undef PMPI_Initialized
#define PMPI_Initialized MPI_Initialized
#undef PMPI_Intercomm_create
#define PMPI_Intercomm_create MPI_Intercomm_create
#undef PMPI_Intercomm_merge
#define PMPI_Intercomm_merge MPI_Intercomm_merge
#undef PMPI_Iprobe
#define PMPI_Iprobe MPI_Iprobe
#undef PMPI_Irecv
#define PMPI_Irecv MPI_Irecv
#undef PMPI_Irsend
#define PMPI_Irsend MPI_Irsend
#undef PMPI_Isend
#define PMPI_Isend MPI_Isend
#undef PMPI_Issend
#define PMPI_Issend MPI_Issend
#undef PMPI_Keyval_create
#define PMPI_Keyval_create MPI_Keyval_create
#undef PMPI_Keyval_free
#define PMPI_Keyval_free MPI_Keyval_free
#undef PMPI_Name_get
#define PMPI_Name_get MPI_Name_get
#undef PMPI_Name_put
#define PMPI_Name_put MPI_Name_put
#undef PMPI_Op_create
#define PMPI_Op_create MPI_Op_create
#undef PMPI_Op_free
#define PMPI_Op_free MPI_Op_free
#undef PMPI_Pack
#define PMPI_Pack MPI_Pack
#undef PMPI_Pack_size
#define PMPI_Pack_size MPI_Pack_size
#undef PMPI_Pcontrol
#define PMPI_Pcontrol MPI_Pcontrol
#undef PMPI_Probe
#define PMPI_Probe MPI_Probe
#undef PMPI_Recv
#define PMPI_Recv MPI_Recv
#undef PMPI_Recv_init
#define PMPI_Recv_init MPI_Recv_init
#undef PMPI_Reduce
#define PMPI_Reduce MPI_Reduce
#undef PMPI_Reduce_scatter
#define PMPI_Reduce_scatter MPI_Reduce_scatter
#undef PMPI_Request_c2f
#define PMPI_Request_c2f MPI_Request_c2f
#undef PMPI_Request_free
#define PMPI_Request_free MPI_Request_free
#undef PMPI_Rsend
#define PMPI_Rsend MPI_Rsend
#undef PMPI_Rsend_init
#define PMPI_Rsend_init MPI_Rsend_init
#undef PMPI_Scan
#define PMPI_Scan MPI_Scan
#undef PMPI_Scatter
#define PMPI_Scatter MPI_Scatter
#undef PMPI_Scatterv
#define PMPI_Scatterv MPI_Scatterv
#undef PMPI_Send
#define PMPI_Send MPI_Send
#undef PMPI_Send_init
#define PMPI_Send_init MPI_Send_init
#undef PMPI_Sendrecv
#define PMPI_Sendrecv MPI_Sendrecv
#undef PMPI_Sendrecv_replace
#define PMPI_Sendrecv_replace MPI_Sendrecv_replace
#undef PMPI_Ssend
#define PMPI_Ssend MPI_Ssend
#undef PMPI_Ssend_init
#define PMPI_Ssend_init MPI_Ssend_init
#undef PMPI_Start
#define PMPI_Start MPI_Start
#undef PMPI_Startall
#define PMPI_Startall MPI_Startall
#undef PMPI_Status_c2f
#define PMPI_Status_c2f MPI_Status_c2f
#undef PMPI_Status_f2c
#define PMPI_Status_f2c MPI_Status_f2c
#undef PMPI_Test
#define PMPI_Test MPI_Test
#undef PMPI_Test_cancelled
#define PMPI_Test_cancelled MPI_Test_cancelled
#undef PMPI_Testall
#define PMPI_Testall MPI_Testall
#undef PMPI_Testany
#define PMPI_Testany MPI_Testany
#undef PMPI_Testsome
#define PMPI_Testsome MPI_Testsome
#undef PMPI_Topo_status
#define PMPI_Topo_status MPI_Topo_status
#undef PMPI_Topo_test
#define PMPI_Topo_test MPI_Topo_test
#undef PMPI_Type_commit
#define PMPI_Type_commit MPI_Type_commit
#undef PMPI_Type_contiguous
#define PMPI_Type_contiguous MPI_Type_contiguous
#undef PMPI_Type_count
#define PMPI_Type_count MPI_Type_count
#undef PMPI_Type_create_darray
#define PMPI_Type_create_darray MPI_Type_create_darray
#undef PMPI_Type_create_indexed_block
#define PMPI_Type_create_indexed_block MPI_Type_create_indexed_block
#undef PMPI_Type_create_subarray
#define PMPI_Type_create_subarray MPI_Type_create_subarray
#undef PMPI_Type_extent
#define PMPI_Type_extent MPI_Type_extent
#undef PMPI_Type_free
#define PMPI_Type_free MPI_Type_free
#undef PMPI_Type_get_contents
#define PMPI_Type_get_contents MPI_Type_get_contents
#undef PMPI_Type_get_envelope
#define PMPI_Type_get_envelope MPI_Type_get_envelope
#undef PMPI_Type_hindexed
#define PMPI_Type_hindexed MPI_Type_hindexed
#undef PMPI_Type_hvector
#define PMPI_Type_hvector MPI_Type_hvector
#undef PMPI_Type_indexed
#define PMPI_Type_indexed MPI_Type_indexed
#undef PMPI_Type_lb
#define PMPI_Type_lb MPI_Type_lb
#undef PMPI_Type_size
#define PMPI_Type_size MPI_Type_size
#undef PMPI_Type_struct
#define PMPI_Type_struct MPI_Type_struct
#undef PMPI_Type_ub
#define PMPI_Type_ub MPI_Type_ub
#undef PMPI_Type_vector
#define PMPI_Type_vector MPI_Type_vector
#undef PMPI_Unpack
#define PMPI_Unpack MPI_Unpack
#undef PMPI_Wait
#define PMPI_Wait MPI_Wait
#undef PMPI_Waitall
#define PMPI_Waitall MPI_Waitall
#undef PMPI_Waitany
#define PMPI_Waitany MPI_Waitany
#undef PMPI_Waitsome
#define PMPI_Waitsome MPI_Waitsome
#undef PMPI_Wtick
#define PMPI_Wtick MPI_Wtick
#undef PMPI_Wtime
#define PMPI_Wtime MPI_Wtime

/* The Fortran versions are handled directly by the Fortran wrappers */

/* MPI-2 functions */
#undef PMPI_Add_error_class
#define PMPI_Add_error_class MPI_Add_error_class
#undef PMPI_Alloc_mem
#define PMPI_Alloc_mem MPI_Alloc_mem
#undef PMPI_Close_port
#define PMPI_Close_port MPI_Close_port
#undef PMPI_Comm_call_errhandler
#define PMPI_Comm_call_errhandler MPI_Comm_call_errhandler
#undef PMPI_Comm_clone
#define PMPI_Comm_clone MPI_Comm_clone
#undef PMPI_Comm_connect
#define PMPI_Comm_connect MPI_Comm_connect
#undef PMPI_Comm_create_errhandler
#define PMPI_Comm_create_errhandler MPI_Comm_create_errhandler
#undef PMPI_Comm_create_keyval
#define PMPI_Comm_create_keyval MPI_Comm_create_keyval
#undef PMPI_Comm_disconnect
#define PMPI_Comm_disconnect MPI_Comm_disconnect
#undef PMPI_Comm_free_keyval
#define PMPI_Comm_free_keyval MPI_Comm_free_keyval
#undef PMPI_Comm_get_errhandler
#define PMPI_Comm_get_errhandler MPI_Comm_get_errhandler
#undef PMPI_Comm_get_name
#define PMPI_Comm_get_name MPI_Comm_get_name
#undef PMPI_Comm_get_parent
#define PMPI_Comm_get_parent MPI_Comm_get_parent
#undef PMPI_Comm_join
#define PMPI_Comm_join MPI_Comm_join
#undef PMPI_Comm_set_errhandler
#define PMPI_Comm_set_errhandler MPI_Comm_set_errhandler
#undef PMPI_Comm_set_name
#define PMPI_Comm_set_name MPI_Comm_set_name
#undef PMPI_Comm_spawn
#define PMPI_Comm_spawn MPI_Comm_spawn
#undef PMPI_Comm_spawn_multiple
#define PMPI_Comm_spawn_multiple MPI_Comm_spawn_multiple
#undef PMPI_Exscan
#define PMPI_Exscan MPI_Exscan
#undef PMPI_File_call_errhandler
#define PMPI_File_call_errhandler MPI_File_call_errhandler
#undef PMPI_File_close
#define PMPI_File_close MPI_File_close
#undef PMPI_File_create_errhandler
#define PMPI_File_create_errhandler MPI_File_create_errhandler
#undef PMPI_File_delete
#define PMPI_File_delete MPI_File_delete
#undef PMPI_File_get_amode
#define PMPI_File_get_amode MPI_File_get_amode
#undef PMPI_File_get_atomicity
#define PMPI_File_get_atomicity MPI_File_get_atomicity
#undef PMPI_File_get_byte_offset
#define PMPI_File_get_byte_offset MPI_File_get_byte_offset
#undef PMPI_File_get_errhandler
#define PMPI_File_get_errhandler MPI_File_get_errhandler
#undef PMPI_File_get_group
#define PMPI_File_get_group MPI_File_get_group
#undef PMPI_File_get_info
#define PMPI_File_get_info MPI_File_get_info
#undef PMPI_File_get_position
#define PMPI_File_get_position MPI_File_get_position
#undef PMPI_File_get_position_shared
#define PMPI_File_get_position_shared MPI_File_get_position_shared
#undef PMPI_File_get_size
#define PMPI_File_get_size MPI_File_get_size
#undef PMPI_File_get_type_extent
#define PMPI_File_get_type_extent MPI_File_get_type_extent
#undef PMPI_File_get_view
#define PMPI_File_get_view MPI_File_get_view
#undef PMPI_File_iread
#define PMPI_File_iread MPI_File_iread
#undef PMPI_File_iread_at
#define PMPI_File_iread_at MPI_File_iread_at
#undef PMPI_File_iread_shared
#define PMPI_File_iread_shared MPI_File_iread_shared
#undef PMPI_File_iwrite
#define PMPI_File_iwrite MPI_File_iwrite
#undef PMPI_File_iwrite_at
#define PMPI_File_iwrite_at MPI_File_iwrite_at
#undef PMPI_File_iwrite_shared
#define PMPI_File_iwrite_shared MPI_File_iwrite_shared
#undef PMPI_File_open
#define PMPI_File_open MPI_File_open
#undef PMPI_File_preallocate
#define PMPI_File_preallocate MPI_File_preallocate
#undef PMPI_File_read
#define PMPI_File_read MPI_File_read
#undef PMPI_File_read_all_begin
#define PMPI_File_read_all_begin MPI_File_read_all_begin
#undef PMPI_File_read_at
#define PMPI_File_read_at MPI_File_read_at
#undef PMPI_File_read_at_all_begin
#define PMPI_File_read_at_all_begin MPI_File_read_at_all_begin
#undef PMPI_File_read_ordered
#define PMPI_File_read_ordered MPI_File_read_ordered
#undef PMPI_File_read_ordered_begin
#define PMPI_File_read_ordered_begin MPI_File_read_ordered_begin
#undef PMPI_File_read_shared
#define PMPI_File_read_shared MPI_File_read_shared
#undef PMPI_File_seek
#define PMPI_File_seek MPI_File_seek
#undef PMPI_File_seek_shared
#define PMPI_File_seek_shared MPI_File_seek_shared
#undef PMPI_File_set_atomicity
#define PMPI_File_set_atomicity MPI_File_set_atomicity
#undef PMPI_File_set_errhandler
#define PMPI_File_set_errhandler MPI_File_set_errhandler
#undef PMPI_File_set_info
#define PMPI_File_set_info MPI_File_set_info
#undef PMPI_File_set_size
#define PMPI_File_set_size MPI_File_set_size
#undef PMPI_File_set_view
#define PMPI_File_set_view MPI_File_set_view
#undef PMPI_File_sync
#define PMPI_File_sync MPI_File_sync
#undef PMPI_File_write
#define PMPI_File_write MPI_File_write
#undef PMPI_File_write_all_begin
#define PMPI_File_write_all_begin MPI_File_write_all_begin
#undef PMPI_File_write_at
#define PMPI_File_write_at MPI_File_write_at
#undef PMPI_File_write_at_all_begin
#define PMPI_File_write_at_all_begin MPI_File_write_at_all_begin
#undef PMPI_File_write_ordered
#define PMPI_File_write_ordered MPI_File_write_ordered
#undef PMPI_File_write_ordered_begin
#define PMPI_File_write_ordered_begin MPI_File_write_ordered_begin
#undef PMPI_File_write_shared
#define PMPI_File_write_shared MPI_File_write_shared
#undef PMPI_Finalized
#define PMPI_Finalized MPI_Finalized
#undef PMPI_Free_mem
#define PMPI_Free_mem MPI_Free_mem
#undef PMPI_Get
#define PMPI_Get MPI_Get
#undef PMPI_Get_address
#define PMPI_Get_address MPI_Get_address
#undef PMPI_Get_version
#define PMPI_Get_version MPI_Get_version
#undef PMPI_Grequest_complete
#define PMPI_Grequest_complete MPI_Grequest_complete
#undef PMPI_Info_create
#define PMPI_Info_create MPI_Info_create
#undef PMPI_Info_free
#define PMPI_Info_free MPI_Info_free
#undef PMPI_Init_thread
#define PMPI_Init_thread MPI_Init_thread
#undef PMPI_Lookup_name
#define PMPI_Lookup_name MPI_Lookup_name
#undef PMPI_Open_port
#define PMPI_Open_port MPI_Open_port
#undef PMPI_Pack_external
#define PMPI_Pack_external MPI_Pack_external
#undef PMPI_Publish_name
#define PMPI_Publish_name MPI_Publish_name
#undef PMPI_Put
#define PMPI_Put MPI_Put
#undef PMPI_Query_thread
#define PMPI_Query_thread MPI_Query_thread
#undef PMPI_Register_datarep
#define PMPI_Register_datarep MPI_Register_datarep
#undef PMPI_Request_get_status
#define PMPI_Request_get_status MPI_Request_get_status
#undef PMPI_Sizeof
#define PMPI_Sizeof MPI_Sizeof
#undef PMPI_Status_set_cancelled
#define PMPI_Status_set_cancelled MPI_Status_set_cancelled
#undef PMPI_Status_set_elements
#define PMPI_Status_set_elements  MPI_Status_set_elements
#undef PMPI_Type_create_darray
#define PMPI_Type_create_darray MPI_Type_create_darray
#undef PMPI_Type_create_hindexed
#define PMPI_Type_create_hindexed MPI_Type_create_hindexed
#undef PMPI_Type_create_indexed_block
#define PMPI_Type_create_indexed_block MPI_Type_create_indexed_block
#undef PMPI_Type_create_keyval
#define PMPI_Type_create_keyval MPI_Type_create_keyval
#undef PMPI_Type_create_resized
#define PMPI_Type_create_resized MPI_Type_create_resized
#undef PMPI_Type_create_struct
#define PMPI_Type_create_struct MPI_Type_create_struct
#undef PMPI_Type_create_subarray
#define PMPI_Type_create_subarray MPI_Type_create_subarray
#undef PMPI_Type_delete_attr
#define PMPI_Type_delete_attr MPI_Type_delete_attr
#undef PMPI_Type_dup
#define PMPI_Type_dup MPI_Type_dup
#undef PMPI_Type_free_keyval
#define PMPI_Type_free_keyval MPI_Type_free_keyval
#undef PMPI_Type_get_contents
#define PMPI_Type_get_contents MPI_Type_get_contents
#undef PMPI_Type_get_extent
#define PMPI_Type_get_extent MPI_Type_get_extent
#undef PMPI_Type_get_name
#define PMPI_Type_get_name MPI_Type_get_name
#undef PMPI_Type_get_true_extent
#define PMPI_Type_get_true_extent MPI_Type_get_true_extent
#undef PMPI_Type_match_size
#define PMPI_Type_match_size MPI_Type_match_size
#undef PMPI_Type_set_name
#define PMPI_Type_set_name MPI_Type_set_name
#undef PMPI_Unpack_external
#define PMPI_Unpack_external MPI_Unpack_external
#undef PMPI_Unpublish_name
#define PMPI_Unpublish_name MPI_Unpublish_name
#undef PMPI_Win_call_errhandler
#define PMPI_Win_call_errhandler MPI_Win_call_errhandler
#undef PMPI_Win_complete
#define PMPI_Win_complete MPI_Win_complete
#undef PMPI_Win_create
#define PMPI_Win_create MPI_Win_create
#undef PMPI_Win_create_errhandler
#define PMPI_Win_create_errhandler MPI_Win_create_errhandler
#undef PMPI_Win_create_keyval
#define PMPI_Win_create_keyval MPI_Win_create_keyval
#undef PMPI_Win_fence
#define PMPI_Win_fence MPI_Win_fence
#undef PMPI_Win_free
#define PMPI_Win_free MPI_Win_free
#undef PMPI_Win_free_keyval
#define PMPI_Win_free_keyval MPI_Win_free_keyval
#undef PMPI_Win_get_errhandler
#define PMPI_Win_get_errhandler MPI_Win_get_errhandler
#undef PMPI_Win_get_group
#define PMPI_Win_get_group MPI_Win_get_group
#undef PMPI_Win_get_name
#define PMPI_Win_get_name MPI_Win_get_name
#undef PMPI_Win_lock
#define PMPI_Win_lock MPI_Win_lock
#undef PMPI_Win_post
#define PMPI_Win_post MPI_Win_post
#undef PMPI_Win_set_attr
#define PMPI_Win_set_attr MPI_Win_set_attr
#undef PMPI_Win_set_errhandler
#define PMPI_Win_set_errhandler MPI_Win_set_errhandler
#undef PMPI_Win_set_name
#define PMPI_Win_set_name MPI_Win_set_name
#undef PMPI_Win_start
#define PMPI_Win_start MPI_Win_start
#undef PMPI_Win_unlock
#define PMPI_Win_unlock MPI_Win_unlock
#undef PMPI_Win_wait
#define PMPI_Win_wait MPI_Win_wait

#endif
