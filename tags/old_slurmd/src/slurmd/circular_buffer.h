/*****************************************************************************\
 *  circular_buffer.h
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

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
