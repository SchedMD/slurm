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
} circular_buffer_t ;

int init_circular_buffer ( circular_buffer_t ** buf_ptr ) ;
void free_circular_buffer ( circular_buffer_t * buf_ptr ) ;
int read_update ( circular_buffer_t * buf , unsigned int size ) ;
int write_update ( circular_buffer_t * buf , unsigned int size ) ;
#endif
