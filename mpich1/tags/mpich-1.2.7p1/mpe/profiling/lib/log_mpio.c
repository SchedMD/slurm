#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include <stdio.h>
#include "mpi.h"
#include "mpe.h"

static int MPI_File_open_stateid_0=0,MPI_File_open_ncalls_0=0;
static int MPI_File_close_stateid_0=1,MPI_File_close_ncalls_0=0;
static int MPI_File_delete_stateid_0=2,MPI_File_delete_ncalls_0=0;
static int MPI_File_set_size_stateid_0=3,MPI_File_set_size_ncalls_0=0;
static int MPI_File_preallocate_stateid_0=4,MPI_File_preallocate_ncalls_0=0;
static int MPI_File_get_size_stateid_0=5,MPI_File_get_size_ncalls_0=0;
static int MPI_File_get_group_stateid_0=6,MPI_File_get_group_ncalls_0=0;
static int MPI_File_get_amode_stateid_0=7,MPI_File_get_amode_ncalls_0=0;
static int MPI_File_set_info_stateid_0=8,MPI_File_set_info_ncalls_0=0;
static int MPI_File_get_info_stateid_0=9,MPI_File_get_info_ncalls_0=0;
static int MPI_File_set_view_stateid_0=10,MPI_File_set_view_ncalls_0=0;
static int MPI_File_get_view_stateid_0=11,MPI_File_get_view_ncalls_0=0;
static int MPI_File_read_at_stateid_0=12,MPI_File_read_at_ncalls_0=0;
static int MPI_File_read_at_all_stateid_0=13,MPI_File_read_at_all_ncalls_0=0;
static int MPI_File_write_at_stateid_0=14,MPI_File_write_at_ncalls_0=0;
static int MPI_File_write_at_all_stateid_0=15,MPI_File_write_at_all_ncalls_0=0;
static int MPI_File_iread_at_stateid_0=16,MPI_File_iread_at_ncalls_0=0;
static int MPI_File_iwrite_at_stateid_0=17,MPI_File_iwrite_at_ncalls_0=0;
static int MPI_File_read_stateid_0=18,MPI_File_read_ncalls_0=0;
static int MPI_File_read_all_stateid_0=19,MPI_File_read_all_ncalls_0=0;
static int MPI_File_write_stateid_0=20,MPI_File_write_ncalls_0=0;
static int MPI_File_write_all_stateid_0=21,MPI_File_write_all_ncalls_0=0;
static int MPI_File_iread_stateid_0=22,MPI_File_iread_ncalls_0=0;
static int MPI_File_iwrite_stateid_0=23,MPI_File_iwrite_ncalls_0=0;
static int MPI_File_seek_stateid_0=24,MPI_File_seek_ncalls_0=0;
static int MPI_File_get_position_stateid_0=25,MPI_File_get_position_ncalls_0=0;
static int MPI_File_get_byte_offset_stateid_0=26,MPI_File_get_byte_offset_ncalls_0=0;
static int MPI_File_read_shared_stateid_0=27,MPI_File_read_shared_ncalls_0=0;
static int MPI_File_write_shared_stateid_0=28,MPI_File_write_shared_ncalls_0=0;
static int MPI_File_iread_shared_stateid_0=29,MPI_File_iread_shared_ncalls_0=0;
static int MPI_File_iwrite_shared_stateid_0=30,MPI_File_iwrite_shared_ncalls_0=0;
static int MPI_File_read_ordered_stateid_0=31,MPI_File_read_ordered_ncalls_0=0;
static int MPI_File_write_ordered_stateid_0=32,MPI_File_write_ordered_ncalls_0=0;
static int MPI_File_seek_shared_stateid_0=33,MPI_File_seek_shared_ncalls_0=0;
static int MPI_File_get_position_shared_stateid_0=34,MPI_File_get_position_shared_ncalls_0=0;
static int MPI_File_read_at_all_begin_stateid_0=35,MPI_File_read_at_all_begin_ncalls_0=0;
static int MPI_File_read_at_all_end_stateid_0=36,MPI_File_read_at_all_end_ncalls_0=0;
static int MPI_File_write_at_all_begin_stateid_0=37,MPI_File_write_at_all_begin_ncalls_0=0;
static int MPI_File_write_at_all_end_stateid_0=38,MPI_File_write_at_all_end_ncalls_0=0;
static int MPI_File_read_all_begin_stateid_0=39,MPI_File_read_all_begin_ncalls_0=0;
static int MPI_File_read_all_end_stateid_0=40,MPI_File_read_all_end_ncalls_0=0;
static int MPI_File_write_all_begin_stateid_0=41,MPI_File_write_all_begin_ncalls_0=0;
static int MPI_File_write_all_end_stateid_0=42,MPI_File_write_all_end_ncalls_0=0;
static int MPI_File_read_ordered_begin_stateid_0=43,MPI_File_read_ordered_begin_ncalls_0=0;
static int MPI_File_read_ordered_end_stateid_0=44,MPI_File_read_ordered_end_ncalls_0=0;
static int MPI_File_write_ordered_begin_stateid_0=45,MPI_File_write_ordered_begin_ncalls_0=0;
static int MPI_File_write_ordered_end_stateid_0=46,MPI_File_write_ordered_end_ncalls_0=0;
static int MPI_File_get_type_extent_stateid_0=47,MPI_File_get_type_extent_ncalls_0=0;
static int MPI_Register_datarep_stateid_0=48,MPI_Register_datarep_ncalls_0=0;
static int MPI_File_set_atomicity_stateid_0=49,MPI_File_set_atomicity_ncalls_0=0;
static int MPI_File_get_atomicity_stateid_0=50,MPI_File_get_atomicity_ncalls_0=0;
static int MPI_File_sync_stateid_0=51,MPI_File_sync_ncalls_0=0;


