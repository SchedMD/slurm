/*
 * bits_bytes.c  - tools for manipulating bitmaps and strings
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM  1

#ifdef have_config_h
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "list.h"
#include "slurm.h"

#define BUF_SIZE 1024
#define SEPCHARS " \n\t"

int node_record_count = 0;	/* count of records in the node record table */

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main (int argc, char *argv[]) {
	char in_line[128];
	char *out_line;
	int error_code, int_found, i, size;
	char *string_found;
	unsigned *map1, *map2, *map3;
	char *buffer, *format;
	int buffer_offset, buffer_size;
	int start_inx, end_inx, count_inx;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;

	error_code = log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	if (error_code)
		printf ("log_init error %d\n", error_code);

	printf ("testing string manipulation functions...\n");
	strcpy (in_line,
		"test1=UNLIMITED test2=1234 test3 left_over test4=my,string");

	error_code = load_integer (&int_found, "test1=", in_line);
	if (error_code)
		printf ("load_integer error on test1\n");
	if (int_found != -1)
		printf ("load_integer parse error on test1, got %d\n",
			int_found);

	error_code = load_integer (&int_found, "test2=", in_line);
	if (error_code)
		printf ("load_integer error on test2\n");
	if (int_found != 1234)
		printf ("load_integer parse error on test2, got %d\n",
			int_found);

	error_code = load_integer (&int_found, "test3", in_line);
	if (error_code)
		printf ("load_integer error on test3\n");
	if (int_found != 1)
		printf ("load_integer parse error on test3, got %d\n",
			int_found);

	string_found = NULL;	/* NOTE: arg1 of load_string is freed if set */
	error_code = load_string (&string_found, "test4=", in_line);
	if (error_code)
		printf ("load_string error on test4\n");
	if (strcmp (string_found, "my,string") != 0)
		printf ("load_string parse error on test4, got :%s:\n",
			string_found);
	xfree (string_found);

	printf ("NOTE: we expect this to print \"leftover\"\n");
	report_leftover (in_line, 0);

	printf ("\n\n_testing bitmap manipulation functions...\n");
	node_record_count = 97;
	size = (node_record_count + (sizeof (unsigned) * 8) -
		1) / (sizeof (unsigned) * 8);
	size *= (sizeof (unsigned) * 8);
	map1 = xmalloc (size);
	memset (map1, 0, size);
	bitmap_set (map1, 23);
	bitmap_set (map1, 71);
	out_line = bitmap_print (map1);
	printf ("bitmap_print #1 map1 shows %s\n", out_line);
	xfree (out_line);
	map2 = bitmap_copy (map1);
	out_line = bitmap_print (map2);
	printf ("bitmap_print #2 map2 shows %s\n", out_line);
	xfree (out_line);
	map3 = bitmap_copy (map1);
	bitmap_clear (map2, 23);
	if (bitmap_is_super (map2, map1) != 1)
		printf ("error: bitmap_is_super error 1\n");
	if (bitmap_is_super (map1, map2) != 0)
		printf ("error: bitmap_is_super error 2\n");
	bitmap_or (map3, map2);
	if (bitmap_value (map3, 23) != 1)
		printf ("error: bitmap_or error 1\n");
	if (bitmap_value (map3, 71) != 1)
		printf ("error: bitmap_or error 2\n");
	if (bitmap_value (map3, 93) != 0)
		printf ("error: bitmap_or error 3\n");
	bitmap_and (map3, map2);
	if (bitmap_value (map3, 23) != 0)
		printf ("error: bitmap_and error 1\n");
	if (bitmap_value (map3, 71) != 1)
		printf ("error: bitmap_and error 2\n");
	if (bitmap_value (map3, 93) != 0)
		printf ("error: bitmap_and error 3\n");
	out_line = bitmap_print (map3);
	printf ("bitmap_print #3 map3 shows %s\n", out_line);
	xfree (out_line);

	bitmap_fill (map1);
	out_line = bitmap_print (map1);
	if (bitmap_value (map1, 34) != 1)
		printf ("error: bitmap_fill error 1\n");
	printf ("bitmap_print #4 map1 shows %s\n", out_line);
	xfree (out_line);

	memset (map1, 0, size);
	for (i = 0; i < 10; i++) {
		bitmap_set (map1, (i + 35));
		if (i > 0)
			bitmap_set (map1, (i + 65));
	}
	out_line = bitmap_print (map1);
	printf ("bitmap_print #6 map1 shows %s\n", out_line);
	size = bitmap_count (map1);
	if (size != 19)
		printf ("error: bitmap_count error, %d\n", size);

	printf ("\n\n_testing buffer i/o functions...\n");
	buffer = NULL;
	buffer_offset = buffer_size = 0;
	error_code =
		write_buffer (&buffer, &buffer_offset, &buffer_size,
			      "val1\n");
	if (error_code)
		printf ("write_buffer error on test1\n");
	error_code =
		write_buffer (&buffer, &buffer_offset, &buffer_size,
			      "val2\n");
	if (error_code)
		printf ("write_buffer error on test2\n");
	buffer_offset = 0;
	error_code =
		read_buffer (buffer, &buffer_offset, buffer_size, &out_line);
	if (error_code)
		printf ("read_buffer error on test1\n");
	if (strcmp (out_line, "val1\n") != 0)
		printf ("read_buffer error on test2\n");
	error_code =
		read_buffer (buffer, &buffer_offset, buffer_size, &out_line);
	if (error_code)
		printf ("read_buffer error on test3\n");
	if (strcmp (out_line, "val2\n") != 0)
		printf ("read_buffer error on test4\n");
	xfree(buffer);

	/* check node name parsing */
	out_line = "linux[003-234]";
	error_code =
		parse_node_name (out_line, &format, &start_inx, &end_inx,
				 &count_inx);
	if (error_code != 0)
		printf ("error: parse_node_name error %d\n", error_code);
	else {
		if ((start_inx != 3) || (end_inx != 234))
			printf ("error: parse_node_name failure\n");
		printf ("parse_node_name of \"%s\" produces format \"%s\", %d to %d, %d records\n", out_line, format, start_inx, end_inx, count_inx);
		if (format)
			xfree (format);
	}

	exit (0);
}
#endif


