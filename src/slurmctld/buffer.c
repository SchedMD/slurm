/* $Id$ */

/* seemingly unused buffer functions */

/* 
 * read_buffer - read a line from the specified buffer
 * input: buffer - pointer to read buffer, must be allocated by alloc()
 *        buffer_offset - byte offset in buffer, read location
 *        buffer_size - byte size of buffer
 *        line - pointer to location to be loaded with pointer to the line
 * output: buffer_offset - incremented by  size of size plus the value size itself
 *         line - set to pointer to the line
 *         returns 0 if no error or EFAULT on end of buffer, EINVAL on bad tag 
 */
int 
read_buffer (char *buffer, int *buffer_offset, int buffer_size, char **line) 
{
	if ((*buffer_offset) >= buffer_size)
		return EFAULT;
	line[0] = &buffer[*buffer_offset];
	(*buffer_offset) += (strlen (line[0]) + 1);

	if ((*buffer_offset) > buffer_size)
		return EFAULT;
	return 0;
}

/* 
 * write_buffer - write the specified line to the specified buffer, 
 *               enlarging the buffer as needed
 * input: buffer - pointer to write buffer, must be allocated by alloc()
 *        buffer_offset - byte offset in buffer, write location
 *        buffer_size - byte size of buffer
 *        line - pointer to data to be writen
 * output: buffer - value is written here, buffer may be relocated by xrealloc()
 *         buffer_offset - incremented by value_size
 *         returns 0 if no error or errno otherwise 
 */
int 
write_buffer (char **buffer, int *buffer_offset, int *buffer_size, char *line) 
{
	int line_size;

	line_size = strlen (line) + 1;
	if ((*buffer_offset + line_size) >= *buffer_size) {
		(*buffer_size) += line_size + 8096;
		if (buffer[0])
			xrealloc (buffer[0], *buffer_size);
		else
			buffer[0] = xmalloc(*buffer_size);
	}

	memcpy (buffer[0] + (*buffer_offset), line, line_size);
	(*buffer_offset) += line_size;
	return 0;
}


