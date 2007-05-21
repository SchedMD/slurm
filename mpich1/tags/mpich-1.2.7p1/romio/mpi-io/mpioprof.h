/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

/* 
   This header file converts all MPI_ names into PMPI_ names, for 
   building the profiling interface
 */

#ifdef MPIO_BUILD_PROFILING

#undef MPI_File_open
#define MPI_File_open PMPI_File_open
#undef MPI_File_close
#define MPI_File_close PMPI_File_close
#undef MPI_File_delete
#define MPI_File_delete PMPI_File_delete
#undef MPI_File_set_size
#define MPI_File_set_size PMPI_File_set_size
#undef MPI_File_preallocate
#define MPI_File_preallocate PMPI_File_preallocate
#undef MPI_File_get_size
#define MPI_File_get_size PMPI_File_get_size
#undef MPI_File_get_group
#define MPI_File_get_group PMPI_File_get_group
#undef MPI_File_get_amode
#define MPI_File_get_amode PMPI_File_get_amode
#undef MPI_File_set_info
#define MPI_File_set_info PMPI_File_set_info
#undef MPI_File_get_info
#define MPI_File_get_info PMPI_File_get_info

#undef MPI_File_set_view
#define MPI_File_set_view PMPI_File_set_view
#undef MPI_File_get_view
#define MPI_File_get_view PMPI_File_get_view

#undef MPI_File_read_at
#define MPI_File_read_at PMPI_File_read_at
#undef MPI_File_read_at_all
#define MPI_File_read_at_all PMPI_File_read_at_all
#undef MPI_File_write_at
#define MPI_File_write_at PMPI_File_write_at
#undef MPI_File_write_at_all
#define MPI_File_write_at_all PMPI_File_write_at_all
#undef MPI_File_iread_at
#define MPI_File_iread_at PMPI_File_iread_at
#undef MPI_File_iwrite_at
#define MPI_File_iwrite_at PMPI_File_iwrite_at

#undef MPI_File_read
#define MPI_File_read PMPI_File_read
#undef MPI_File_read_all
#define MPI_File_read_all  PMPI_File_read_all 
#undef MPI_File_write
#define MPI_File_write PMPI_File_write
#undef MPI_File_write_all
#define MPI_File_write_all PMPI_File_write_all
#undef MPI_File_iread
#define MPI_File_iread PMPI_File_iread
#undef MPI_File_iwrite
#define MPI_File_iwrite PMPI_File_iwrite
#undef MPI_File_seek
#define MPI_File_seek PMPI_File_seek
#undef MPI_File_get_position
#define MPI_File_get_position PMPI_File_get_position
#undef MPI_File_get_byte_offset
#define MPI_File_get_byte_offset PMPI_File_get_byte_offset

#undef MPI_File_read_shared
#define MPI_File_read_shared PMPI_File_read_shared
#undef MPI_File_write_shared
#define MPI_File_write_shared PMPI_File_write_shared
#undef MPI_File_iread_shared
#define MPI_File_iread_shared PMPI_File_iread_shared
#undef MPI_File_iwrite_shared
#define MPI_File_iwrite_shared PMPI_File_iwrite_shared
#undef MPI_File_read_ordered
#define MPI_File_read_ordered PMPI_File_read_ordered
#undef MPI_File_write_ordered
#define MPI_File_write_ordered PMPI_File_write_ordered
#undef MPI_File_seek_shared
#define MPI_File_seek_shared PMPI_File_seek_shared
#undef MPI_File_get_position_shared
#define MPI_File_get_position_shared PMPI_File_get_position_shared

#undef MPI_File_read_at_all_begin
#define MPI_File_read_at_all_begin PMPI_File_read_at_all_begin
#undef MPI_File_read_at_all_end
#define MPI_File_read_at_all_end PMPI_File_read_at_all_end
#undef MPI_File_write_at_all_begin
#define MPI_File_write_at_all_begin PMPI_File_write_at_all_begin
#undef MPI_File_write_at_all_end
#define MPI_File_write_at_all_end PMPI_File_write_at_all_end
#undef MPI_File_read_all_begin
#define MPI_File_read_all_begin PMPI_File_read_all_begin
#undef MPI_File_read_all_end
#define MPI_File_read_all_end PMPI_File_read_all_end
#undef MPI_File_write_all_begin
#define MPI_File_write_all_begin PMPI_File_write_all_begin
#undef MPI_File_write_all_end
#define MPI_File_write_all_end PMPI_File_write_all_end
#undef MPI_File_read_ordered_begin
#define MPI_File_read_ordered_begin PMPI_File_read_ordered_begin
#undef MPI_File_read_ordered_end
#define MPI_File_read_ordered_end PMPI_File_read_ordered_end
#undef MPI_File_write_ordered_begin
#define MPI_File_write_ordered_begin PMPI_File_write_ordered_begin
#undef MPI_File_write_ordered_end
#define MPI_File_write_ordered_end PMPI_File_write_ordered_end