/*
 * bitmap_and - and two bitmaps together
 * input: bitmap1 and bitmap2 - the bitmaps to and
 * output: bitmap1 is set to the value of bitmap1 & bitmap2
 */
void bitmap_and (unsigned *bitmap1, unsigned *bitmap2) {
	int i, size;

	if ((bitmap1 == NULL) || (bitmap2 == NULL)) {
		fatal ("bitmap_and: bitmap pointer is NULL\n");
	}

	size = (node_record_count + (sizeof (unsigned) * 8) -
		1) / (sizeof (unsigned) * 8);
	for (i = 0; i < size; i++) {
		bitmap1[i] &= bitmap2[i];
	}			
}


/*
 * bitmap_clear - clear the specified bit in the specified bitmap
 * input: bitmap - the bit map to manipulate
 *        position - postition to clear
 * output: bitmap - updated value
 */
void bitmap_clear (unsigned *bitmap, int position) {
	int val, bit;
	unsigned mask;

	if (bitmap == NULL) {
		fatal ("bitmap_clear: bitmap pointer is NULL\n");
	}

	val = position / (sizeof (unsigned) * 8);
	bit = position % (sizeof (unsigned) * 8);
	mask = ~(0x1 << ((sizeof (unsigned) * 8) - 1 - bit));

	bitmap[val] &= mask;
}


/*
 * bitmap_copy - create a copy of a bitmap
 * input: bitmap - the bitmap create a copy of
 * output: returns pointer to copy of bitmap or NULL if error (no memory)
 * NOTE:  the returned value must be freed by the calling routine
 */
