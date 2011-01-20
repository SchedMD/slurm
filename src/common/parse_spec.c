/* $Id$ */
/*****************************************************************************\
 * parse_spec.c - configuration file parser
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef   _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "src/common/log.h"
#include "src/common/parse_spec.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_strcasestr.h"


#define BUFFER_SIZE 1024
#define SEPCHARS " \n\t"

static int   _load_long (long *destination, char *keyword, char *in_line) ;
static int   _load_integer (int *destination, char *keyword, char *in_line) ;
static int   _load_float (float *destination, char *keyword, char *in_line) ;

/*
 * slurm_parser - parse the supplied specification into keyword/value pairs
 *	only the keywords supplied will be searched for. The supplied
 *	specification is altered, overwriting the keyword and value pairs
 *	with spaces.
 * spec - pointer to the string of specifications, sets of three values
 *	(as many sets as required): keyword, type, value
 * IN keyword - string with the keyword to search for including equal sign
 *		(e.g. "name=")
 * IN type - char with value 'd'==int, 'f'==float, 's'==string, 'l'==long
 * IN/OUT value - pointer to storage location for value (char **) for type 's'
 * RET 0 if no error, otherwise errno code
 * NOTE: terminate with a keyword value of "END"
 * NOTE: values of type (char *) are xfreed if non-NULL. caller must xfree any
 *	returned value
 */
int
slurm_parser (char *spec, ...)
{
	va_list ap;
	char *keyword, **str_ptr;
	int error_code, *int_ptr, type;
	long *long_ptr;
	float *float_ptr;

	error_code = 0;
	va_start(ap, spec);
	while (error_code == 0) {
		keyword = va_arg(ap, char *);
		if (strcmp (keyword, "END") == 0)
			break;
		type = va_arg(ap, int);
		switch (type) {
		case 'd':
			int_ptr = va_arg(ap, int *);
			error_code = _load_integer(int_ptr, keyword, spec);
			break;
		case 'f':
			float_ptr = va_arg(ap, float *);
			error_code = _load_float(float_ptr, keyword, spec);
			break;
		case 'l':
			long_ptr = va_arg(ap, long *);
			error_code = _load_long(long_ptr, keyword, spec);
			break;
		case 's':
			str_ptr = va_arg(ap, char **);
			error_code = load_string (str_ptr, keyword, spec);
			break;
		default:
			fatal ("parse_spec: invalid type %c", type);
		}
	}
	va_end(ap);
	return error_code;
}


/*
 * _load_float - parse a string for a keyword, value pair, and load the float
 *	value
 * IN keyword - string to search for
 * IN/OUT in_line - string to search for keyword, the keyword and value
 *	(if present) are overwritten by spaces
 * OUT destination - set to value, no change if value not found
 * RET 0 if no error, otherwise an error code
 * NOTE: in_line is overwritten, do not use a constant
 */
static int
_load_float (float *destination, char *keyword, char *in_line)
{
	char scratch[BUFFER_SIZE];	/* scratch area for parsing the input line */
	char *str_ptr1, *str_ptr2, *str_ptr3;
	int i, str_len1, str_len2;

	str_ptr1 = (char *) slurm_strcasestr (in_line, keyword);
	if (str_ptr1 != NULL) {
		str_len1 = strlen (keyword);
		strcpy (scratch, str_ptr1 + str_len1);
		if ((scratch[0] < '0') && (scratch[0] > '9')) {
			error ("_load_float: bad value for keyword %s",
			       keyword);
			return EINVAL;
		}
		str_ptr2 = (char *) strtok_r (scratch, SEPCHARS, &str_ptr3);
		str_len2 = strlen (str_ptr2);
		*destination = (float) strtod (scratch, (char **) NULL);
		for (i = 0; i < (str_len1 + str_len2); i++) {
			str_ptr1[i] = ' ';
		}
	}
	return 0;
}


/*
 * _load_integer - parse a string for a keyword, value pair, and load the
 *	integer value
 * OUT destination - location into which result is stored
 *	set to 1 if keyword found without value,
 *	set to -1 if keyword followed by "unlimited"
 * IN keyword - string to search for
 * IN/OUT in_line - string to search for keyword, the keyword and value
 *	(if present) are overwritten by spaces
 * RET 0 if no error, otherwise an error code
 */
