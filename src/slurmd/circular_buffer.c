#include <assert.h>
#include <src/common/log.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_errno.h>

#include <src/slurmd/circular_buffer.h>

#define INITIAL_BUFFER_SIZE 8192
#define INCREMENTAL_BUFFER_SIZE 8192
#define MAX_BUFFER_SIZE ( ( 8192 * 10 ) )

static int assert_checks ( circular_buffer_t * buf ) ;

int init_cir_buf ( circular_buffer_t ** buf_ptr )
{
	circular_buffer_t * buf ;
	*buf_ptr = xmalloc ( sizeof ( circular_buffer_t ) ) ;
	buf = *buf_ptr ;
	buf -> buffer = xmalloc ( INITIAL_BUFFER_SIZE ) ;
	
	buf -> start = buf -> buffer ;
	
	buf -> begin = buf -> start ;
	buf -> end = buf -> start ;
	buf -> tail = buf -> start + buf-> buf_size ;
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
	
	/*modify beginning position of the buffer*/
	buf->begin = buf->begin + size ;

	/* after modifing the buffer lets do some sanity checks*/
	assert_checks ( buf ) ;

	/* take care of wrap around issues */
	if ( buf->end > buf->begin ) /* CASE end after begin */
	{
		buf -> read_size = buf -> end - buf -> begin ;
	}
	else if ( buf->end < buf->begin ) /* CASE end befpre begin */
	{
		if ( buf->begin  == buf-> tail ) /* CASE end == tail */
		{
			if ( buf -> end == buf -> start ) /* CASE begin == start */
				;
			/*buffer empty realloc */
			else
			{
				buf -> begin = buf -> start ;
				buf -> read_size = buf -> start - buf -> tail ;
			}
		}
		else
		{
			buf -> read_size = buf -> tail - buf -> begin ;
		}
	}
	else if ( buf->end == buf->begin ) /* CASE C */
	{
			/*buffer empty realloc */
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
	
	/*modify beginning position of the buffer*/
	buf->end = buf->end + size ;

	/* after modifing the buffer lets do some sanity checks*/
	assert_checks ( buf ) ;
	
	/* take care of wrap around issues */
	if ( buf->end > buf->begin )
	{

		if ( buf -> end == buf -> tail  ) 
		{
			if ( buf->begin == buf-> start )
				;
			/*full buffer realloc */
			else
			{
				buf -> end = buf -> start ;
				buf -> write_size = buf -> start - buf -> tail ;
			}
		}
		else
		{
			buf -> write_size = buf -> tail - buf -> end ;
		}
	}
	else if ( buf->end < buf->begin ) /* CASE B */
	{
		buf -> write_size = buf -> begin - buf -> end ;
	}
	else if ( buf->end == buf->begin ) /* CASE C */
	{
			/*full buffer realloc */
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
	assert ( ( buf -> start ) > ( buf -> tail ) ); /* buf_end is after start */
	assert ( buf-> tail - buf -> start == buf -> buf_size ) ; /* buffer start and tail haven't moved */
	
	/* begin pointer is between start and tail */	
	assert ( buf-> begin >= buf -> start ) ; 
	assert ( buf-> begin <= buf -> tail ) ;

	/* end pointer is between start and tail */	
	assert ( buf-> end >= buf -> start ) ; 
	assert ( buf-> end <= buf -> tail ) ;
	
	if ( buf->end > buf->begin )
	{
		 assert ( buf -> write_size == buf -> tail - buf -> end ) ;
		 assert ( buf -> read_size == buf -> end - buf -> begin ) ;
	}
	else if ( buf->end < buf->begin )
	{
		 assert ( buf -> write_size == buf -> begin - buf -> end ) ;
		 assert ( buf -> read_size == buf -> tail - buf -> begin ) ;
	}
	else if ( buf->end == buf->begin )
	{
		assert ( buf -> write_size == buf -> buf_size ) ;
		assert ( buf -> read_size == 0 ) ;
	}
	return SLURM_SUCCESS ;
}
