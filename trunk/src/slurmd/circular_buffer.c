#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <src/common/log.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_errno.h>

#include <src/slurmd/circular_buffer.h>

#define DEF_INITIAL_BUFFER_SIZE 8192
#define DEF_INCREMENTAL_BUFFER_SIZE 8192
#define DEF_MAX_BUFFER_SIZE ( ( 8192 * 10 ) )
#define BUFFER_FULL_DUMP_SIZE ( buf -> min_size / 2 )

static int assert_checks ( circular_buffer_t * buf ) ;
static int assert_checks_2 ( circular_buffer_t * buf ) ;
static int expand_buffer ( circular_buffer_t * buf ) ;
static int shrink_buffer ( circular_buffer_t * buf ) ;
static void common_init ( circular_buffer_t * buf ) ;

void free_circular_buffer ( circular_buffer_t * buf_ptr )
{
	if ( buf_ptr )
	{
		if ( buf_ptr -> buffer )
		{
			xfree (  buf_ptr -> buffer ) ;
		}
		xfree ( buf_ptr ) ;
	}
}

int init_circular_buffer ( circular_buffer_t ** buf_ptr )
{
	circular_buffer_t * buf ;
	*buf_ptr = xmalloc ( sizeof ( circular_buffer_t ) ) ;
	buf = *buf_ptr ;

	buf -> min_size = DEF_INITIAL_BUFFER_SIZE ;
	buf -> max_size = DEF_MAX_BUFFER_SIZE ;
	buf -> incremental_size = DEF_INCREMENTAL_BUFFER_SIZE ;

	common_init ( buf ) ;
	return SLURM_SUCCESS ;

}

int init_circular_buffer2 ( circular_buffer_t ** buf_ptr , int min_size , int max_size , int incremental_size )
{
	circular_buffer_t * buf ;
	*buf_ptr = xmalloc ( sizeof ( circular_buffer_t ) ) ;
	buf = *buf_ptr ;
	
	buf -> min_size = min_size ;	
	buf -> max_size = max_size ;	
	buf -> incremental_size = incremental_size ;	

	common_init ( buf ) ;
	return SLURM_SUCCESS ;
}

static void common_init ( circular_buffer_t * buf )
{
	buf -> buffer = xmalloc ( buf -> min_size ) ;
	buf -> buf_size = buf -> min_size ;

	buf -> start = buf -> buffer ;
	buf -> end = buf -> start + buf-> buf_size ;

	buf -> head = buf -> start ;
	buf -> tail = buf -> start ;

	buf -> read_size = 0 ;
	buf -> write_size = buf -> min_size ;
	
	return ;
}

void print_circular_buffer ( circular_buffer_t * buf )
{
	info ( "--" ) ;
	info ( "buffer  %X", buf -> buffer ) ;
	info ( "start   %X", buf -> start ) ;
	info ( "end     %X", buf -> end ) ;
	info ( "head    %X", buf -> head ) ;
	info ( "tail    %X", buf -> tail ) ;
	info ( "rhead    %i", buf -> head - buf -> start ) ;
	info ( "rtail    %i", buf -> tail - buf -> start ) ;
	info ( "size    %i", buf -> buf_size ) ;
	info ( "read s  %i", buf -> read_size  ) ;
	info ( "write s %i", buf -> write_size ) ;
}

int cir_buf_read_update ( circular_buffer_t * buf , unsigned int size )
{
	/*if zero read, just return */
	if ( size == 0 )
	{
		debug( "zero length read in cirular buffer" ) ;
		return SLURM_SUCCESS ;
	}

	/* before modifing the buffer lets do some sanity checks*/
	assert ( size <= buf-> read_size ) ;
	assert_checks ( buf ) ;
	assert_checks_2 ( buf ) ;

	/*modify headning position of the buffer*/
	buf->head = buf->head + size ;

	/* after modifing the buffer lets do some sanity checks*/
	assert_checks ( buf ) ;

	/* take care of wrap around issues */
	if ( buf->tail > buf->head ) /* CASE tail after head */
	{
		buf -> read_size = buf -> tail - buf -> head ;
		buf -> write_size = buf -> end - buf -> tail ;
	}
	else if ( buf->tail < buf->head ) /* CASE tail befpre head */
	{
		if ( buf->head  == buf-> end ) /* CASE tail == end */
		{
			if ( buf -> tail == buf -> start ) /* CASE head == start */
			{
				/* buffer empty*/
				shrink_buffer ( buf ) ;
			}
			else
			{
				buf -> head = buf -> start ;
				buf -> read_size = buf -> tail - buf -> head ;
				buf -> write_size = buf -> end - buf -> tail ;
			}
		}
		else
		{
			buf -> read_size = buf -> end - buf -> head ;
			buf -> write_size = buf -> head - buf -> tail ;
		}
	}
	else if ( buf->tail == buf->head ) /* CASE head == tail */
	{
		/* buffer empty*/
		shrink_buffer ( buf ) ;
	}

	/* final sanity check */
	assert_checks ( buf ) ;
	assert_checks_2 ( buf ) ;
	return SLURM_SUCCESS ;
}