unsigned * bitmap_copy (unsigned *bitmap) {
	int size;
	unsigned *output;

	if (bitmap == NULL) {
		fatal ("bitmap_copy: bitmap pointer is NULL\n");
	}

	size = (node_record_count + (sizeof (unsigned) * 8) - 1) / 8;	/* bytes */
	output = xmalloc (size);

	(void) memcpy (output, bitmap, size);
	return output;
}


/*
 * bitmap_count - return the count of set bits in the specified bitmap
 * input: bitmap - the bit map to get count from
 * output: returns the count of set bits
 * NOTE: this routine adapted from linux 2.4.9 <linux/bitops.h>.
 */
int bitmap_count (unsigned *bitmap) {
	int count, byte, size, word, res;
	unsigned scan;

	if (bitmap == NULL) {
		fatal ("bitmap_count: bitmap pointer is NULL\n");
	}

	count = 0;
	size = (node_record_count + (sizeof (unsigned) * 8) - 1) / 8;	/* bytes */
	size /= sizeof (unsigned);	/* count of unsigned's */
	for (word = 0; word < size; word++) {
		if (sizeof (unsigned) == 4) {
			res = (bitmap[word] & 0x55555555) +
				((bitmap[word] >> 1) & 0x55555555);
			res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
			res = (res & 0x0f0f0f0f) + ((res >> 4) & 0x0f0f0f0f);
			res = (res & 0x00ff00ff) + ((res >> 8) & 0x00ff00ff);
			res = (res & 0x0000ffff) + ((res >> 16) & 0x0000ffff);
			count += res;
		}
		else if (sizeof (unsigned) == 8) {
			res = (bitmap[word] & 0x5555555555555555) +
				((bitmap[word] >> 1) & 0x5555555555555555);
			res = (res & 0x3333333333333333) +
				((res >> 2) & 0x3333333333333333);
			res = (res & 0x0f0f0f0f0f0f0f0f) +
				((res >> 4) & 0x0f0f0f0f0f0f0f0f);
			res = (res & 0x00ff00ff00ff00ff) +
				((res >> 8) & 0x00ff00ff00ff00ff);
			res = (res & 0x0000ffff0000ffff) +
				((res >> 16) & 0x0000ffff0000ffff);
			res = (res & 0x00000000ffffffff) +
				((res >> sizeof (unsigned) *
				  4) & 0x00000000ffffffff);
			count += res;
		}
		else {
			for (byte = 0; byte < (sizeof (unsigned) * 8);
			     byte += 8) {
				scan = bitmap[word] >>
					((sizeof (unsigned) * 8) - 8 - byte);
				if (scan & 0x01)
					count++;
				if (scan & 0x02)
					count++;
				if (scan & 0x04)
					count++;
				if (scan & 0x08)
					count++;
				if (scan & 0x10)
					count++;
				if (scan & 0x20)
					count++;
				if (scan & 0x40)
					count++;
				if (scan & 0x80)
					count++;
			}
		}
	}
	return count;
}


/*
 * bitmap_fill - fill the provided bitmap so that all bits between the highest and lowest
 * 	previously set bits are also set (i.e fill in the gaps to make it contiguous)
 * input: bitmap - pointer to the bit map to fill in
 * output: bitmap - the filled in bitmap
 */
void bitmap_fill (unsigned *bitmap) {
	int bit, size, word;
	int first, last, position, gap;
	unsigned mask;

	if (bitmap == NULL) {
		fatal ("bitmap_fill: bitmap pointer is NULL\n");
	}

	first = last = position = gap = -1;
	size = (node_record_count + (sizeof (unsigned) * 8) - 1) / 8;	/* bytes */
	size /= sizeof (unsigned);	/* count of unsigned's */
	for (word = 0; word < size; word++) {
		for (bit = 0; bit < (sizeof (unsigned) * 8); bit++) {
			position++;
			mask = (0x1 << ((sizeof (unsigned) * 8) - 1 - bit));
			if (bitmap[word] & mask) {
				if (first == -1)
					first = position;
				if ((last != (position - 1)) && (last != -1))
					gap = 1;
				last = position;
			}
		}
	}

	if (gap == -1)
		return;

	position = -1;
	for (word = 0; word < size; word++) {
		for (bit = 0; bit < (sizeof (unsigned) * 8); bit++) {
			position++;
			if (position <= first)
				continue;
			if (position >= last)
				continue;
			mask = (0x1 << ((sizeof (unsigned) * 8) - 1 - bit));
			bitmap[word] |= mask;
		}
	}
}


