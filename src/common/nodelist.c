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
#include <src/common/nodelist.h>
#include <src/common/xmalloc.h>

/*
 * bitfmt2int - convert a string describing bitmap (e.g. "0-30,45,50-60") 
 *	into an array of integer (start/end) pairs terminated by -1
 *	(e.g. "0, 30, 45, 45, 50, 60, -1")
 * input: bitmap string as produced by bitstring.c : bitfmt
 * output: an array of integers
 * NOTE: the caller must xfree the returned memory
 */
int *
bitfmt2int (char *bit_str_ptr) 
{
	int *bit_int_ptr, i, bit_inx, size, sum, start_val;

	if (bit_str_ptr == NULL) 
		return NULL;
	size = strlen (bit_str_ptr) + 1;
	bit_int_ptr = xmalloc ( sizeof (int *) * size);
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

