#ifndef _CIRCULAR_BUFFER_H
#define _CIRCULAR_BUFFER_H
typedef struct circular_buffer
{
	char * buffer ; /* buffer pointer - this never changes except during allocate and deallocate */
	char * start ; /* buffer pointer copy - this never changes except during allocate and deallocate , but it is used in a lot of arithmetic hence the paranoia copy */
	char * end ; /* one char past the last char of the buffer - ths never changes except during allocate and deallocate , but it is used in a lot of arithmetic */
	unsigned int buf_size ; /* buffer size - this never changes except during allocate and deallocate */
	unsigned int read_size ; /* buffer size that can be read */
	unsigned int write_size ; /* buffer size that can be written */
	char * head ; /* beginning of the used portion of the buffer */
	char * tail; /* end of the used portion of the buffer */
	unsigned int min_size ;
	unsigned int max_size ;
	unsigned int incremental_size ;
} circular_buffer_t ;

int inline init_circular_buffer2 ( circular_buffer_t ** buf_ptr , int min_size , int max_size , int incremental_size ) ;
int inline init_circular_buffer ( circular_buffer_t ** buf_ptr ) ;
void inline free_circular_buffer ( circular_buffer_t * buf_ptr ) ;
void inline print_circular_buffer ( circular_buffer_t * buf_ptr ) ;
int cir_buf_read_update ( circular_buffer_t * buf , unsigned int size ) ;
int cir_buf_write_update ( circular_buffer_t * buf , unsigned int size ) ;
#endif