int cir_buf_write_update ( circular_buffer_t * buf , unsigned int size )
{
	/*if zero read, just return */
	if ( size == 0 )
	{
		info ( "zero length write in cirular buffer" ) ;
		return SLURM_SUCCESS ;
	}

	/* before modifing the buffer lets do some sanity checks*/
	assert ( size <= buf-> write_size ) ;
	assert_checks ( buf ) ;
	assert_checks_2 ( buf ) ;

	/*modify headning position of the buffer*/
	buf->tail = buf->tail + size ;

	/* after modifing the buffer lets do some sanity checks*/
	assert_checks ( buf ) ;

	/* take care of wrap around issues */
	if ( buf->tail > buf->head ) /* CASE tail after head */
	{
		if ( buf -> tail == buf -> end  ) /* CASE tail == end */
		{
			if ( buf->head == buf-> start ) /* CASE head == start */
			{
				/* buffer full*/
				buf -> write_size -= size ;
				buf -> read_size += size ;
				expand_buffer ( buf ) ;
			}
			else
			{
				buf -> tail = buf -> start ;
				buf -> write_size = buf -> head - buf -> tail ;
				buf -> read_size = buf -> end - buf -> head ;
			}
		}
		else
		{
			buf -> write_size = buf -> end - buf -> tail ;
			buf -> read_size = buf -> tail - buf -> head ;
		}
	}
	else if ( buf->tail < buf->head ) /* CASE tail before head */
	{
		buf -> write_size = buf -> head - buf -> tail ;
		buf -> read_size = buf -> end - buf -> head ;
	}
	else if ( buf->tail == buf->head ) /* CASE head == tail */
	{
		/* buffer full*/
		buf -> write_size -= size ;
		buf -> read_size += size ;
		expand_buffer ( buf ) ;
	}

	/* final sanity check */
	assert_checks ( buf ) ;
	assert_checks_2 ( buf ) ;
	return SLURM_SUCCESS ;
}

static int assert_checks_2 ( circular_buffer_t * buf )
{
	/* sanity checks */

	/* head pointer is between start and end */	
	assert ( buf-> head >= buf -> start ) ; 
	assert ( buf-> head < buf -> end ) ;

	/* tail pointer is between start and end */	
	assert ( buf-> tail >= buf -> start ) ; 
	assert ( buf-> tail < buf -> end ) ;

	if ( buf->tail > buf->head )
	{
		assert ( buf -> write_size == buf -> end - buf -> tail ) ;
		assert ( buf -> read_size == buf -> tail - buf -> head ) ;
	}
	else if ( buf->tail < buf->head )
	{
		assert ( buf -> write_size == buf -> head - buf -> tail ) ;
		assert ( buf -> read_size == buf -> end - buf -> head ) ;
	}
	else if ( buf->tail == buf->head )
	{
		assert ( buf -> write_size == buf -> buf_size ) ;
		assert ( buf -> read_size == 0 ) ;
	}

	return SLURM_SUCCESS ;
}

static int assert_checks ( circular_buffer_t * buf )
{
	/* sanity checks */
	/* insures that dump data when MAX_BUFFER_SIZE is full will work correctly */
	
	assert ( buf != NULL ) ; /* buf struct is not null */
	assert ( buf-> start == buf -> buffer ); /* stat hasn't moved */
	assert ( ( buf -> start ) < ( buf -> end ) ); /* buf_end is after start */
	assert ( buf-> end - buf -> start == buf -> buf_size ) ; /* buffer start and end haven't moved */

	/* head pointer is between start and end */	
	assert ( buf-> head >= buf -> start ) ; 
	assert ( buf-> head <= buf -> end ) ;

	/* tail pointer is between start and end */	
	assert ( buf-> tail >= buf -> start ) ; 
	assert ( buf-> tail <= buf -> end ) ;

	return SLURM_SUCCESS ;
}

