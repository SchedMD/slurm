/*
    This file should be INCLUDED into log_wrap.c when adding the IO routines
    to the profiling list

    Also set MPE_MAX_STATES to 180
 */

#define MPE_FILE_OPEN_ID 128
#define MPE_FILE_CLOSE_ID 129
#define MPE_FILE_DELETE_ID 130
#define MPE_FILE_SET_SIZE_ID 131
#define MPE_FILE_PREALLOCATE_ID 132
#define MPE_FILE_GET_SIZE_ID 133
#define MPE_FILE_GET_GROUP_ID 134
#define MPE_FILE_GET_AMODE_ID 135
#define MPE_FILE_SET_INFO_ID 136
#define MPE_FILE_GET_INFO_ID 137
#define MPE_FILE_SET_VIEW_ID 138
#define MPE_FILE_GET_VIEW_ID 139
#define MPE_FILE_READ_AT_ID 140
#define MPE_FILE_READ_AT_ALL_ID 141
#define MPE_FILE_WRITE_AT_ID 142
#define MPE_FILE_WRITE_AT_ALL_ID 143
#define MPE_FILE_IREAD_AT_ID 144
#define MPE_FILE_IWRITE_AT_ID 145
#define MPE_FILE_READ_ID 146
#define MPE_FILE_READ_ALL_ID 147
#define MPE_FILE_WRITE_ID 148
#define MPE_FILE_WRITE_ALL_ID 149
#define MPE_FILE_IREAD_ID 150
#define MPE_FILE_IWRITE_ID 151
#define MPE_FILE_SEEK_ID 152
#define MPE_FILE_GET_POSITION_ID 153
#define MPE_FILE_GET_BYTE_OFFSET_ID 154
#define MPE_FILE_READ_SHARED_ID 155
#define MPE_FILE_WRITE_SHARED_ID 156
#define MPE_FILE_IREAD_SHARED_ID 157
#define MPE_FILE_IWRITE_SHARED_ID 158
#define MPE_FILE_READ_ORDERED_ID 159
#define MPE_FILE_WRITE_ORDERED_ID 160
#define MPE_FILE_SEEK_SHARED_ID 161
#define MPE_FILE_GET_POSITION_SHARED_ID 162
#define MPE_FILE_READ_AT_ALL_BEGIN_ID 163
#define MPE_FILE_READ_AT_ALL_END_ID 164
#define MPE_FILE_WRITE_AT_ALL_BEGIN_ID 165
#define MPE_FILE_WRITE_AT_ALL_END_ID 166
#define MPE_FILE_READ_ALL_BEGIN_ID 167
#define MPE_FILE_READ_ALL_END_ID 168
#define MPE_FILE_WRITE_ALL_BEGIN_ID 169
#define MPE_FILE_WRITE_ALL_END_ID 170
#define MPE_FILE_READ_ORDERED_BEGIN_ID 171
#define MPE_FILE_READ_ORDERED_END_ID 172
#define MPE_FILE_WRITE_ORDERED_BEGIN_ID 173
#define MPE_FILE_WRITE_ORDERED_END_ID 174
#define MPE_FILE_GET_TYPE_EXTENT_ID 175
#define MPE_REGISTER_DATAREP_ID 176
#define MPE_FILE_SET_ATOMICITY_ID 177
#define MPE_FILE_GET_ATOMICITY_ID 178
#define MPE_FILE_SYNC_ID 179

#if defined( HAVE_NO_MPIO_REQUEST )
#define  MPIO_Request  MPI_Request
#endif


