#include <unistd.h>
#include <src/slurmd/circular_buffer.h>
#include <src/common/slurm_errno.h>
#include <src/common/log.h>

int main ( int argc , char ** argv )
{
	circular_buffer_t * cir1 ;
/*test1*/
	init_circular_buffer ( & cir1 ) ;
	free_circular_buffer ( cir1 ) ;

	
/*test2*/
	init_circular_buffer ( & cir1 ) ;
	
	info ( "init") ;
	print_circular_buffer ( cir1 ) ;
	cir_buf_write_update ( cir1 , 8192 ) ;
	info ( "cir_buf_write 8k") ;
	print_circular_buffer ( cir1 ) ;
	cir_buf_read_update ( cir1 , 8192 ) ;
	info ( "cir_buf_read 8k") ;
	print_circular_buffer ( cir1 ) ;

	cir_buf_write_update ( cir1 , 8192 ) ;
	info ( "cir_buf_write 8k") ;
	print_circular_buffer ( cir1 ) ;
	cir_buf_read_update ( cir1 , 8192 ) ;
	info ( "cir_buf_read 8k") ;
	print_circular_buffer ( cir1 ) ;
	
	free_circular_buffer ( cir1 ) ;

	
/*test3*/
	init_circular_buffer ( & cir1 ) ;
	
	info ( "init") ;
	print_circular_buffer ( cir1 ) ;
	cir_buf_write_update ( cir1 , 8192 ) ;
	info ( "cir_buf_write 8k") ;
	print_circular_buffer ( cir1 ) ;
	cir_buf_read_update ( cir1 , 8192 ) ;
	info ( "cir_buf_read 8k") ;
	print_circular_buffer ( cir1 ) ;
	
	cir_buf_write_update ( cir1 , 6 * 1024 ) ;
	info ( "cir_buf_write 6k") ;
	print_circular_buffer ( cir1 ) ;
	cir_buf_read_update ( cir1 , 4 * 1024 ) ;
	info ( "cir_buf_read 4k") ;
	print_circular_buffer ( cir1 ) ;
	
	cir_buf_write_update ( cir1 , 2 * 1024 ) ;
	info ( "cir_buf_write 2k") ;
	print_circular_buffer ( cir1 ) ;
	cir_buf_write_update ( cir1 , 2 * 1024 ) ;
	info ( "cir_buf_write 2k") ;
	print_circular_buffer ( cir1 ) ;
	cir_buf_write_update ( cir1 , 2 * 1024 ) ;
	info ( "cir_buf_write 2k") ;
	print_circular_buffer ( cir1 ) ;
	cir_buf_read_update ( cir1 , 8 * 1024 ) ;
	info ( "cir_buf_read 8k") ;
	print_circular_buffer ( cir1 ) ;
	
	free_circular_buffer ( cir1 ) ;
	
	return SLURM_SUCCESS ;
}
