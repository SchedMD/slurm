/*****************************************************************************\
 *  argv.c  -
 *****************************************************************************
 *  Copyright (C) 2012-2013 Los Alamos National Security, LLC.
 *  Written by Jimmy Cao <Jimmy.Cao@emc.com>, Ralph Castain <rhc@open-mpi.org>
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"

#include "argv.h"
#include "constants.h"

/*
 * Append a string to the end of a new or existing argv array.
 */
int argv_append(int *argc, char ***argv, const char *arg)
{
	int rc;

	/* add the new element */
	if (SLURM_SUCCESS != (rc = argv_append_nosize(argv, arg))) {
		return rc;
	}

	*argc = argv_count(*argv);

	return SLURM_SUCCESS;
}

int argv_append_nosize(char ***argv, const char *arg)
{
	int argc;

	/* Create new argv. */
	if (NULL == *argv) {
		*argv = (char**) malloc(2 * sizeof(char *));
		if (NULL == *argv) {
			return SLURM_FAILURE;
		}
		argc = 0;
		(*argv)[0] = NULL;
		(*argv)[1] = NULL;
	}

	/* Extend existing argv. */
	else {
		/* count how many entries currently exist */
		argc = argv_count(*argv);

		*argv = (char**) realloc(*argv, (argc + 2) * sizeof(char *));
		if (NULL == *argv) {
			return SLURM_FAILURE;
		}
	}

	/* Set the newest element to point to a copy of the arg string */
	(*argv)[argc] = strdup(arg);
	if (NULL == (*argv)[argc]) {
		return SLURM_FAILURE;
	}

	argc = argc + 1;
	(*argv)[argc] = NULL;

	return SLURM_SUCCESS;
}

int argv_prepend_nosize(char ***argv, const char *arg)
{
	int argc;
	int i;

	/* Create new argv. */
	if (NULL == *argv) {
		*argv = (char**) malloc(2 * sizeof(char *));
		if (NULL == *argv) {
			return SLURM_FAILURE;
		}
		(*argv)[0] = strdup(arg);
		(*argv)[1] = NULL;
	} else {
		/* count how many entries currently exist */
		argc = argv_count(*argv);

		*argv = (char**) realloc(*argv, (argc + 2) * sizeof(char *));
		if (NULL == *argv) {
			return SLURM_FAILURE;
		}
		(*argv)[argc+1] = NULL;

		/* shift all existing elements down 1 */
		for (i=argc; 0 < i; i--) {
			(*argv)[i] = (*argv)[i-1];
		}
		(*argv)[0] = strdup(arg);
	}

	return SLURM_SUCCESS;
}

int argv_append_unique_nosize(char ***argv, const char *arg, bool overwrite)
{
	int i;

	/* if the provided array is NULL, then the arg cannot be present,
	 * so just go ahead and append
	 */
	if (NULL == *argv) {
		return argv_append_nosize(argv, arg);
	}

	/* see if this arg is already present in the array */
	for (i=0; NULL != (*argv)[i]; i++) {
		if (0 == strcmp(arg, (*argv)[i])) {
			/* already exists - are we authorized to overwrite? */
			if (overwrite) {
				free((*argv)[i]);
				(*argv)[i] = strdup(arg);
			}
			return SLURM_SUCCESS;
		}
	}

	/* we get here if the arg is not in the array - so add it */
	return argv_append_nosize(argv, arg);
}

/*
 * Free a NULL-terminated argv array.
 */
void argv_free(char **argv)
{
	char **p;

	if (NULL == argv)
		return;

	for (p = argv; NULL != *p; ++p) {
		free(*p);
	}

	free(argv);
}


/*
 * Split a string into a NULL-terminated argv array.
 */
