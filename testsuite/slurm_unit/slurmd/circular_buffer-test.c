#include <unistd.h>
#include <src/slurmd/circular_buffer.h>
#include <src/common/slurm_errno.h>
#include <src/common/log.h>

int main ( int argc , char ** argv )
{
	circular_buffer_t * cir1 ;

	init_circular_buffer ( & cir1 ) ;
	free_circular_buffer ( cir1 ) ;

	init_circular_buffer ( & cir1 ) ;
	info ( "init") ;
	print_circular_buffer ( cir1 ) ;
	write_update ( cir1 , 8192 ) ;
	info ( "write 8k") ;
	print_circular_buffer ( cir1 ) ;
	read_update ( cir1 , 8192 ) ;
	info ( "read 8k") ;
	print_circular_buffer ( cir1 ) ;
	write_update ( cir1 , 8192 ) ;
	info ( "write 8k") ;
	print_circular_buffer ( cir1 ) ;
	read_update ( cir1 , 8192 ) ;
	info ( "read 8k") ;
	print_circular_buffer ( cir1 ) ;
	free_circular_buffer ( cir1 ) ;

	init_circular_buffer ( & cir1 ) ;
	print_circular_buffer ( cir1 ) ;
	write_update ( cir1 , 8192 ) ;
	info ( "write 8k") ;
	print_circular_buffer ( cir1 ) ;
	read_update ( cir1 , 8192 ) ;
	info ( "read 8k") ;
	print_circular_buffer ( cir1 ) ;
	write_update ( cir1 , 6 * 1024 ) ;
	info ( "write 6k") ;
	print_circular_buffer ( cir1 ) ;
	read_update ( cir1 , 4 * 1024 ) ;
	info ( "read 4k") ;
	print_circular_buffer ( cir1 ) ;
	write_update ( cir1 , 4 * 1024 ) ;
	info ( "write 4k") ;
	print_circular_buffer ( cir1 ) ;
	read_update ( cir1 , 4 * 1024 ) ;
	info ( "read 4k") ;
	print_circular_buffer ( cir1 ) ;
	read_update ( cir1 , 2 * 1024 ) ;
	info ( "read 2k") ;
	print_circular_buffer ( cir1 ) ;
	free_circular_buffer ( cir1 ) ;
	
	return SLURM_SUCCESS ;
}
