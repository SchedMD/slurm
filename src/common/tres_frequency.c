/*****************************************************************************\
 *  tres_frequency.c - Perform TRES frequency control functions
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
 * check if TRES frequency setting is allowed on this node
 * if so, create and initialize appropriate data structures
 */
extern void tres_freq_init(slurmd_conf_t *conf)
{
//FIXME - To do
}

/*
 * free memory from TRES frequency data structures
 */
extern void tres_freq_fini(void)
{
//FIXME - To do
}

/*
 * reset debug flag (slurmd)
 */
extern void tres_freq_reconfig(void)
{
//FIXME - To do
}

/*
 * Send the tres_frequency info to slurmstepd
 */
extern void tres_freq_send_info(int fd)
{
//FIXME - To do
}

/*
 * Receive the tres_frequency table info from slurmd
 */
extern void tres_freq_recv_info(int fd)
{
//FIXME - To do
}

/*
 * Validate the TRES frequency to set
 * Called from task cpuset code
 */
extern void tres_freq_cpuset_validate(stepd_step_rec_t *job)
{
//FIXME - To do
}

/*
 * Validate the TRES and select the frequency to set
 * Called from task cgroup code
 */
extern void tres_freq_cgroup_validate(stepd_step_rec_t *job,
				      char *step_alloc_cores)
{
//FIXME - To do
}

/*
 * Verify slurm.conf TresFreqDef option
 *
 * arg IN - Parameter value to check
 * RET - -1 on error, else 0
 */
extern int tres_freq_verify_def(const char *arg)
{
//FIXME - To do
	return 0;
}

/*
 * Test for valid number or name (e.g. high, medium, low, etc.)
 * RET - -1 on error, else 0
 */
static int _test_val(const char *arg)
{
	char *end_ptr = NULL;
	long int val;
	int rc = 0;

	if ((arg == NULL) || (arg[0] == '\0'))
		return -1;

	if ((arg[0] >= '0') && (arg[0] <= '9')) {
		val = strtol(arg, &end_ptr, 10);
		if ((val == LONG_MAX) || (val < 0) || (end_ptr[0] != '\0'))
			rc = -1;
	} else if (strcmp(arg, "low")  && strcmp(arg, "medium") &&
		   strcmp(arg, "high") && strcmp(arg, "highm1")) {
		rc = -1;
	}

	return rc;
}

/*
 * Test for valid GPU frequency specification
 * RET - -1 on error, else 0
 */
static int _valid_gpu_freq(const char *arg)
{
	char *eq, *save_ptr = NULL, *tmp, *tok;
	int rc = 0;

	if ((arg == NULL) || (arg[0] == '\0'))
		return -1;

	tmp = xstrdup(arg);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		eq = strchr(tok, '=');
		if (!eq) {
			rc = _test_val(tok);
			if ((rc != 0) && !strcmp(tok, "verbose"))
				rc = 0;	/* "verbose" is undocumented option */
		} else {
			eq[0] = '\0';
			if (!strcmp(tok, "memory")) {
				rc = _test_val(eq + 1);
			} else {
				rc = -1;
			}
		}
		if (rc != 0)
			break;
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	return rc;
}

/*
 * Verify --tres-freq command line option
 *
 * arg IN - Parameter value to check
 * RET - -1 on error, else 0
 *
 * Example: gpu:medium,memory=high
 *          gpu:450
 */
extern int tres_freq_verify_cmdline(const char *arg)
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
			if (_valid_gpu_freq(sep) != 0) {
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

/*
 * Set environment variables associated with TRES frequency variables.
 */
extern int tres_freq_set_env(char *var)
{
//FIXME - To do
	return 0;
}

/*
 * set TRES frequency values
 */
extern void tres_freq_set(stepd_step_rec_t *job)
{
//FIXME - To do
}

/*
 * reset TRES frequency values after suspend/resume
 */
extern void tres_freq_reset(stepd_step_rec_t *job)
{
//FIXME - To do
}
