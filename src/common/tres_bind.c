/*****************************************************************************\
 *  tres_bind.c - Perform TRES binding functions
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Written by Morris Jette
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

#include <limits.h>	/* For LONG_MIN, LONG_MAX */
#include <stdlib.h>

#include "src/common/xstring.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * Test for valid comma-delimited set of numbers
 * RET - -1 on error, else 0
 */
static int _valid_num_list(const char *arg)
{
	char *tmp, *tok, *end_ptr = NULL, *save_ptr = NULL;
	long int val;
	int rc = 0;

	tmp = xstrdup(arg);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		val = strtol(tok, &end_ptr, 0);
		if ((val < 0) || (val == LONG_MAX) ||
		    ((end_ptr[0] != '\0') && (end_ptr[0] != '*'))) {
			rc = -1;
			break;
		}
		if (end_ptr[0] == '*') {
			val = strtol(end_ptr+1, &end_ptr, 0);
			if ((val < 0) || (val == LONG_MAX) ||
			    (end_ptr[0] != '\0')) {
				rc = -1;
				break;
			}
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	return rc;
}

/*
 * Test for valid GPU binding specification
 * RET - -1 on error, else 0
 */
static int _valid_gpu_bind(const char *arg)
{
	if (!strcmp(arg, "closest"))
		return 0;
	if (!strncmp(arg, "map_gpu:", 8))
		return _valid_num_list(arg + 8);
	if (!strncmp(arg, "mask_gpu:", 9))
		return _valid_num_list(arg + 9);
	return -1;
}

/*
 * Verify --tres-bind command line option
 * NOTE: Separate TRES specifications with ";" rather than ","
 *
 * arg IN - Parameter value to check
 * RET - -1 on error, else 0
 *
 * Example: gpu:closest
 *          gpu:map_gpu:0,1
 *          gpu:mask_gpu:0x3,0x3
 *          gpu:map_gpu:0,1;nic:closest
 */
extern int tres_bind_verify_cmdline(const char *arg)
{
	char *sep, *save_ptr = NULL, *tmp, *tok;
	int rc = 0;

	if ((arg == NULL) || (arg[0] == '\0'))
		return 0;

	tmp = xstrdup(arg);
	tok = strtok_r(tmp, ";", &save_ptr);
	while (tok) {
		sep = strchr(tok, ':');		/* Bad format */
		if (!sep) {
			rc = -1;
			break;
		}
		sep[0] = '\0';
		sep++;
		if (!strcmp(tok, "gpu")) {	/* Only support GPUs today */
			if (_valid_gpu_bind(sep) != 0) {
				rc = -1;
				break;
			}
		} else {
			rc = -1;
			break;
		}
		tok = strtok_r(NULL, ";", &save_ptr);
	}
	xfree(tmp);

	return rc;
}
