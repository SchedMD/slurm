
#ifndef _CIRCULAR_BUFFER_H
#define _CIRCULAR_BUFFER_H

typedef struct circular_buffer {
	char         *buffer;	/* buffer pointer - this never changes 
				   except during allocate and deallocate */

	char         *start;	/* buffer pointer copy - this never changes 
				   except during allocate and deallocate , 
				   but it is used in a lot of arithmetic 
				   hence the paranoia copy 		 */

	char         *end;	/* one char past the last char of the buffer
				   - ths never changes except during allocate 
				   and deallocate , but it is used in a lot 
				   of arithmetic 			 */
	
	unsigned int buf_size;	/* buffer size - this never changes except 
				   during allocate and deallocate 	 */

	unsigned int read_size;	/* buffer size that can be read 	 */

	unsigned int write_size;/* buffer size that can be written 	 */

	char         *head;	/* beginning of the used portion of the 
				   buffer 				 */

	char         *tail;	/* end of the used portion of the buffer */

	unsigned int min_size;	/* min buffer size 			 */
	unsigned int max_size;	/* max buffer size 			 */

	unsigned int incremental_size;	/* incremental buffer size 	 */

} circular_buffer_t;

typedef struct cir_buf_line {
	char   *line[2];
	size_t line_length[2];
	size_t line_count;
	size_t max_line_length;
} cir_buf_line_t;

/* init_circular_buffer2
 * allocated buffer structure and sets default parameter according to passed parameters
 * OUT buf_ptr		- the allocate buffer 
 * IN min_size		- buffer min size default 8K
 * IN max_size		- buffer max size 10 * 8K
 * IN incremental_size	- buffer increment size 8K
 */
int inline init_circular_buffer2(circular_buffer_t ** buf_ptr,
				 int min_size, int max_size,
				 int incremental_size);

/* init_circular_buffer2
 * allocated buffer structure and sets default parameter according to passed parameters
 * OUT buf_ptr		- the allocate buffer 
 */
int inline init_circular_buffer(circular_buffer_t ** buf_ptr);

/* free_circular_buffer
 * deallocates the buffer
 * IN buf_ptr		- the allocated buffer 
 */
void inline free_circular_buffer(circular_buffer_t * buf_ptr);

/* print_circular_buffer
 * prints the buffer
 * IN buf_ptr		- the buffer to print
 */
void inline print_circular_buffer(circular_buffer_t * buf_ptr);

/* cir_buf_read_update
 * updated the buffer state after a read from the buffer
 * IN buf_ptr		- the allocated buffer 
 * IN size 		- size of the read 
 */
int cir_buf_read_update(circular_buffer_t * buf, unsigned int size);

/* cir_buf_write_update
 * updated the buffer state after a write to the buffer
 * IN buf_ptr		- the allocated buffer 
 * IN size 		- size of the write 
 */
int cir_buf_write_update(circular_buffer_t * buf, unsigned int size);

int cir_buf_get_line(circular_buffer_t * buf, cir_buf_line_t * line);

int cir_buf_update_line(circular_buffer_t * buf, cir_buf_line_t * line);

#endif /* !_CIRCULAR_BUFFER_H */