void MPE_Init_MPIIO( void )
{
  MPE_State *state;
  
  state = &states[MPE_FILE_OPEN_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_OPEN";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_CLOSE_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_CLOSE";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_DELETE_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_DELETE";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_SET_SIZE_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_SET_SIZE";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_PREALLOCATE_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_PREALLOCATE";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_SIZE_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_SIZE";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_GROUP_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_GROUP";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_AMODE_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_AMODE";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_SET_INFO_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_SET_INFO";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_INFO_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_INFO";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_SET_VIEW_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_SET_VIEW";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_VIEW_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_VIEW";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_AT_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_AT";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_AT_ALL_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_AT_ALL";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_AT_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_AT";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_AT_ALL_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_AT_ALL";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_IREAD_AT_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_IREAD_AT";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_IWRITE_AT_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_IWRITE_AT";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_ALL_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_ALL";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_ALL_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_ALL";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_IREAD_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_IREAD";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_IWRITE_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_IWRITE";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_SEEK_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_SEEK";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_POSITION_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_POSITION";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_BYTE_OFFSET_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_BYTE_OFFSET";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_SHARED_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_SHARED";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_SHARED_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_SHARED";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_IREAD_SHARED_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_IREAD_SHARED";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_IWRITE_SHARED_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_IWRITE_SHARED";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_ORDERED_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_ORDERED";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_ORDERED_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_ORDERED";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_SEEK_SHARED_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_SEEK_SHARED";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_POSITION_SHARED_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_POSITION_SHARED";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_AT_ALL_BEGIN_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_AT_ALL_BEGIN";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_AT_ALL_END_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_AT_ALL_END";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_AT_ALL_BEGIN_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_AT_ALL_BEGIN";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_AT_ALL_END_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_AT_ALL_END";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_ALL_BEGIN_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_ALL_BEGIN";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_ALL_END_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_ALL_END";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_ALL_BEGIN_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_ALL_BEGIN";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_ALL_END_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_ALL_END";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_ORDERED_BEGIN_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_ORDERED_BEGIN";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_READ_ORDERED_END_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_READ_ORDERED_END";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_ORDERED_BEGIN_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_ORDERED_BEGIN";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_WRITE_ORDERED_END_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_WRITE_ORDERED_END";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_TYPE_EXTENT_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_TYPE_EXTENT";
  state->color = "brown:gray2";
  
  state = &states[MPE_REGISTER_DATAREP_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "REGISTER_DATAREP";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_SET_ATOMICITY_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_SET_ATOMICITY";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_GET_ATOMICITY_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_GET_ATOMICITY";
  state->color = "brown:gray2";
  
  state = &states[MPE_FILE_SYNC_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "FILE_SYNC";
  state->color = "brown:gray2";
  
}


int MPI_File_open( MPI_Comm  comm,char * filename,int  amode,MPI_Info  info,MPI_File * fh  )
{
  int returnVal;

/*
    MPI_File_open - prototyping replacement for MPI_File_open
    Log the beginning and ending of the time spent in MPI_File_open calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_OPEN_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_open( comm, filename, amode, info, fh );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_close( MPI_File * fh  )
{
  int returnVal;

/*
    MPI_File_close - prototyping replacement for MPI_File_close
    Log the beginning and ending of the time spent in MPI_File_close calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_CLOSE_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_close( fh );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_delete( char * filename,MPI_Info  info  )
{
  int returnVal;

/*
    MPI_File_delete - prototyping replacement for MPI_File_delete
    Log the beginning and ending of the time spent in MPI_File_delete calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_DELETE_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_delete( filename, info );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_set_size( MPI_File  fh,MPI_Offset  size  )
{
  int returnVal;

/*
    MPI_File_set_size - prototyping replacement for MPI_File_set_size
    Log the beginning and ending of the time spent in MPI_File_set_size calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_SET_SIZE_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_set_size( fh, size );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_preallocate( MPI_File  fh,MPI_Offset  size  )
{
  int returnVal;

/*
    MPI_File_preallocate - prototyping replacement for MPI_File_preallocate
    Log the beginning and ending of the time spent in MPI_File_preallocate calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_PREALLOCATE_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_preallocate( fh, size );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_size( MPI_File  fh,MPI_Offset * size  )
{
  int returnVal;

/*
    MPI_File_get_size - prototyping replacement for MPI_File_get_size
    Log the beginning and ending of the time spent in MPI_File_get_size calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_SIZE_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_size( fh, size );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_group( MPI_File  fh,MPI_Group * group  )
{
  int returnVal;

/*
    MPI_File_get_group - prototyping replacement for MPI_File_get_group
    Log the beginning and ending of the time spent in MPI_File_get_group calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_GROUP_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_group( fh, group );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_amode( MPI_File  fh,int * amode  )
{
  int returnVal;

/*
    MPI_File_get_amode - prototyping replacement for MPI_File_get_amode
    Log the beginning and ending of the time spent in MPI_File_get_amode calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_AMODE_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_amode( fh, amode );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_set_info( MPI_File  fh,MPI_Info  info  )
{
  int returnVal;

/*
    MPI_File_set_info - prototyping replacement for MPI_File_set_info
    Log the beginning and ending of the time spent in MPI_File_set_info calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_SET_INFO_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_set_info( fh, info );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_info( MPI_File  fh,MPI_Info * info_used  )
{
  int returnVal;

/*
    MPI_File_get_info - prototyping replacement for MPI_File_get_info
    Log the beginning and ending of the time spent in MPI_File_get_info calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_INFO_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_info( fh, info_used );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_set_view( MPI_File  fh,MPI_Offset  disp,MPI_Datatype  etype,MPI_Datatype  filetype,char * datarep,MPI_Info  info  )
{
  int returnVal;

/*
    MPI_File_set_view - prototyping replacement for MPI_File_set_view
    Log the beginning and ending of the time spent in MPI_File_set_view calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_SET_VIEW_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_set_view( fh, disp, etype, filetype, datarep, info );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_view( MPI_File  fh,MPI_Offset * disp,MPI_Datatype * etype,MPI_Datatype * filetype,char * datarep  )
{
  int returnVal;

/*
    MPI_File_get_view - prototyping replacement for MPI_File_get_view
    Log the beginning and ending of the time spent in MPI_File_get_view calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_VIEW_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_view( fh, disp, etype, filetype, datarep );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_at( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_at - prototyping replacement for MPI_File_read_at
    Log the beginning and ending of the time spent in MPI_File_read_at calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_AT_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_at( fh, offset, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_at_all( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_at_all - prototyping replacement for MPI_File_read_at_all
    Log the beginning and ending of the time spent in MPI_File_read_at_all calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_AT_ALL_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_at_all( fh, offset, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_at( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_at - prototyping replacement for MPI_File_write_at
    Log the beginning and ending of the time spent in MPI_File_write_at calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_AT_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_at( fh, offset, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_at_all( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_at_all - prototyping replacement for MPI_File_write_at_all
    Log the beginning and ending of the time spent in MPI_File_write_at_all calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_AT_ALL_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_at_all( fh, offset, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_iread_at( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iread_at - prototyping replacement for MPI_File_iread_at
    Log the beginning and ending of the time spent in MPI_File_iread_at calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_IREAD_AT_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_iread_at( fh, offset, buf, count, datatype, request );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_iwrite_at( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iwrite_at - prototyping replacement for MPI_File_iwrite_at
    Log the beginning and ending of the time spent in MPI_File_iwrite_at calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_IWRITE_AT_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_iwrite_at( fh, offset, buf, count, datatype, request );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read - prototyping replacement for MPI_File_read
    Log the beginning and ending of the time spent in MPI_File_read calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read( fh, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_all( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_all - prototyping replacement for MPI_File_read_all
    Log the beginning and ending of the time spent in MPI_File_read_all calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_ALL_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_all( fh, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write - prototyping replacement for MPI_File_write
    Log the beginning and ending of the time spent in MPI_File_write calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write( fh, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_all( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_all - prototyping replacement for MPI_File_write_all
    Log the beginning and ending of the time spent in MPI_File_write_all calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_ALL_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_all( fh, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_iread( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iread - prototyping replacement for MPI_File_iread
    Log the beginning and ending of the time spent in MPI_File_iread calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_IREAD_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_iread( fh, buf, count, datatype, request );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_iwrite( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iwrite - prototyping replacement for MPI_File_iwrite
    Log the beginning and ending of the time spent in MPI_File_iwrite calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_IWRITE_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_iwrite( fh, buf, count, datatype, request );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_seek( MPI_File  fh,MPI_Offset  offset,int  whence  )
{
  int returnVal;

/*
    MPI_File_seek - prototyping replacement for MPI_File_seek
    Log the beginning and ending of the time spent in MPI_File_seek calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_SEEK_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_seek( fh, offset, whence );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_position( MPI_File  fh,MPI_Offset * offset  )
{
  int returnVal;

/*
    MPI_File_get_position - prototyping replacement for MPI_File_get_position
    Log the beginning and ending of the time spent in MPI_File_get_position calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_POSITION_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_position( fh, offset );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_byte_offset( MPI_File  fh,MPI_Offset  offset,MPI_Offset * disp  )
{
  int returnVal;

/*
    MPI_File_get_byte_offset - prototyping replacement for MPI_File_get_byte_offset
    Log the beginning and ending of the time spent in MPI_File_get_byte_offset calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_BYTE_OFFSET_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_byte_offset( fh, offset, disp );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_shared( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_shared - prototyping replacement for MPI_File_read_shared
    Log the beginning and ending of the time spent in MPI_File_read_shared calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_SHARED_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_shared( fh, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_shared( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_shared - prototyping replacement for MPI_File_write_shared
    Log the beginning and ending of the time spent in MPI_File_write_shared calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_SHARED_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_shared( fh, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_iread_shared( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iread_shared - prototyping replacement for MPI_File_iread_shared
    Log the beginning and ending of the time spent in MPI_File_iread_shared calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_IREAD_SHARED_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_iread_shared( fh, buf, count, datatype, request );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_iwrite_shared( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPIO_Request * request  )
{
  int returnVal;

/*
    MPI_File_iwrite_shared - prototyping replacement for MPI_File_iwrite_shared
    Log the beginning and ending of the time spent in MPI_File_iwrite_shared calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_IWRITE_SHARED_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_iwrite_shared( fh, buf, count, datatype, request );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_ordered( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_ordered - prototyping replacement for MPI_File_read_ordered
    Log the beginning and ending of the time spent in MPI_File_read_ordered calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_ORDERED_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_ordered( fh, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_ordered( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_ordered - prototyping replacement for MPI_File_write_ordered
    Log the beginning and ending of the time spent in MPI_File_write_ordered calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_ORDERED_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_ordered( fh, buf, count, datatype, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_seek_shared( MPI_File  fh,MPI_Offset  offset,int  whence  )
{
  int returnVal;

/*
    MPI_File_seek_shared - prototyping replacement for MPI_File_seek_shared
    Log the beginning and ending of the time spent in MPI_File_seek_shared calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_SEEK_SHARED_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_seek_shared( fh, offset, whence );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_position_shared( MPI_File  fh,MPI_Offset * offset  )
{
  int returnVal;

/*
    MPI_File_get_position_shared - prototyping replacement for MPI_File_get_position_shared
    Log the beginning and ending of the time spent in MPI_File_get_position_shared calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_POSITION_SHARED_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_position_shared( fh, offset );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_at_all_begin( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_read_at_all_begin - prototyping replacement for MPI_File_read_at_all_begin
    Log the beginning and ending of the time spent in MPI_File_read_at_all_begin calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_AT_ALL_BEGIN_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_at_all_begin( fh, offset, buf, count, datatype );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_at_all_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_at_all_end - prototyping replacement for MPI_File_read_at_all_end
    Log the beginning and ending of the time spent in MPI_File_read_at_all_end calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_AT_ALL_END_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_at_all_end( fh, buf, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_at_all_begin( MPI_File  fh,MPI_Offset  offset,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_write_at_all_begin - prototyping replacement for MPI_File_write_at_all_begin
    Log the beginning and ending of the time spent in MPI_File_write_at_all_begin calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_AT_ALL_BEGIN_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_at_all_begin( fh, offset, buf, count, datatype );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_at_all_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_at_all_end - prototyping replacement for MPI_File_write_at_all_end
    Log the beginning and ending of the time spent in MPI_File_write_at_all_end calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_AT_ALL_END_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_at_all_end( fh, buf, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_all_begin( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_read_all_begin - prototyping replacement for MPI_File_read_all_begin
    Log the beginning and ending of the time spent in MPI_File_read_all_begin calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_ALL_BEGIN_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_all_begin( fh, buf, count, datatype );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_all_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_all_end - prototyping replacement for MPI_File_read_all_end
    Log the beginning and ending of the time spent in MPI_File_read_all_end calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_ALL_END_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_all_end( fh, buf, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_all_begin( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_write_all_begin - prototyping replacement for MPI_File_write_all_begin
    Log the beginning and ending of the time spent in MPI_File_write_all_begin calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_ALL_BEGIN_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_all_begin( fh, buf, count, datatype );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_all_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_all_end - prototyping replacement for MPI_File_write_all_end
    Log the beginning and ending of the time spent in MPI_File_write_all_end calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_ALL_END_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_all_end( fh, buf, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_ordered_begin( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_read_ordered_begin - prototyping replacement for MPI_File_read_ordered_begin
    Log the beginning and ending of the time spent in MPI_File_read_ordered_begin calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_ORDERED_BEGIN_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_ordered_begin( fh, buf, count, datatype );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_read_ordered_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_read_ordered_end - prototyping replacement for MPI_File_read_ordered_end
    Log the beginning and ending of the time spent in MPI_File_read_ordered_end calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_READ_ORDERED_END_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_read_ordered_end( fh, buf, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_ordered_begin( MPI_File  fh,void * buf,int  count,MPI_Datatype  datatype  )
{
  int returnVal;

/*
    MPI_File_write_ordered_begin - prototyping replacement for MPI_File_write_ordered_begin
    Log the beginning and ending of the time spent in MPI_File_write_ordered_begin calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_ORDERED_BEGIN_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_ordered_begin( fh, buf, count, datatype );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_write_ordered_end( MPI_File  fh,void * buf,MPI_Status * status  )
{
  int returnVal;

/*
    MPI_File_write_ordered_end - prototyping replacement for MPI_File_write_ordered_end
    Log the beginning and ending of the time spent in MPI_File_write_ordered_end calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_WRITE_ORDERED_END_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_write_ordered_end( fh, buf, status );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_type_extent( MPI_File  fh,MPI_Datatype  datatype,MPI_Aint * extent  )
{
  int returnVal;

/*
    MPI_File_get_type_extent - prototyping replacement for MPI_File_get_type_extent
    Log the beginning and ending of the time spent in MPI_File_get_type_extent calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_TYPE_EXTENT_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_type_extent( fh, datatype, extent );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_set_atomicity( MPI_File  fh,int  flag  )
{
  int returnVal;

/*
    MPI_File_set_atomicity - prototyping replacement for MPI_File_set_atomicity
    Log the beginning and ending of the time spent in MPI_File_set_atomicity calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_SET_ATOMICITY_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_set_atomicity( fh, flag );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_get_atomicity( MPI_File  fh,int * flag  )
{
  int returnVal;

/*
    MPI_File_get_atomicity - prototyping replacement for MPI_File_get_atomicity
    Log the beginning and ending of the time spent in MPI_File_get_atomicity calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_GET_ATOMICITY_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_get_atomicity( fh, flag );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}

int MPI_File_sync( MPI_File  fh  )
{
  int returnVal;

/*
    MPI_File_sync - prototyping replacement for MPI_File_sync
    Log the beginning and ending of the time spent in MPI_File_sync calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( MPE_FILE_SYNC_ID,MPI_COMM_NULL);
  
  returnVal = PMPI_File_sync( fh );

  MPE_LOG_STATE_END( MPI_COMM_NULL );


  return returnVal;
}
