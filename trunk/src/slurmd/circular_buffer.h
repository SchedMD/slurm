#ifndef _CIRCULAR_BUFFER_H
#define _CIRCULAR_BUFFER_H
typedef struct circular_buffer
{
	char * buffer ; /* buffer pointer - this never changes except during allocate and deallocate */
	char * start ; /* buffer pointer copy - this never changes except during allocate and deallocate , but it is used in a lot of arithmetic hence the paranoia copy */
	unsigned int buf_size ; /* buffer size - this never changes except during allocate and deallocate */
	unsigned int read_size ; /* buffer size that can be read */
	unsigned int write_size ; /* buffer size that can be written */
	char * begin ; /* beginning of the used portion of the buffer */
	char * end ; /* end of the used portion of the buffer */
	char * tail ; /* one char past the last char of the buffer */
} circular_buffer_t ;

int init_cir_buf ( circular_buffer_t ** buf_ptr ) ;
int read_update ( circular_buffer_t * buf , unsigned int size ) ;
int write_update ( circular_buffer_t * buf , unsigned int size ) ;
#endif
