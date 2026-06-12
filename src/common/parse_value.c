/*****************************************************************************\
 *  parse_value.c - helper functions to simplify typed values management in
 *                  Slurm parser (see parse_config.{h,c})
 *****************************************************************************
 *  Initially written by Francois Chevallier <chevallierfrancois@free.fr> @ BULL
 *  for slurm-2.6. Adapted by Matthieu Hautreux <matthieu.hautreux@cea.fr>, CEA,
 *  for slurm-14.11.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef   _ISOC99_SOURCE
#  define _ISOC99_SOURCE /* strtof() */
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/parse_value.h"

#include "slurm/slurm.h"

int s_p_handle_long(long* data, const char* key, const char* value)
{
	char *endptr;
	long num;
	errno = 0;
	num = strtol(value, &endptr, 0);
	if ((num == 0 && errno == EINVAL)
		|| (*endptr != '\0')) {
		if (xstrcasecmp(value, "UNLIMITED") == 0
			|| xstrcasecmp(value, "INFINITE") == 0) {
			num = (long) INFINITE;
		} else {
			error("\"%s\" is not a valid number", value);
			return SLURM_ERROR;
		}
	} else if (errno == ERANGE) {
		error("\"%s\" is out of range", value);
		return SLURM_ERROR;
	}
	*data = num;
	return SLURM_SUCCESS;
}

int s_p_handle_uint16(uint16_t* data, const char* key, const char *value)
{
	char *endptr;
	unsigned long num;

	errno = 0;
	num = strtoul(value, &endptr, 0);
	if ((num == 0 && errno == EINVAL)
		|| (*endptr != '\0')) {
		if (xstrcasecmp(value, "UNLIMITED") == 0
			|| xstrcasecmp(value, "INFINITE") == 0) {
			num = INFINITE16;
		} else {
			error("%s value \"%s\" is not a valid number",
				key, value);
			return SLURM_ERROR;
		}
	} else if (errno == ERANGE) {
		error("%s value (%s) is out of range", key, value);
		return SLURM_ERROR;
	} else if (value[0] == '-') {
		error("%s value (%s) is less than zero", key,
			value);
		return SLURM_ERROR;
	} else if (num > 0xffff) {
		error("%s value (%s) is greater than 65535", key,
			value);
		return SLURM_ERROR;
	}
	*data = (uint16_t)num;
	return SLURM_SUCCESS;
}

int s_p_handle_uint32(uint32_t* data, const char* key, const char* value)
{
	char *endptr;
	unsigned long num;

	errno = 0;
	num = strtoul(value, &endptr, 0);
	if ((endptr[0] == 'k') || (endptr[0] == 'K')) {
		num *= 1024;
		endptr++;
	}
	if ((num == 0 && errno == EINVAL)
		|| (*endptr != '\0')) {
		if ((xstrcasecmp(value, "UNLIMITED") == 0) ||
			(xstrcasecmp(value, "INFINITE")  == 0)) {
			num = INFINITE;
		} else {
			error("%s value (%s) is not a valid number",
				key, value);
			return SLURM_ERROR;
		}
	} else if (errno == ERANGE) {
		error("%s value (%s) is out of range", key, value);
		return SLURM_ERROR;
	} else if (value[0] == '-') {
		error("%s value (%s) is less than zero", key,
			value);
		return SLURM_ERROR;
	} else if (num > 0xffffffff) {
		error("%s value (%s) is greater than 4294967295",
			key, value);
		return SLURM_ERROR;
	}
	*data = (uint32_t)num;
	return SLURM_SUCCESS;
}

int s_p_handle_uint64(uint64_t* data, const char* key, const char* value)
{
	char *endptr;
	unsigned long long num;

	errno = 0;
	num = strtoull(value, &endptr, 0);
	if ((endptr[0] == 'k') || (endptr[0] == 'K')) {
		num *= 1024;
		endptr++;
	}
	if ((num == 0 && errno == EINVAL)
		|| (*endptr != '\0')) {
		if ((xstrcasecmp(value, "UNLIMITED") == 0) ||
			(xstrcasecmp(value, "INFINITE")  == 0)) {
			num = INFINITE64;
		} else {
			error("%s value (%s) is not a valid number",
				key, value);
			return SLURM_ERROR;
		}
	} else if (errno == ERANGE) {
		error("%s value (%s) is out of range", key, value);
		return SLURM_ERROR;
	} else if (value[0] == '-') {
		error("%s value (%s) is less than zero", key,
			value);
		return SLURM_ERROR;
	} else if (num > 0xffffffffffffffff) {
		error("%s value (%s) is greater than 4294967295",
			key, value);
		return SLURM_ERROR;
	}
	*data = (uint64_t)num;
	return SLURM_SUCCESS;
}

int s_p_handle_boolean(bool* data, const char* key, const char* value)
{
	bool flag;

	if (!xstrcasecmp(value, "yes")
		|| !xstrcasecmp(value, "up")
		|| !xstrcasecmp(value, "true")
		|| !xstrcasecmp(value, "1")) {
		flag = true;
	} else if (!xstrcasecmp(value, "no")
		   || !xstrcasecmp(value, "down")
		   || !xstrcasecmp(value, "false")
		   || !xstrcasecmp(value, "0")) {
		flag = false;
	} else {
		error("\"%s\" is not a valid option for \"%s\"",
			value, key);
		return SLURM_ERROR;
	}

	*data = flag;
	return SLURM_SUCCESS;
}

int s_p_handle_float(float* data, const char* key, const char* value)
{
	char *endptr;
	float num;

	errno = 0;
	num = strtof(value, &endptr);
	if ((num == 0 && errno == EINVAL)
		|| (*endptr != '\0')) {
		if ((xstrcasecmp(value, "UNLIMITED") == 0) ||
			(xstrcasecmp(value, "INFINITE")  == 0)) {
			num = INFINITY;
		} else {
			error("%s value (%s) is not a valid number",
				key, value);
			return SLURM_ERROR;
		}
	} else if (errno == ERANGE) {
		error("%s value (%s) is out of range", key, value);
		return SLURM_ERROR;
	}
	*data = num;
	return SLURM_SUCCESS;
}

int s_p_handle_double(double* data, const char* key, const char* value)
{
	char *endptr;
	double num;

	errno = 0;
	num = strtod(value, &endptr);
	if ((num == 0 && errno == EINVAL)
		|| (*endptr != '\0')) {
		if ((xstrcasecmp(value, "UNLIMITED") == 0) ||
			(xstrcasecmp(value, "INFINITE")  == 0)) {
			num = HUGE_VAL;
		} else {
			error("%s value (%s) is not a valid number",
				key, value);
			return SLURM_ERROR;
		}
	} else if (errno == ERANGE) {
		error("%s value (%s) is out of range", key, value);
		return SLURM_ERROR;
	}
	*data = num;
	return SLURM_SUCCESS;
}

int s_p_handle_long_double(long double* data, const char* key,
			   const char* value)
{
	char *endptr;
	long double num;

	errno = 0;
	num = strtold(value, &endptr);
	if ((num == 0 && errno == EINVAL)
		|| (*endptr != '\0')) {
		if ((xstrcasecmp(value, "UNLIMITED") == 0) ||
			(xstrcasecmp(value, "INFINITE")  == 0)) {
			num = HUGE_VALL;
		} else {
			error("%s value (%s) is not a valid number",
				key, value);
			return SLURM_ERROR;
		}
	} else if (errno == ERANGE) {
		error("%s value (%s) is out of range", key, value);
		return SLURM_ERROR;
	}
	*data = num;
	return SLURM_SUCCESS;
}
