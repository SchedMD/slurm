/* $Id$ */

/* slurm_parser() functionality - split out from bits_bytes.c  and
 * moved into slurmctld/ srcdir
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "log.h"
#include "xmalloc.h"

#define BUF_SIZE 1024
#define SEPCHARS " \n\t"

/* 
 * slurm_parser - parse the supplied specification into keyword/value pairs
 *	only the keywords supplied will be searched for. the supplied specification
 *	is altered, overwriting the keyword and value pairs with spaces.
 * input: spec - pointer to the string of specifications
 *	sets of three values (as many sets as required): keyword, type, value 
 *	keyword - string with the keyword to search for including equal sign 
 *		(e.g. "name=")
 *	type - char with value 'd' for int, 'f' for float, 's' for string
 *	value - pointer to storage location for value (char **) for type 's'
 * output: spec - everything read is overwritten by speces
 *	value - set to read value (unchanged if keyword not found)
 *	return - 0 if no error, otherwise errno code
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
			error_code = load_integer(int_ptr, keyword, spec);
			break;
		case 'f':
			float_ptr = va_arg(ap, float *);
			error_code = load_float(float_ptr, keyword, spec);
			break;
		case 's':
			str_ptr = va_arg(ap, char **);
			error_code = load_string(str_ptr, keyword, spec);
			break;
		default:
			fatal ("parse_spec: invalid type %c", type);
		}
	}
	va_end(ap);
	return error_code;
}


/*
 * load_float - location into which result is stored
 *        keyword - string to search for
 *        in_line - string to search for keyword
 * output: *destination - set to value, no change if value not found
 *         in_line - the keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: in_line is overwritten, do not use a constant
 */
int 
load_float (float *destination, char *keyword, char *in_line) 
{
	char scratch[BUF_SIZE];	/* scratch area for parsing the input line */
	char *str_ptr1, *str_ptr2, *str_ptr3;
	int i, str_len1, str_len2;

	str_ptr1 = (char *) strstr (in_line, keyword);
	if (str_ptr1 != NULL) {
		str_len1 = strlen (keyword);
		strcpy (scratch, str_ptr1 + str_len1);
		if ((scratch[0] < '0') && (scratch[0] > '9')) {
			error ("load_float: bad value for keyword %s\n", keyword);
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
int 
load_integer (int *destination, char *keyword, char *in_line) 
{
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
int 
load_string (char **destination, char *keyword, char *in_line) 
{
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