static char **argv_split_inter(const char *src_string, int delimiter,
		int include_empty)
{
	char arg[SIZE];
	char **argv = NULL;
	const char *p;
	char *argtemp;
	int argc = 0;
	size_t arglen;

	while (src_string && *src_string) {
		p = src_string;
		arglen = 0;

		while (('\0' != *p) && (*p != delimiter)) {
			++p;
			++arglen;
		}

		/* zero length argument, skip */
		if (src_string == p) {
			if (include_empty) {
				arg[0] = '\0';
				if (SLURM_SUCCESS != argv_append(&argc, &argv, arg))
					return NULL;
			}
		}

		/* tail argument, add straight from the original string */
		else if ('\0' == *p) {
			if (SLURM_SUCCESS != argv_append(&argc, &argv, src_string))
				return NULL;
			src_string = p;
			continue;
		}

		/* long argument, malloc buffer, copy and add */
		else if (arglen > (SIZE - 1)) {
			argtemp = (char*) malloc(arglen + 1);
			if (NULL == argtemp)
				return NULL;

			strncpy(argtemp, src_string, arglen);
			argtemp[arglen] = '\0';

			if (SLURM_SUCCESS != argv_append(&argc, &argv, argtemp)) {
				free(argtemp);
				return NULL;
			}

			free(argtemp);
		}

		/* short argument, copy to buffer and add */
		else {
			strncpy(arg, src_string, arglen);
			arg[arglen] = '\0';

			if (SLURM_SUCCESS != argv_append(&argc, &argv, arg))
				return NULL;
		}

		src_string = p + 1;
	}

	/* All done */
	return argv;
}

char **argv_split(const char *src_string, int delimiter)
{
	return argv_split_inter(src_string, delimiter, 0);
}

char **argv_split_with_empty(const char *src_string, int delimiter)
{
	return argv_split_inter(src_string, delimiter, 1);
}

/*
 * Return the length of a NULL-terminated argv array.
 */
int argv_count(char **argv)
{
	char **p;
	int i;

	if (NULL == argv)
		return 0;

	for (i = 0, p = argv; *p; i++, p++)
		continue;

	return i;
}

/*
 * Join all the elements of an argv array into a single
 * newly-allocated string.
 */
char *argv_join(char **argv, int delimiter)
{
	char **p;
	char *pp;
	char *str;
	size_t str_len = 0;
	size_t i;

	/* Bozo case */
	if (NULL == argv || NULL == argv[0]) {
		return strdup("");
	}

	/* Find the total string length in argv including delimiters.  The
     	 last delimiter is replaced by the NULL character. */
	for (p = argv; *p; ++p) {
		str_len += strlen(*p) + 1;
	}

	/* Allocate the string. */
	if (NULL == (str = (char*) malloc(str_len)))
		return NULL;

	/* Loop filling in the string. */
	str[--str_len] = '\0';
	p = argv;
	pp = *p;

	for (i = 0; i < str_len; ++i) {
		if ('\0' == *pp) {
			/* End of a string, fill in a delimiter
			 * and go to the next string. */
			str[i] = (char) delimiter;
			++p;
			pp = *p;
		} else {
			str[i] = *pp++;
		}
	}

	/* All done */
	return str;
}

/*
 * Join all the elements of an argv array from within a
 * specified range into a single newly-allocated string.
 */
char *argv_join_range(char **argv, size_t start, size_t end, int delimiter)
{
	char **p;
	char *pp;
	char *str;
	size_t str_len = 0;
	size_t i;

	/* Bozo case */
	if (NULL == argv || NULL == argv[0] || (int)start > argv_count(argv)) {
		return strdup("");
	}

	/* Find the total string length in argv including delimiters.  The
	 * last delimiter is replaced by the NULL character. */
	for (p = &argv[start], i=start; *p && i < end; ++p, ++i) {
		str_len += strlen(*p) + 1;
	}

	/* Allocate the string. */
	if (NULL == (str = (char*) malloc(str_len)))
		return NULL;

	/* Loop filling in the string. */
	str[--str_len] = '\0';
	p = &argv[start];
	pp = *p;

	for (i = 0; i < str_len; ++i) {
		if ('\0' == *pp) {
			/* End of a string, fill in a delimiter and go to the
			 * next string. */
			str[i] = (char) delimiter;
			++p;
			pp = *p;
		} else {
			str[i] = *pp++;
		}
	}

	/* All done */
	return str;
}

/*
 * Return the number of bytes consumed by an argv array.
 */
size_t argv_len(char **argv)
{
	char **p;
	size_t length;

	if (NULL == argv)
		return (size_t) 0;

	length = sizeof(char *);

	for (p = argv; *p; ++p) {
		length += strlen(*p) + 1 + sizeof(char *);
	}

	return length;
}