/* 
 * bitmap_is_super - report if one bitmap's contents are a superset of another
 * input: bitmap1 and bitmap2 - the bitmaps to compare
 * output: return 1 if if all bits in bitmap1 are also in bitmap2, 0 otherwise 
 */
int bitmap_is_super (unsigned *bitmap1, unsigned *bitmap2) {
	int i, size;

	if ((bitmap1 == NULL) || (bitmap2 == NULL)) {
		fatal ("bitmap_or: bitmap pointer is NULL\n");
	}

	size = (node_record_count + (sizeof (unsigned) * 8) -
		1) / (sizeof (unsigned) * 8);
	for (i = 0; i < size; i++) {
		if (bitmap1[i] != (bitmap1[i] & bitmap2[i]))
			return 0;
	}
	return 1;
}


/*
 * bitmap_or - or two bitmaps together
 * input: bitmap1 and bitmap2 - the bitmaps to or
 * output: bitmap1 is set to the value of bitmap1 | bitmap2
 */
void bitmap_or (unsigned *bitmap1, unsigned *bitmap2) {
	int i, size;

	if ((bitmap1 == NULL) || (bitmap2 == NULL)) {
		fatal ("bitmap_or: bitmap pointer is NULL\n");
	}

	size = (node_record_count + (sizeof (unsigned) * 8) - 1) / 
		(sizeof (unsigned) * 8);
	for (i = 0; i < size; i++) {
		bitmap1[i] |= bitmap2[i];
	}
}


/*
 * bitmap_print - convert the specified bitmap into a printable hexadecimal string
 * input: bitmap - the bit map to print
 * output: returns a string
 * NOTE: the returned string must be freed by the calling program
 */
char * bitmap_print (unsigned *bitmap) {
	int i, j, k, size, nibbles;
	char *output, temp_str[2];

	if (bitmap == NULL) {
		fatal ("bitmap_print: bitmap pointer is NULL\n");
	}

	size = (node_record_count + (sizeof (unsigned) * 8) -
		1) / (sizeof (unsigned) * 8);
	nibbles = (node_record_count + 3) / 4;
	output = (char *) xmalloc (nibbles + 3);

	strcpy (output, "0x");
	k = 0;
	for (i = 0; i < size; i++) {	/* each unsigned */
		for (j = ((sizeof (unsigned) * 8) - 4); j >= 0; j -= 4) {	/* each nibble */
			sprintf (temp_str, "%x", ((bitmap[i] >> j) & 0xf));
			strcat (output, temp_str);
			k++;
			if (k == nibbles)
				return output;
		}
	}
	return output;
}


/*
 * bitmap_set - set the specified bit in the specified bitmap
 * input: bitmap - the bit map to manipulate
 *        position - postition to set
 * output: bitmap - updated value
 */
void bitmap_set (unsigned *bitmap, int position) {
	int val, bit;
	unsigned mask;

	if (bitmap == NULL) {
		fatal ("bitmap_set: bitmap pointer is NULL\n");
	}

	val = position / (sizeof (unsigned) * 8);
	bit = position % (sizeof (unsigned) * 8);
	mask = (0x1 << ((sizeof (unsigned) * 8) - 1 - bit));

	bitmap[val] |= mask;
}


/*
 * bitmap_value - return the value of specified bit in the specified bitmap
 * input: bitmap - the bit map to get value from
 *        position - postition to get
 * output: normally returns the value 0 or 1
 */