#undef MPI_File_get_type_extent
#define MPI_File_get_type_extent PMPI_File_get_type_extent
#undef MPI_Register_datarep
#define MPI_Register_datarep PMPI_Register_datarep
#undef MPI_File_set_atomicity
#define MPI_File_set_atomicity PMPI_File_set_atomicity
#undef MPI_File_get_atomicity
#define MPI_File_get_atomicity PMPI_File_get_atomicity
#undef MPI_File_sync
#define MPI_File_sync PMPI_File_sync

#undef MPI_Type_create_subarray
#define MPI_Type_create_subarray PMPI_Type_create_subarray
#undef MPI_Type_create_darray
#define MPI_Type_create_darray PMPI_Type_create_darray

#undef MPI_File_set_errhandler
#define MPI_File_set_errhandler PMPI_File_set_errhandler
#undef MPI_File_get_errhandler
#define MPI_File_get_errhandler PMPI_File_get_errhandler

#if !defined(MPI_File_f2c) || defined(MPICH_RENAMING_MPI_FUNCS)
#undef MPI_File_f2c
#define MPI_File_f2c PMPI_File_f2c
#undef MPI_File_c2f
#define MPI_File_c2f PMPI_File_c2f
#endif

#undef MPIO_Test
#undef PMPIO_Test
#define MPIO_Test PMPIO_Test
#undef MPIO_Wait
#undef PMPIO_Wait
#define MPIO_Wait PMPIO_Wait
#undef MPIO_Testall
#define MPIO_Testall PMPIO_Testall
#undef MPIO_Waitall
#define MPIO_Waitall PMPIO_Waitall
#undef MPIO_Testany
#define MPIO_Testany PMPIO_Testany
#undef MPIO_Waitany
#define MPIO_Waitany PMPIO_Waitany
#undef MPIO_Testsome
#define MPIO_Testsome PMPIO_Testsome
#undef MPIO_Waitsome
#define MPIO_Waitsome PMPIO_Waitsome
#undef MPIO_Request_f2c
#define MPIO_Request_f2c PMPIO_Request_f2c
#undef MPIO_Request_c2f
#define MPIO_Request_c2f PMPIO_Request_c2f

#if defined(HAVE_MPI_INFO_SRC)  /* only in info source directory */

#undef MPI_Info_create
#define MPI_Info_create PMPI_Info_create
#undef MPI_Info_set
#define MPI_Info_set PMPI_Info_set
#undef MPI_Info_delete
#define MPI_Info_delete PMPI_Info_delete
#undef MPI_Info_get
#define MPI_Info_get PMPI_Info_get
#undef MPI_Info_get_valuelen
#define MPI_Info_get_valuelen PMPI_Info_get_valuelen
#undef MPI_Info_get_nkeys
#define MPI_Info_get_nkeys PMPI_Info_get_nkeys
#undef MPI_Info_get_nthkey
#define MPI_Info_get_nthkey PMPI_Info_get_nthkey
#undef MPI_Info_dup
#define MPI_Info_dup PMPI_Info_dup
#undef MPI_Info_free
#define MPI_Info_free PMPI_Info_free
#undef MPI_Info_c2f
#define MPI_Info_c2f PMPI_Info_c2f
#undef MPI_Info_f2c
#define MPI_Info_f2c PMPI_Info_f2c

#endif

#undef MPI_Grequest_start
#define MPI_Grequest_start PMPI_Grequest_start
#undef MPI_Grequest_complete
#define MPI_Grequest_complete PMPI_Grequest_complete
#undef MPI_Status_set_cancelled
#define MPI_Status_set_cancelled PMPI_Status_set_cancelled

#endif
