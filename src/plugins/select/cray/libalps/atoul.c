/*
 * String utilities
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#define _ISOC99_SOURCE		/* for LLONG_{MIN,MAX} */
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/**
 * atou64 - Convert string into u64
 * Returns 1 if ok, < 0 on error.
 */
int atou64(const char *str, uint64_t *val)
{
	char *endptr;

	errno = 0;				/* strtol() manpage */
	*val  = strtoull(str, &endptr, 0);
	if ((errno == ERANGE && *val == ULLONG_MAX) ||
	    (errno != 0 && *val == 0)	||	/* other error */
	    endptr == str		||	/* no digits */
	    *endptr != '\0')			/* junk at end */
		return -1;
	return 1;
}

int atou32(const char *str, uint32_t *val)
{
	uint64_t tmp;

	if (!atou64(str, &tmp) || tmp > 0xFFFFffffUL)
		return -1;
	*val = tmp;
	return 1;
}
