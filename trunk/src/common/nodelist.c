/* $Id$ */

/*
 * nodelist.c
 *
 * author: moe jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM  1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

/*
 * bitfmt2int - convert a string describing bitmap (e.g. "0-30,45,50-60") 
 *	into an array of integer (start/end) pairs terminated by -1
 *	(e.g. "0, 30, 45, 45, 50, 60, -1")
 * input: bitmap string as produced by bitstring.c : bitfmt
 * output: an array of integers
 * NOTE: the caller must free the returned memory
 */
int *
bitfmt2int (char *bit_str_ptr) 
{
	int *bit_int_ptr, i, bit_inx, size, sum, start_val;

	if (bit_str_ptr == NULL) 
		return NULL;
	size = strlen (bit_str_ptr) + 1;
	bit_int_ptr = malloc ( sizeof (int *) * size);
	if (bit_int_ptr == NULL)
		return NULL;

	bit_inx = sum = 0;
	start_val = -1;
	for (i = 0; i < size; i++) {
		if (bit_str_ptr[i] >= '0' &&
		    bit_str_ptr[i] <= '9'){
			sum = (sum * 10) + (bit_str_ptr[i] - '0');
		}

		else if (bit_str_ptr[i] == '-') {
			start_val = sum;
			sum = 0;
		}

		else if (bit_str_ptr[i] == ',' || 
		         bit_str_ptr[i] == (char) NULL) {
			if (i == 0)
				break;
			if (start_val == -1)
				start_val = sum;
			bit_int_ptr[bit_inx++] = start_val;
			bit_int_ptr[bit_inx++] = sum;
			start_val = -1;
			sum = 0;
		}
	}
	bit_int_ptr[bit_inx] = -1;
	return bit_int_ptr;
}

/* 
 * parse_node_names - parse the node name for regular expressions and return a 
 *                   sprintf format generate multiple node names as needed.
 * input: node_name - node name to parse
 * output: format - sprintf format for generating names
 *         start_inx - first index to used
 *         end_inx - last index value to use
 *         count_inx - number of index values to use (will be zero if none)
 *         return 0 if no error, error code otherwise
 * NOTE: the calling program must execute free(format) when the storage location 
 *       is no longer needed
 */
int 
parse_node_names (char *node_name, char **format, int *start_inx, int *end_inx,
		 int *count_inx) 
{
	int base, format_pos, precision, i;
	char type[1];

	i = strlen (node_name);
	format[0] = (char *) malloc (i + 1);

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
#if DEBUG_SYSTEM > 1
				printf ("parse_node_name: invalid '[' in node name %s\n", 
					node_name); 
#endif
				free (format[0]);
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
#if DEBUG_SYSTEM > 1
				printf ("parse_node_name: invalid '%c' in node name %s\n", 
					 node_name[i], node_name); 
#endif
				free (format[0]);
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
#if DEBUG_SYSTEM > 1
				printf ("parse_node_name: invalid '%c' in node name %s\n",
					 node_name[i], node_name);
#endif
				free (format[0]);
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