static int shrink_buffer ( circular_buffer_t * buf )
{
	char * new_buffer ;

	if ( buf->buf_size == buf -> min_size )
	{
	/*	info ( "circular buffer at minimum" ) ; */

		buf -> head = buf -> start ;
		buf -> tail = buf -> start ;

		buf -> read_size = 0 ;
		buf -> write_size = buf -> min_size ;
		
		return SLURM_SUCCESS ;
	}
	else
	{
		new_buffer = xmalloc ( buf -> min_size ) ;
		xfree ( buf -> buffer ) ;
		buf -> buffer = new_buffer ;
		buf -> buf_size = buf -> min_size ;
		
		buf -> start = new_buffer ;
		buf -> end = new_buffer + buf -> min_size ; 
		
		buf -> head = new_buffer ;
		buf -> tail = new_buffer ;

		buf -> read_size = 0 ;
		buf -> write_size = buf -> min_size ;
		
		return SLURM_SUCCESS ;
	}
}

static int expand_buffer ( circular_buffer_t * buf )
{
	char * new_buffer ;
	int data_size ;
	int data_size_blk1 ;
	int data_size_blk2 ;
	
/*	info ( "EXPANDING BUFFER" ) ; */
	/*print_circular_buffer ( buf ) ; */

	/* buffer has reached its maximum size going to dump some data out the bit bucket */
	if ( buf->buf_size == buf -> max_size )
	{
		/*print_circular_buffer ( buf ) ; */
		/*info ( "circular buffer maxed, dumping BUFFER_FULL_DUMP_SIZE of data"); */
		if ( buf->tail - buf->start >= BUFFER_FULL_DUMP_SIZE )
		{
			buf-> tail = buf->tail - BUFFER_FULL_DUMP_SIZE ;
			buf -> write_size = BUFFER_FULL_DUMP_SIZE ;
			if ( buf->tail > buf->head ) /* CASE tail after head */
			{
				buf -> read_size -= BUFFER_FULL_DUMP_SIZE ;
			}
			else /*if ( buf->tail < buf->head ) */ /* CASE tail befpre head */
			{
				/* read_size stays the same */
			}
		}
		else
		{
			int datasize_blk1 =  buf -> tail - buf -> start ;
			int datasize_blk2 = BUFFER_FULL_DUMP_SIZE - datasize_blk1 ;
			buf -> tail = buf -> end - datasize_blk2 ;
			buf -> write_size = datasize_blk2 ;
			buf -> read_size = buf -> tail - buf -> head ;
		}
		/*print_circular_buffer ( buf ) ; */
		return SLURM_SUCCESS ;
	}

	if ( buf->tail > buf->head )
	{
		new_buffer = xmalloc ( buf->buf_size + buf -> incremental_size  ) ;
		data_size = buf->tail - buf->head ;
		memcpy ( new_buffer , buf->head , data_size ) ;
		xfree ( buf -> buffer ) ;

	}
	else if ( buf->tail <= buf->head ) /* CASE B */
	{
		new_buffer = xmalloc ( buf->buf_size + buf -> incremental_size ) ;
		data_size_blk1 = buf->end - buf->head ;
		data_size_blk2 = buf->tail - buf->start ;
		data_size = data_size_blk1 + data_size_blk2 ;
		memcpy ( new_buffer , buf->head , data_size_blk1 ) ;
		memcpy ( new_buffer + data_size_blk1 , buf->start , data_size_blk2 ) ;
		xfree ( buf -> buffer ) ;
	}
	else 
	{
		fatal ( "Logically Impossible, but we are doing buffer arithmetic" ) ;
	}

	/* set up new state variables */
	/* !!!statement order below does matter */
	buf -> buffer = new_buffer ;
	buf -> start = new_buffer ;
	buf -> head = new_buffer ;
	buf -> tail = new_buffer + data_size ;
	buf -> end = new_buffer + buf->buf_size + buf -> incremental_size ;
	buf -> buf_size += buf -> incremental_size ;
	buf -> read_size = data_size ;
	buf -> write_size = buf -> end - buf-> tail ;

	return SLURM_SUCCESS ;
}