/*
 * Copy a NULL-terminated argv array.
 */
char **argv_copy(char **argv)
{
	char **dupv = NULL;
	int dupc = 0;

	if (NULL == argv)
		return NULL;

	/* create an "empty" list, so that we return something valid if we
	 * were passed a valid list with no contained elements */
	dupv = (char**) malloc(sizeof(char*));
	dupv[0] = NULL;

	while (NULL != *argv) {
		if (SLURM_SUCCESS != argv_append(&dupc, &dupv, *argv)) {
			argv_free(dupv);
			return NULL;
		}

		++argv;
	}

	/* All done */
	return dupv;
}

int argv_delete(int *argc, char ***argv, int start, int num_to_delete)
{
	int i;
	int count;
	int suffix_count;
	char **tmp;

	/* Check for the bozo cases */
	if (NULL == argv || NULL == *argv || 0 == num_to_delete) {
		return SLURM_SUCCESS;
	}
	count = argv_count(*argv);
	if (start > count) {
		return SLURM_SUCCESS;
	} else if (start < 0 || num_to_delete < 0) {
		return SLURM_FAILURE;
	}

	/* Ok, we have some tokens to delete.  Calculate the new length of
	 * the argv array. */
	suffix_count = count - (start + num_to_delete);
	if (suffix_count < 0) {
		suffix_count = 0;
	}

	/* Free all items that are being deleted */
	for (i = start; i < count && i < start + num_to_delete; ++i) {
		free((*argv)[i]);
	}

	/* Copy the suffix over the deleted items */
	for (i = start; i < start + suffix_count; ++i) {
		(*argv)[i] = (*argv)[i + num_to_delete];
	}

	/* Add the trailing NULL */
	(*argv)[i] = NULL;

	/* adjust the argv array */
	tmp = (char**)realloc(*argv, sizeof(char**) * (i + 1));
	if (NULL != tmp) *argv = tmp;

	/* adjust the argc */
	(*argc) -= num_to_delete;

	return SLURM_SUCCESS;
}

int argv_insert(char ***target, int start, char **source)
{
	int i, source_count, target_count;
	int suffix_count;

	/* Check for the bozo cases */
	if (NULL == target || NULL == *target || start < 0) {
		return SLURM_FAILURE;
	} else if (NULL == source) {
		return SLURM_SUCCESS;
	}

	/* Easy case: appending to the end */
	target_count = argv_count(*target);
	source_count = argv_count(source);
	if (start > target_count) {
		for (i = 0; i < source_count; ++i) {
			argv_append(&target_count, target, source[i]);
		}
	}

	/* Harder: insertting into the middle */
	else {
		/* Alloc out new space */
		*target = (char**) realloc(*target,
					   sizeof(char *) *
					   (target_count + source_count + 1));

		/* Move suffix items down to the end */
		suffix_count = target_count - start;
		for (i = suffix_count - 1; i >= 0; --i) {
			(*target)[start + source_count + i] =
					(*target)[start + i];
		}
		(*target)[start + suffix_count + source_count] = NULL;

		/* Strdup in the source argv */
		for (i = start; i < start + source_count; ++i) {
			(*target)[i] = strdup(source[i - start]);
		}
	}

	/* All done */
	return SLURM_SUCCESS;
}

int argv_insert_element(char ***target, int location, char *source)
{
	int i, target_count;
	int suffix_count;

	/* Check for the bozo cases */
	if (NULL == target || NULL == *target || location < 0) {
		return SLURM_FAILURE;
	} else if (NULL == source) {
		return SLURM_SUCCESS;
	}

	/* Easy case: appending to the end */
	target_count = argv_count(*target);
	if (location > target_count) {
		argv_append(&target_count, target, source);
		return SLURM_SUCCESS;
	}

	/* Alloc out new space */
	*target = (char**) realloc(*target,
			sizeof(char*) * (target_count + 2));

	/* Move suffix items down to the end */
	suffix_count = target_count - location;
	for (i = suffix_count - 1; i >= 0; --i) {
		(*target)[location + 1 + i] =
				(*target)[location + i];
	}
	(*target)[location + suffix_count + 1] = NULL;

	/* Strdup in the source */
	(*target)[location] = strdup(source);

	/* All done */
	return SLURM_SUCCESS;
}