int bitmap_value (unsigned *bitmap, int position) {
	int val, bit;
	unsigned mask;

	if (bitmap == NULL) {
		fatal ("bitmap_value: bitmap pointer is NULL\n");
	}

	val = position / (sizeof (unsigned) * 8);
	bit = position % (sizeof (unsigned) * 8);
	mask = (0x1 << ((sizeof (unsigned) * 8) - 1 - bit));

	mask &= bitmap[val];
	if (mask == 0)
		return 0;
	else
		return 1;
}


/*
 * load_integer - parse a string for a keyword, value pair  
 * input: *destination - location into which result is stored
 *        keyword - string to search for
 *        in_line - string to search for keyword
 * output: *destination - set to value, no change if value not found, 
 *             set to 1 if keyword found without value, 
 *             set to -1 if keyword followed by "unlimited"
 *         in_line - the keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: in_line is overwritten, do not use a constant
 */
int load_integer (int *destination, char *keyword, char *in_line) {
	char scratch[BUF_SIZE];	/* scratch area for parsing the input line */
	char *str_ptr1, *str_ptr2, *str_ptr3;
	int i, str_len1, str_len2;

	str_ptr1 = (char *) strstr (in_line, keyword);
	if (str_ptr1 != NULL) {
		str_len1 = strlen (keyword);
		strcpy (scratch, str_ptr1 + str_len1);
		if ((scratch[0] == (char) NULL) || 
		    (isspace ((int) scratch[0]))) {	/* keyword with no value set */
			*destination = 1;
			str_len2 = 0;
		}
		else {
			str_ptr2 =
				(char *) strtok_r (scratch, SEPCHARS, &str_ptr3);
			str_len2 = strlen (str_ptr2);
			if (strcmp (str_ptr2, "UNLIMITED") == 0)
				*destination = -1;
			else if ((str_ptr2[0] >= '0') && (str_ptr2[0] <= '9')) {
				*destination =
					(int) strtol (scratch, (char **) NULL, 10);
			}
			else {
				error ("load_integer: bad value for keyword %s\n",
					keyword);
				return EINVAL;
			}
		}

		for (i = 0; i < (str_len1 + str_len2); i++) {
			str_ptr1[i] = ' ';
		}
	}
	return 0;
}


/*
 * load_string - parse a string for a keyword, value pair  
 * input: *destination - location into which result is stored
 *        keyword - string to search for
 *        in_line - string to search for keyword
 * output: *destination - set to value, no change if value not found, 
 *	     if *destination had previous value, that memory location is automatically freed
 *         in_line - the keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: destination must be free when no longer required
 * NOTE: if destination is non-NULL at function call time, it will be freed 
 * NOTE: in_line is overwritten, do not use a constant
 */
int load_string (char **destination, char *keyword, char *in_line) {
	char scratch[BUF_SIZE];	/* scratch area for parsing the input line */
	char *str_ptr1, *str_ptr2, *str_ptr3;
	int i, str_len1, str_len2;

	str_ptr1 = (char *) strstr (in_line, keyword);
	if (str_ptr1 != NULL) {
		str_len1 = strlen (keyword);
		strcpy (scratch, str_ptr1 + str_len1);
		if ((scratch[0] == (char) NULL) || 
		    (isspace ((int) scratch[0]))) {	/* keyword with no value set */
			error ("load_string: keyword %s lacks value\n",
				keyword);
			return EINVAL;
		}
		str_ptr2 = (char *) strtok_r (scratch, SEPCHARS, &str_ptr3);
		str_len2 = strlen (str_ptr2);
		if (destination[0] != NULL)
			xfree (destination[0]);
		destination[0] = (char *) xmalloc (str_len2 + 1);
		strcpy (destination[0], str_ptr2);
		for (i = 0; i < (str_len1 + str_len2); i++) {
			str_ptr1[i] = ' ';
		}
	}
	return 0;
}

/* 
 * parse_node_name - parse the node name for regular expressions and return a sprintf  
 * format generate multiple node names as needed.
 * input: node_name - node name to parse
 * output: format - sprintf format for generating names
 *         start_inx - first index to used
 *         end_inx - last index value to use
 *         count_inx - number of index values to use (will be zero if none)
 *         return 0 if no error, error code otherwise
 * NOTE: the calling program must execute free(format) when the storage location 
 *       is no longer needed
 */
