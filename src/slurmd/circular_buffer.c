#include <assert.h>
#include <src/common/log.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_errno.h>

#include <src/slurmd/circular_buffer.h>

#define INITIAL_BUFFER_SIZE 8192
#define INCREMENTAL_BUFFER_SIZE 8192
#define MAX_BUFFER_SIZE ( ( 8192 * 10 ) )

static int assert_checks ( circular_buffer_t * buf ) ;
static int expand_buffer ( circular_buffer_t * buf ) ;

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
	buf -> buffer = xmalloc ( INITIAL_BUFFER_SIZE ) ;
	buf -> buf_size = INITIAL_BUFFER_SIZE ;

	buf -> start = buf -> buffer ;
	buf -> head = buf -> start ;

	buf -> tail = buf -> start ;
	buf -> end = buf -> start + buf-> buf_size ;

	buf -> read_size = 0 ;
	buf -> write_size = INITIAL_BUFFER_SIZE ;
	return SLURM_SUCCESS ;
}

int read_update ( circular_buffer_t * buf , unsigned int size )
{
	/*if zero read, just return */
	if ( size == 0 )
	{
		info ( "zero length read in cirular buffer" ) ;
		return SLURM_SUCCESS ;
	}

	/* before modifing the buffer lets do some sanity checks*/
	assert ( size <= buf-> read_size ) ;
	assert_checks ( buf ) ;

	/*modify headning position of the buffer*/
	buf->head = buf->head + size ;

	/* after modifing the buffer lets do some sanity checks*/
	assert_checks ( buf ) ;

	/* take care of wrap around issues */
	if ( buf->tail > buf->head ) /* CASE tail after head */
	{
		buf -> read_size = buf -> tail - buf -> head ;
	}
	else if ( buf->tail < buf->head ) /* CASE tail befpre head */
	{
		if ( buf->head  == buf-> end ) /* CASE tail == end */
		{
			if ( buf -> tail == buf -> start ) /* CASE head == start */
			{
				fatal ( "buffer empty, shrink or adjust code not written" ) ;
			}
			else
			{
				buf -> head = buf -> start ;
				buf -> read_size = buf -> start - buf -> end ;
			}
		}
		else
		{
			buf -> read_size = buf -> end - buf -> head ;
		}
	}
	else if ( buf->tail == buf->head ) /* CASE head == tail */
	{
		fatal ( "buffer empty, shrink or adjust code not written" ) ;
	}

	/* final sanity check */
	assert_checks ( buf ) ;
	return SLURM_SUCCESS ;
}

int write_update ( circular_buffer_t * buf , unsigned int size )
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

	/*modify headning position of the buffer*/
	buf->tail = buf->tail + size ;

	/* after modifing the buffer lets do some sanity checks*/
	assert_checks ( buf ) ;

	/* take care of wrap around issues */
	if ( buf->tail > buf->head )
	{

		if ( buf -> tail == buf -> end  ) 
		{
			if ( buf->head == buf-> start )
			{
				expand_buffer ( buf ) ;
			}
			else
			{
				buf -> tail = buf -> start ;
				buf -> write_size = buf -> start - buf -> end ;
			}
		}
		else
		{
			buf -> write_size = buf -> end - buf -> tail ;
		}
	}
	else if ( buf->tail < buf->head ) /* CASE B */
	{
		buf -> write_size = buf -> head - buf -> tail ;
	}
	else if ( buf->tail == buf->head ) /* CASE C */
	{
		expand_buffer ( buf ) ;
	}

	/* final sanity check */
	assert_checks ( buf ) ;
	return SLURM_SUCCESS ;
}

static int assert_checks ( circular_buffer_t * buf )
{
	/* sanity checks */

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

static int expand_buffer ( circular_buffer_t * buf )
{
	char * new_buffer ;
	int data_size ;
	int data_size_blk1 ;
	int data_size_blk2 ;

	if ( buf->buf_size == MAX_BUFFER_SIZE )
	{
		info ( "circular buffer maxed, dumping INCREMENTAL_BUFFER_SIZE of data");
		/* dump data */

	}

	if ( buf->tail > buf->head )
	{
		new_buffer = xmalloc ( buf->buf_size + INCREMENTAL_BUFFER_SIZE ) ;
		data_size = buf->tail - buf->head ;
		memcpy ( new_buffer , buf->head , data_size ) ;
		xfree ( buf -> buffer ) ;

	}
	else if ( buf->tail <= buf->head ) /* CASE B */
	{
		new_buffer = xmalloc ( buf->buf_size + INCREMENTAL_BUFFER_SIZE ) ;
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
	buf -> end = new_buffer + buf->buf_size + INCREMENTAL_BUFFER_SIZE ;
	buf -> buf_size += INCREMENTAL_BUFFER_SIZE ;

	return SLURM_SUCCESS ;
}