static int
_load_integer (int *destination, char *keyword, char *in_line)
{
	char scratch[BUFFER_SIZE];	/* scratch area for parsing the input line */
	char *str_ptr1, *str_ptr2, *str_ptr3;
	int i, str_len1, str_len2;

	str_ptr1 = (char *) slurm_strcasestr (in_line, keyword);
	if (str_ptr1 != NULL) {
		str_len1 = strlen (keyword);
		strcpy (scratch, str_ptr1 + str_len1);
		if ((scratch[0] == '\0') ||
		    (isspace ((int) scratch[0]))) {
			/* keyword with no value set */
			*destination = 1;
			str_len2 = 0;
		}
		else {
			str_ptr2 = (char *) strtok_r (scratch, SEPCHARS,
			                              &str_ptr3);
			str_len2 = strlen (str_ptr2);
			if (strcasecmp (str_ptr2, "UNLIMITED") == 0)
				*destination = -1;
			else if ((str_ptr2[0] >= '0') &&
			         (str_ptr2[0] <= '9')) {
				*destination = (int) strtol (scratch,
				                             (char **) NULL,
				                             10);
			}
			else {
				error ("_load_integer: bad value for keyword %s",
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
 * _load_long - parse a string for a keyword, value pair, and load the
 *	long value
 * IN/OUT destination - location into which result is stored, set to value,
 *	no change if value not found,
 *      set to 1 if keyword found without value,
 *      set to -1 if keyword followed by "unlimited"
 * IN keyword - string to search for
 * IN/OUR in_line - string to search for keyword, the keyword and value
 *	(if present) are overwritten by spaces
 * RET 0 if no error, otherwise an error code
 * NOTE: in_line is overwritten, do not use a constant
 */
static int
_load_long (long *destination, char *keyword, char *in_line)
{
	char scratch[BUFFER_SIZE];	/* scratch area for parsing the input line */
	char *str_ptr1, *str_ptr2, *str_ptr3;
	int i, str_len1, str_len2;

	str_ptr1 = (char *) slurm_strcasestr (in_line, keyword);
	if (str_ptr1 != NULL) {
		str_len1 = strlen (keyword);
		strcpy (scratch, str_ptr1 + str_len1);
		if ((scratch[0] == '\0') ||
		    (isspace ((int) scratch[0]))) {
			/* keyword with no value set */
			*destination = 1;
			str_len2 = 0;
		}
		else {
			str_ptr2 = (char *) strtok_r (scratch, SEPCHARS,
			                              &str_ptr3);
			str_len2 = strlen (str_ptr2);
			if (strcasecmp (str_ptr2, "UNLIMITED") == 0)
				*destination = -1L;
			else if ((str_ptr2[0] == '-') ||
				 ((str_ptr2[0] >= '0') &&
			          (str_ptr2[0] <= '9'))) {
				*destination = strtol (scratch,
				                       (char **) NULL, 10);
			}
			else {
				error ("_load_long: bad value for keyword %s",
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
 * load_string  - parse a string for a keyword, value pair, and load the
 *	char value
 * IN/OUT destination - location into which result is stored, set to value,
 *	no change if value not found, if destination had previous value,
 *	that memory location is automatically freed
 * IN keyword - string to search for
 * IN/OUT in_line - string to search for keyword, the keyword and value
 *	(if present) are overwritten by spaces
 * RET 0 if no error, otherwise an error code
 * NOTE: destination must be free when no longer required
 * NOTE: if destination is non-NULL at function call time, it will be freed
 * NOTE: in_line is overwritten, do not use a constant
 */
int
load_string  (char **destination, char *keyword, char *in_line)
{
	char scratch[BUFFER_SIZE];/* scratch area for parsing the input line */
	char *str_ptr1, *str_ptr2, *str_ptr3;
	int i, str_len1, str_len2;

	str_ptr1 = (char *) slurm_strcasestr (in_line, keyword);
	if (str_ptr1 != NULL) {
		int quoted = 0;
		str_len1 = strlen (keyword);
		if (str_ptr1[str_len1] == '"')
			quoted = 1;
		strcpy (scratch, str_ptr1 + str_len1 + quoted);
		if (quoted)
			str_ptr2 = strtok_r (scratch, "\042\n", &str_ptr3);
		else
			str_ptr2 = strtok_r (scratch, SEPCHARS, &str_ptr3);
		if ((str_ptr2 == NULL) ||
		    ((str_len2 = strlen (str_ptr2))== 0)){
			info ("load_string : keyword %s lacks value",
			      keyword);
			return EINVAL;
		}
		xfree (destination[0]);
		destination[0] = xstrdup(str_ptr2);
		for (i = 0; i < (str_len1 + str_len2 + quoted); i++)
			str_ptr1[i] = ' ';
		if (quoted && (str_ptr1[i] == '"'))
			str_ptr1[i] = ' ';
	}
	return 0;
}