int parse_node_name (char *node_name, char **format, int *start_inx, int *end_inx,
		 int *count_inx) {
	int base, format_pos, precision, i;
	char type[1];

	i = strlen (node_name);
	format[0] = (char *) xmalloc (i + 1);

	*start_inx = 0;
	*end_inx = 0;
	*count_inx = 0;
	format_pos = 0;
	base = 0;
	format[0][format_pos] = (char) NULL;
	i = 0;
	while (1) {
		if (node_name[i] == (char) NULL)
			break;
		if (node_name[i] == '\\') {
			if (node_name[++i] == (char) NULL)
				break;
			format[0][format_pos++] = node_name[i++];
		}
		else if (node_name[i] == '[') {	/* '[' preceeding number range */
			if (node_name[++i] == (char) NULL)
				break;
			if (base != 0) {
				error ("parse_node_name: invalid '[' in node name %s\n",
					node_name);
				xfree (format[0]);
				return EINVAL;
			}
			if (node_name[i] == 'o') {
				type[0] = node_name[i++];
				base = 8;
			}
			else {
				type[0] = 'd';
				base = 10;
			}
			precision = 0;
			while (1) {
				if ((node_name[i] >= '0')
				    && (node_name[i] <= '9')) {
					*start_inx =
						((*start_inx) * base) +
						(int) (node_name[i++] - '0');
					precision++;
					continue;
				}
				if (node_name[i] == '-') {	/* '-' between numbers */
					i++;
					break;
				}
				error ("parse_node_name: invalid '%c' in node name %s\n",
					 node_name[i], node_name);
				xfree (format[0]);
				return EINVAL;
			}
			while (1) {
				if ((node_name[i] >= '0')
				    && (node_name[i] <= '9')) {
					*end_inx =
						((*end_inx) * base) +
						(int) (node_name[i++] - '0');
					continue;
				}
				if (node_name[i] == ']') {	/* ']' terminating number range */
					i++;
					break;
				}
				error ("parse_node_name: invalid '%c' in node name %s\n",
					 node_name[i], node_name);
				xfree (format[0]);
				return EINVAL;
			}
			*count_inx = (*end_inx - *start_inx) + 1;
			format[0][format_pos++] = '%';
			format[0][format_pos++] = '.';
			if (precision > 9)
				format[0][format_pos++] =
					'0' + (precision / 10);
			format[0][format_pos++] = '0' + (precision % 10);
			format[0][format_pos++] = type[0];
		}
		else {
			format[0][format_pos++] = node_name[i++];
		}
	}
	format[0][format_pos] = (char) NULL;
	return 0;
}



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
int read_buffer (char *buffer, int *buffer_offset, int buffer_size, char **line) {
	if ((*buffer_offset) >= buffer_size)
		return EFAULT;
	line[0] = &buffer[*buffer_offset];
	(*buffer_offset) += (strlen (line[0]) + 1);

	if ((*buffer_offset) > buffer_size)
		return EFAULT;
	return 0;
}


/* 
 * report_leftover - report any un-parsed (non-whitespace) characters on the
 * configuration input line.
 * input: in_line - what is left of the configuration input line.
 *        line_num - line number of the configuration file.
 * output: none
 */
void report_leftover (char *in_line, int line_num) {
	int bad_index, i;

	bad_index = -1;
	for (i = 0; i < strlen (in_line); i++) {
		if (isspace ((int) in_line[i]) || (in_line[i] == '\n'))
			continue;
		bad_index = i;
		break;
	}

	if (bad_index == -1)
		return;
	error ("report_leftover: ignored input on line %d of configuration: %s\n",
		line_num, &in_line[bad_index]);
	return;
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
int write_buffer (char **buffer, int *buffer_offset, int *buffer_size, char *line) {
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