static int procid_0;
static char logFileName_0[256];










int MPI_File_open( MPI_Comm  comm,char * filename,int  amode,MPI_Info  info,MPI_File * fh  )
{
  int returnVal;

/*
    MPI_File_open - prototyping replacement for MPI_File_open
    Log the beginning and ending of the time spent in MPI_File_open calls.
*/

  ++MPI_File_open_ncalls_0;
  MPE_Log_event( MPI_File_open_stateid_0*2,
	         MPI_File_open_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_open( comm, filename, amode, info, fh );

  MPE_Log_event( MPI_File_open_stateid_0*2+1,
	         MPI_File_open_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_close( MPI_File * fh  )
{
  int returnVal;

/*
    MPI_File_close - prototyping replacement for MPI_File_close
    Log the beginning and ending of the time spent in MPI_File_close calls.
*/

  ++MPI_File_close_ncalls_0;
  MPE_Log_event( MPI_File_close_stateid_0*2,
	         MPI_File_close_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_close( fh );

  MPE_Log_event( MPI_File_close_stateid_0*2+1,
	         MPI_File_close_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_delete( char * filename,MPI_Info  info  )
{
  int returnVal;

/*
    MPI_File_delete - prototyping replacement for MPI_File_delete
    Log the beginning and ending of the time spent in MPI_File_delete calls.
*/

  ++MPI_File_delete_ncalls_0;
  MPE_Log_event( MPI_File_delete_stateid_0*2,
	         MPI_File_delete_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_delete( filename, info );

  MPE_Log_event( MPI_File_delete_stateid_0*2+1,
	         MPI_File_delete_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_set_size( MPI_File  fh,MPI_Offset  size  )
{
  int returnVal;

/*
    MPI_File_set_size - prototyping replacement for MPI_File_set_size
    Log the beginning and ending of the time spent in MPI_File_set_size calls.
*/

  ++MPI_File_set_size_ncalls_0;
  MPE_Log_event( MPI_File_set_size_stateid_0*2,
	         MPI_File_set_size_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_set_size( fh, size );

  MPE_Log_event( MPI_File_set_size_stateid_0*2+1,
	         MPI_File_set_size_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_preallocate( MPI_File  fh,MPI_Offset  size  )
{
  int returnVal;

/*
    MPI_File_preallocate - prototyping replacement for MPI_File_preallocate
    Log the beginning and ending of the time spent in MPI_File_preallocate calls.
*/

  ++MPI_File_preallocate_ncalls_0;
  MPE_Log_event( MPI_File_preallocate_stateid_0*2,
	         MPI_File_preallocate_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_preallocate( fh, size );

  MPE_Log_event( MPI_File_preallocate_stateid_0*2+1,
	         MPI_File_preallocate_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_size( MPI_File  fh,MPI_Offset * size  )
{
  int returnVal;

/*
    MPI_File_get_size - prototyping replacement for MPI_File_get_size
    Log the beginning and ending of the time spent in MPI_File_get_size calls.
*/

  ++MPI_File_get_size_ncalls_0;
  MPE_Log_event( MPI_File_get_size_stateid_0*2,
	         MPI_File_get_size_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_size( fh, size );

  MPE_Log_event( MPI_File_get_size_stateid_0*2+1,
	         MPI_File_get_size_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_group( MPI_File  fh,MPI_Group * group  )
{
  int returnVal;

/*
    MPI_File_get_group - prototyping replacement for MPI_File_get_group
    Log the beginning and ending of the time spent in MPI_File_get_group calls.
*/

  ++MPI_File_get_group_ncalls_0;
  MPE_Log_event( MPI_File_get_group_stateid_0*2,
	         MPI_File_get_group_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_group( fh, group );

  MPE_Log_event( MPI_File_get_group_stateid_0*2+1,
	         MPI_File_get_group_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_amode( MPI_File  fh,int * amode  )
{
  int returnVal;

/*
    MPI_File_get_amode - prototyping replacement for MPI_File_get_amode
    Log the beginning and ending of the time spent in MPI_File_get_amode calls.
*/

  ++MPI_File_get_amode_ncalls_0;
  MPE_Log_event( MPI_File_get_amode_stateid_0*2,
	         MPI_File_get_amode_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_amode( fh, amode );

  MPE_Log_event( MPI_File_get_amode_stateid_0*2+1,
	         MPI_File_get_amode_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_set_info( MPI_File  fh,MPI_Info  info  )
{
  int returnVal;

/*
    MPI_File_set_info - prototyping replacement for MPI_File_set_info
    Log the beginning and ending of the time spent in MPI_File_set_info calls.
*/

  ++MPI_File_set_info_ncalls_0;
  MPE_Log_event( MPI_File_set_info_stateid_0*2,
	         MPI_File_set_info_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_set_info( fh, info );

  MPE_Log_event( MPI_File_set_info_stateid_0*2+1,
	         MPI_File_set_info_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_info( MPI_File  fh,MPI_Info * info_used  )
{
  int returnVal;

/*
    MPI_File_get_info - prototyping replacement for MPI_File_get_info
    Log the beginning and ending of the time spent in MPI_File_get_info calls.
*/

  ++MPI_File_get_info_ncalls_0;
  MPE_Log_event( MPI_File_get_info_stateid_0*2,
	         MPI_File_get_info_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_info( fh, info_used );

  MPE_Log_event( MPI_File_get_info_stateid_0*2+1,
	         MPI_File_get_info_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_set_view( MPI_File  fh,MPI_Offset  disp,MPI_Datatype  etype,MPI_Datatype  filetype,char * datarep,MPI_Info  info  )
{
  int returnVal;

/*
    MPI_File_set_view - prototyping replacement for MPI_File_set_view
    Log the beginning and ending of the time spent in MPI_File_set_view calls.
*/

  ++MPI_File_set_view_ncalls_0;
  MPE_Log_event( MPI_File_set_view_stateid_0*2,
	         MPI_File_set_view_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_set_view( fh, disp, etype, filetype, datarep, info );

  MPE_Log_event( MPI_File_set_view_stateid_0*2+1,
	         MPI_File_set_view_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_view( MPI_File  fh,MPI_Offset * disp,MPI_Datatype * etype,MPI_Datatype * filetype,char * datarep  )
{
  int returnVal;

/*
    MPI_File_get_view - prototyping replacement for MPI_File_get_view
    Log the beginning and ending of the time spent in MPI_File_get_view calls.
*/

  ++MPI_File_get_view_ncalls_0;
  MPE_Log_event( MPI_File_get_view_stateid_0*2,
	         MPI_File_get_view_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_view( fh, disp, etype, filetype, datarep );

  MPE_Log_event( MPI_File_get_view_stateid_0*2+1,
	         MPI_File_get_view_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_at( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_at - prototyping replacement for MPI_File_read_at
    Log the beginning and ending of the time spent in MPI_File_read_at calls.
*/

  ++MPI_File_read_at_ncalls_0;
  MPE_Log_event( MPI_File_read_at_stateid_0*2,
	         MPI_File_read_at_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_at( fh, offset, buf, count, datatype, status );

  MPE_Log_event( MPI_File_read_at_stateid_0*2+1,
	         MPI_File_read_at_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_at_all( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_at_all - prototyping replacement for MPI_File_read_at_all
    Log the beginning and ending of the time spent in MPI_File_read_at_all calls.
*/

  ++MPI_File_read_at_all_ncalls_0;
  MPE_Log_event( MPI_File_read_at_all_stateid_0*2,
	         MPI_File_read_at_all_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_at_all( fh, offset, buf, count, datatype, status );

  MPE_Log_event( MPI_File_read_at_all_stateid_0*2+1,
	         MPI_File_read_at_all_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_at( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_at - prototyping replacement for MPI_File_write_at
    Log the beginning and ending of the time spent in MPI_File_write_at calls.
*/

  ++MPI_File_write_at_ncalls_0;
  MPE_Log_event( MPI_File_write_at_stateid_0*2,
	         MPI_File_write_at_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_at( fh, offset, buf, count, datatype, status );

  MPE_Log_event( MPI_File_write_at_stateid_0*2+1,
	         MPI_File_write_at_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_at_all( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_at_all - prototyping replacement for MPI_File_write_at_all
    Log the beginning and ending of the time spent in MPI_File_write_at_all calls.
*/

  ++MPI_File_write_at_all_ncalls_0;
  MPE_Log_event( MPI_File_write_at_all_stateid_0*2,
	         MPI_File_write_at_all_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_at_all( fh, offset, buf, count, datatype, status );

  MPE_Log_event( MPI_File_write_at_all_stateid_0*2+1,
	         MPI_File_write_at_all_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_iread_at( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iread_at - prototyping replacement for MPI_File_iread_at
    Log the beginning and ending of the time spent in MPI_File_iread_at calls.
*/

  ++MPI_File_iread_at_ncalls_0;
  MPE_Log_event( MPI_File_iread_at_stateid_0*2,
	         MPI_File_iread_at_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_iread_at( fh, offset, buf, count, datatype, request );

  MPE_Log_event( MPI_File_iread_at_stateid_0*2+1,
	         MPI_File_iread_at_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_iwrite_at( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iwrite_at - prototyping replacement for MPI_File_iwrite_at
    Log the beginning and ending of the time spent in MPI_File_iwrite_at calls.
*/

  ++MPI_File_iwrite_at_ncalls_0;
  MPE_Log_event( MPI_File_iwrite_at_stateid_0*2,
	         MPI_File_iwrite_at_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_iwrite_at( fh, offset, buf, count, datatype, request );

  MPE_Log_event( MPI_File_iwrite_at_stateid_0*2+1,
	         MPI_File_iwrite_at_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read - prototyping replacement for MPI_File_read
    Log the beginning and ending of the time spent in MPI_File_read calls.
*/

  ++MPI_File_read_ncalls_0;
  MPE_Log_event( MPI_File_read_stateid_0*2,
	         MPI_File_read_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read( fh, buf, count, datatype, status );

  MPE_Log_event( MPI_File_read_stateid_0*2+1,
	         MPI_File_read_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_all( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_all - prototyping replacement for MPI_File_read_all
    Log the beginning and ending of the time spent in MPI_File_read_all calls.
*/

  ++MPI_File_read_all_ncalls_0;
  MPE_Log_event( MPI_File_read_all_stateid_0*2,
	         MPI_File_read_all_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_all( fh, buf, count, datatype, status );

  MPE_Log_event( MPI_File_read_all_stateid_0*2+1,
	         MPI_File_read_all_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write - prototyping replacement for MPI_File_write
    Log the beginning and ending of the time spent in MPI_File_write calls.
*/

  ++MPI_File_write_ncalls_0;
  MPE_Log_event( MPI_File_write_stateid_0*2,
	         MPI_File_write_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write( fh, buf, count, datatype, status );

  MPE_Log_event( MPI_File_write_stateid_0*2+1,
	         MPI_File_write_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_all( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_all - prototyping replacement for MPI_File_write_all
    Log the beginning and ending of the time spent in MPI_File_write_all calls.
*/

  ++MPI_File_write_all_ncalls_0;
  MPE_Log_event( MPI_File_write_all_stateid_0*2,
	         MPI_File_write_all_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_all( fh, buf, count, datatype, status );

  MPE_Log_event( MPI_File_write_all_stateid_0*2+1,
	         MPI_File_write_all_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_iread( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iread - prototyping replacement for MPI_File_iread
    Log the beginning and ending of the time spent in MPI_File_iread calls.
*/

  ++MPI_File_iread_ncalls_0;
  MPE_Log_event( MPI_File_iread_stateid_0*2,
	         MPI_File_iread_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_iread( fh, buf, count, datatype, request );

  MPE_Log_event( MPI_File_iread_stateid_0*2+1,
	         MPI_File_iread_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_iwrite( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iwrite - prototyping replacement for MPI_File_iwrite
    Log the beginning and ending of the time spent in MPI_File_iwrite calls.
*/

  ++MPI_File_iwrite_ncalls_0;
  MPE_Log_event( MPI_File_iwrite_stateid_0*2,
	         MPI_File_iwrite_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_iwrite( fh, buf, count, datatype, request );

  MPE_Log_event( MPI_File_iwrite_stateid_0*2+1,
	         MPI_File_iwrite_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_seek( MPI_File  fh,MPI_Offset  offset,int  whence  )
{
  int returnVal;

/*
    MPI_File_seek - prototyping replacement for MPI_File_seek
    Log the beginning and ending of the time spent in MPI_File_seek calls.
*/

  ++MPI_File_seek_ncalls_0;
  MPE_Log_event( MPI_File_seek_stateid_0*2,
	         MPI_File_seek_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_seek( fh, offset, whence );

  MPE_Log_event( MPI_File_seek_stateid_0*2+1,
	         MPI_File_seek_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_position( MPI_File  fh,MPI_Offset * offset  )
{
  int returnVal;

/*
    MPI_File_get_position - prototyping replacement for MPI_File_get_position
    Log the beginning and ending of the time spent in MPI_File_get_position calls.
*/

  ++MPI_File_get_position_ncalls_0;
  MPE_Log_event( MPI_File_get_position_stateid_0*2,
	         MPI_File_get_position_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_position( fh, offset );

  MPE_Log_event( MPI_File_get_position_stateid_0*2+1,
	         MPI_File_get_position_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_byte_offset( MPI_File  fh,MPI_Offset  offset,MPI_Offset * disp  )
{
  int returnVal;

/*
    MPI_File_get_byte_offset - prototyping replacement for MPI_File_get_byte_offset
    Log the beginning and ending of the time spent in MPI_File_get_byte_offset calls.
*/

  ++MPI_File_get_byte_offset_ncalls_0;
  MPE_Log_event( MPI_File_get_byte_offset_stateid_0*2,
	         MPI_File_get_byte_offset_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_byte_offset( fh, offset, disp );

  MPE_Log_event( MPI_File_get_byte_offset_stateid_0*2+1,
	         MPI_File_get_byte_offset_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_shared( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_shared - prototyping replacement for MPI_File_read_shared
    Log the beginning and ending of the time spent in MPI_File_read_shared calls.
*/

  ++MPI_File_read_shared_ncalls_0;
  MPE_Log_event( MPI_File_read_shared_stateid_0*2,
	         MPI_File_read_shared_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_shared( fh, buf, count, datatype, status );

  MPE_Log_event( MPI_File_read_shared_stateid_0*2+1,
	         MPI_File_read_shared_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_shared( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_shared - prototyping replacement for MPI_File_write_shared
    Log the beginning and ending of the time spent in MPI_File_write_shared calls.
*/

  ++MPI_File_write_shared_ncalls_0;
  MPE_Log_event( MPI_File_write_shared_stateid_0*2,
	         MPI_File_write_shared_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_shared( fh, buf, count, datatype, status );

  MPE_Log_event( MPI_File_write_shared_stateid_0*2+1,
	         MPI_File_write_shared_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_iread_shared( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iread_shared - prototyping replacement for MPI_File_iread_shared
    Log the beginning and ending of the time spent in MPI_File_iread_shared calls.
*/

  ++MPI_File_iread_shared_ncalls_0;
  MPE_Log_event( MPI_File_iread_shared_stateid_0*2,
	         MPI_File_iread_shared_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_iread_shared( fh, buf, count, datatype, request );

  MPE_Log_event( MPI_File_iread_shared_stateid_0*2+1,
	         MPI_File_iread_shared_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_iwrite_shared( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iwrite_shared - prototyping replacement for MPI_File_iwrite_shared
    Log the beginning and ending of the time spent in MPI_File_iwrite_shared calls.
*/

  ++MPI_File_iwrite_shared_ncalls_0;
  MPE_Log_event( MPI_File_iwrite_shared_stateid_0*2,
	         MPI_File_iwrite_shared_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_iwrite_shared( fh, buf, count, datatype, request );

  MPE_Log_event( MPI_File_iwrite_shared_stateid_0*2+1,
	         MPI_File_iwrite_shared_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_ordered( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_ordered - prototyping replacement for MPI_File_read_ordered
    Log the beginning and ending of the time spent in MPI_File_read_ordered calls.
*/

  ++MPI_File_read_ordered_ncalls_0;
  MPE_Log_event( MPI_File_read_ordered_stateid_0*2,
	         MPI_File_read_ordered_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_ordered( fh, buf, count, datatype, status );

  MPE_Log_event( MPI_File_read_ordered_stateid_0*2+1,
	         MPI_File_read_ordered_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_ordered( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_ordered - prototyping replacement for MPI_File_write_ordered
    Log the beginning and ending of the time spent in MPI_File_write_ordered calls.
*/

  ++MPI_File_write_ordered_ncalls_0;
  MPE_Log_event( MPI_File_write_ordered_stateid_0*2,
	         MPI_File_write_ordered_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_ordered( fh, buf, count, datatype, status );

  MPE_Log_event( MPI_File_write_ordered_stateid_0*2+1,
	         MPI_File_write_ordered_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_seek_shared( MPI_File  fh,MPI_Offset  offset,int  whence  )
{
  int returnVal;

/*
    MPI_File_seek_shared - prototyping replacement for MPI_File_seek_shared
    Log the beginning and ending of the time spent in MPI_File_seek_shared calls.
*/

  ++MPI_File_seek_shared_ncalls_0;
  MPE_Log_event( MPI_File_seek_shared_stateid_0*2,
	         MPI_File_seek_shared_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_seek_shared( fh, offset, whence );

  MPE_Log_event( MPI_File_seek_shared_stateid_0*2+1,
	         MPI_File_seek_shared_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_position_shared( MPI_File  fh,MPI_Offset * offset  )
{
  int returnVal;

/*
    MPI_File_get_position_shared - prototyping replacement for MPI_File_get_position_shared
    Log the beginning and ending of the time spent in MPI_File_get_position_shared calls.
*/

  ++MPI_File_get_position_shared_ncalls_0;
  MPE_Log_event( MPI_File_get_position_shared_stateid_0*2,
	         MPI_File_get_position_shared_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_position_shared( fh, offset );

  MPE_Log_event( MPI_File_get_position_shared_stateid_0*2+1,
	         MPI_File_get_position_shared_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_at_all_begin( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_read_at_all_begin - prototyping replacement for MPI_File_read_at_all_begin
    Log the beginning and ending of the time spent in MPI_File_read_at_all_begin calls.
*/

  ++MPI_File_read_at_all_begin_ncalls_0;
  MPE_Log_event( MPI_File_read_at_all_begin_stateid_0*2,
	         MPI_File_read_at_all_begin_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_at_all_begin( fh, offset, buf, count, datatype );

  MPE_Log_event( MPI_File_read_at_all_begin_stateid_0*2+1,
	         MPI_File_read_at_all_begin_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_at_all_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_at_all_end - prototyping replacement for MPI_File_read_at_all_end
    Log the beginning and ending of the time spent in MPI_File_read_at_all_end calls.
*/

  ++MPI_File_read_at_all_end_ncalls_0;
  MPE_Log_event( MPI_File_read_at_all_end_stateid_0*2,
	         MPI_File_read_at_all_end_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_at_all_end( fh, buf, status );

  MPE_Log_event( MPI_File_read_at_all_end_stateid_0*2+1,
	         MPI_File_read_at_all_end_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_at_all_begin( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_write_at_all_begin - prototyping replacement for MPI_File_write_at_all_begin
    Log the beginning and ending of the time spent in MPI_File_write_at_all_begin calls.
*/

  ++MPI_File_write_at_all_begin_ncalls_0;
  MPE_Log_event( MPI_File_write_at_all_begin_stateid_0*2,
	         MPI_File_write_at_all_begin_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_at_all_begin( fh, offset, buf, count, datatype );

  MPE_Log_event( MPI_File_write_at_all_begin_stateid_0*2+1,
	         MPI_File_write_at_all_begin_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_at_all_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_at_all_end - prototyping replacement for MPI_File_write_at_all_end
    Log the beginning and ending of the time spent in MPI_File_write_at_all_end calls.
*/

  ++MPI_File_write_at_all_end_ncalls_0;
  MPE_Log_event( MPI_File_write_at_all_end_stateid_0*2,
	         MPI_File_write_at_all_end_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_at_all_end( fh, buf, status );

  MPE_Log_event( MPI_File_write_at_all_end_stateid_0*2+1,
	         MPI_File_write_at_all_end_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_all_begin( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_read_all_begin - prototyping replacement for MPI_File_read_all_begin
    Log the beginning and ending of the time spent in MPI_File_read_all_begin calls.
*/

  ++MPI_File_read_all_begin_ncalls_0;
  MPE_Log_event( MPI_File_read_all_begin_stateid_0*2,
	         MPI_File_read_all_begin_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_all_begin( fh, buf, count, datatype );

  MPE_Log_event( MPI_File_read_all_begin_stateid_0*2+1,
	         MPI_File_read_all_begin_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_all_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_all_end - prototyping replacement for MPI_File_read_all_end
    Log the beginning and ending of the time spent in MPI_File_read_all_end calls.
*/

  ++MPI_File_read_all_end_ncalls_0;
  MPE_Log_event( MPI_File_read_all_end_stateid_0*2,
	         MPI_File_read_all_end_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_all_end( fh, buf, status );

  MPE_Log_event( MPI_File_read_all_end_stateid_0*2+1,
	         MPI_File_read_all_end_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_all_begin( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_write_all_begin - prototyping replacement for MPI_File_write_all_begin
    Log the beginning and ending of the time spent in MPI_File_write_all_begin calls.
*/

  ++MPI_File_write_all_begin_ncalls_0;
  MPE_Log_event( MPI_File_write_all_begin_stateid_0*2,
	         MPI_File_write_all_begin_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_all_begin( fh, buf, count, datatype );

  MPE_Log_event( MPI_File_write_all_begin_stateid_0*2+1,
	         MPI_File_write_all_begin_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_all_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_all_end - prototyping replacement for MPI_File_write_all_end
    Log the beginning and ending of the time spent in MPI_File_write_all_end calls.
*/

  ++MPI_File_write_all_end_ncalls_0;
  MPE_Log_event( MPI_File_write_all_end_stateid_0*2,
	         MPI_File_write_all_end_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_all_end( fh, buf, status );

  MPE_Log_event( MPI_File_write_all_end_stateid_0*2+1,
	         MPI_File_write_all_end_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_ordered_begin( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_read_ordered_begin - prototyping replacement for MPI_File_read_ordered_begin
    Log the beginning and ending of the time spent in MPI_File_read_ordered_begin calls.
*/

  ++MPI_File_read_ordered_begin_ncalls_0;
  MPE_Log_event( MPI_File_read_ordered_begin_stateid_0*2,
	         MPI_File_read_ordered_begin_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_ordered_begin( fh, buf, count, datatype );

  MPE_Log_event( MPI_File_read_ordered_begin_stateid_0*2+1,
	         MPI_File_read_ordered_begin_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_read_ordered_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_ordered_end - prototyping replacement for MPI_File_read_ordered_end
    Log the beginning and ending of the time spent in MPI_File_read_ordered_end calls.
*/

  ++MPI_File_read_ordered_end_ncalls_0;
  MPE_Log_event( MPI_File_read_ordered_end_stateid_0*2,
	         MPI_File_read_ordered_end_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_read_ordered_end( fh, buf, status );

  MPE_Log_event( MPI_File_read_ordered_end_stateid_0*2+1,
	         MPI_File_read_ordered_end_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_ordered_begin( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_write_ordered_begin - prototyping replacement for MPI_File_write_ordered_begin
    Log the beginning and ending of the time spent in MPI_File_write_ordered_begin calls.
*/

  ++MPI_File_write_ordered_begin_ncalls_0;
  MPE_Log_event( MPI_File_write_ordered_begin_stateid_0*2,
	         MPI_File_write_ordered_begin_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_ordered_begin( fh, buf, count, datatype );

  MPE_Log_event( MPI_File_write_ordered_begin_stateid_0*2+1,
	         MPI_File_write_ordered_begin_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_write_ordered_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_ordered_end - prototyping replacement for MPI_File_write_ordered_end
    Log the beginning and ending of the time spent in MPI_File_write_ordered_end calls.
*/

  ++MPI_File_write_ordered_end_ncalls_0;
  MPE_Log_event( MPI_File_write_ordered_end_stateid_0*2,
	         MPI_File_write_ordered_end_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_write_ordered_end( fh, buf, status );

  MPE_Log_event( MPI_File_write_ordered_end_stateid_0*2+1,
	         MPI_File_write_ordered_end_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_type_extent( MPI_File  fh,MPI_Datatype  datatype,MPI_Aint * extent  )
{
  int returnVal;

/*
    MPI_File_get_type_extent - prototyping replacement for MPI_File_get_type_extent
    Log the beginning and ending of the time spent in MPI_File_get_type_extent calls.
*/

  ++MPI_File_get_type_extent_ncalls_0;
  MPE_Log_event( MPI_File_get_type_extent_stateid_0*2,
	         MPI_File_get_type_extent_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_type_extent( fh, datatype, extent );

  MPE_Log_event( MPI_File_get_type_extent_stateid_0*2+1,
	         MPI_File_get_type_extent_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_set_atomicity( MPI_File  fh,int  flag  )
{
  int returnVal;

/*
    MPI_File_set_atomicity - prototyping replacement for MPI_File_set_atomicity
    Log the beginning and ending of the time spent in MPI_File_set_atomicity calls.
*/

  ++MPI_File_set_atomicity_ncalls_0;
  MPE_Log_event( MPI_File_set_atomicity_stateid_0*2,
	         MPI_File_set_atomicity_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_set_atomicity( fh, flag );

  MPE_Log_event( MPI_File_set_atomicity_stateid_0*2+1,
	         MPI_File_set_atomicity_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_get_atomicity( MPI_File  fh,int * flag  )
{
  int returnVal;

/*
    MPI_File_get_atomicity - prototyping replacement for MPI_File_get_atomicity
    Log the beginning and ending of the time spent in MPI_File_get_atomicity calls.
*/

  ++MPI_File_get_atomicity_ncalls_0;
  MPE_Log_event( MPI_File_get_atomicity_stateid_0*2,
	         MPI_File_get_atomicity_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_get_atomicity( fh, flag );

  MPE_Log_event( MPI_File_get_atomicity_stateid_0*2+1,
	         MPI_File_get_atomicity_ncalls_0, (char *)0 );


  return returnVal;
}

int MPI_File_sync( MPI_File  fh  )
{
  int returnVal;

/*
    MPI_File_sync - prototyping replacement for MPI_File_sync
    Log the beginning and ending of the time spent in MPI_File_sync calls.
*/

  ++MPI_File_sync_ncalls_0;
  MPE_Log_event( MPI_File_sync_stateid_0*2,
	         MPI_File_sync_ncalls_0, (char *)0 );
  
  returnVal = PMPI_File_sync( fh );

  MPE_Log_event( MPI_File_sync_stateid_0*2+1,
	         MPI_File_sync_ncalls_0, (char *)0 );


  return returnVal;
}
