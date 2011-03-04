/*****************************************************************************\
 *  read_config.c - functions for reading cgroup.conf
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <slurm/slurm_errno.h>

#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/parse_config.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "read_config.h"

slurm_cgroup_conf_t *slurm_cgroup_conf = NULL;

/* Local functions */
static void _clear_slurm_cgroup_conf(void);
static char * _get_conf_path(void);

/*
 * free_slurm_cgroup_conf - free storage associated with the global variable
 *	slurm_cgroup_conf
 */
extern void free_slurm_cgroup_conf(void)
{
	_clear_slurm_cgroup_conf();
	xfree(slurm_cgroup_conf);
}

static void _clear_slurm_cgroup_conf(void)
{
	if (slurm_cgroup_conf) {
		slurm_cgroup_conf->cgroup_automount = false ;
		xfree(slurm_cgroup_conf->cgroup_mount_opts);
		xfree(slurm_cgroup_conf->cgroup_release_agent);
		xfree(slurm_cgroup_conf->user_cgroup_params);
		xfree(slurm_cgroup_conf->job_cgroup_params);
		xfree(slurm_cgroup_conf->jobstep_cgroup_params);
		slurm_cgroup_conf->constrain_ram_space = false ;
		slurm_cgroup_conf->allowed_ram_space = 100 ;
		slurm_cgroup_conf->constrain_swap_space = false ;
		slurm_cgroup_conf->allowed_swap_space = 0 ;
		slurm_cgroup_conf->constrain_cores = false ;
		slurm_cgroup_conf->memlimit_enforcement = 0 ;
		slurm_cgroup_conf->memlimit_threshold = 100 ;
	}
}

/*
 * read_slurm_cgroup_conf - load the Slurm cgroup configuration from the
 *	cgroup.conf file. Store result into global variable slurm_cgroup_conf.
 *	This function can be called more than once.
 * RET SLURM_SUCCESS if no error, otherwise an error code
 */
extern int read_slurm_cgroup_conf(void)
{
	s_p_options_t options[] = {
		{"CgroupAutomount", S_P_BOOLEAN},
		{"CgroupMountOptions", S_P_STRING},
		{"CgroupReleaseAgent", S_P_STRING},
		{"UserCgroupParams", S_P_STRING},
		{"JobCgroupParams", S_P_STRING},
		{"JobStepCgroupParams", S_P_STRING},
		{"ConstrainRAMSpace", S_P_BOOLEAN},
		{"AllowedRAMSpace", S_P_UINT32},
		{"ConstrainSwapSpace", S_P_BOOLEAN},
		{"AllowedSwapSpace", S_P_UINT32},
		{"ConstrainCores", S_P_BOOLEAN},
		{"MemoryLimitEnforcement", S_P_BOOLEAN},
		{"MemoryLimitThreshold", S_P_UINT32},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL;
	struct stat buf;

	/* Set initial values */
	if (slurm_cgroup_conf == NULL) {
		slurm_cgroup_conf = xmalloc(sizeof(slurm_cgroup_conf_t));
	}
	_clear_slurm_cgroup_conf();

	/* Get the slurmdbd.conf path and validate the file */
	conf_path = _get_conf_path();
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		info("No cgroup.conf file (%s)", conf_path);
	} else {
		debug("Reading cgroup.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, NULL, conf_path, false) ==
		    SLURM_ERROR) {
			fatal("Could not open/read/parse cgroup.conf file %s",
			      conf_path);
		}

		/* cgroup initialisation parameters */
		if (!s_p_get_boolean(&slurm_cgroup_conf->cgroup_automount,
				     "CgroupAutomount", tbl))
			slurm_cgroup_conf->cgroup_automount = false;
		s_p_get_string(&slurm_cgroup_conf->cgroup_mount_opts,
			       "CgroupMountOptions", tbl);
		s_p_get_string(&slurm_cgroup_conf->cgroup_release_agent,
			       "CgroupReleaseAgent", tbl);
		if ( ! slurm_cgroup_conf->cgroup_release_agent )
			slurm_cgroup_conf->cgroup_release_agent =
				xstrdup("memory,cpuset");

		/* job and jobsteps cgroup parameters */
		s_p_get_string(&slurm_cgroup_conf->user_cgroup_params,
			       "UserCgroupParams", tbl);
		s_p_get_string(&slurm_cgroup_conf->job_cgroup_params,
			       "JobCgroupParams", tbl);
		s_p_get_string(&slurm_cgroup_conf->jobstep_cgroup_params,
			       "JobStepCgroupParams", tbl);

		/* RAM and Swap constraints related conf items */
		if (!s_p_get_boolean(&slurm_cgroup_conf->constrain_ram_space,
				     "ConstrainRAMSpace", tbl))
			slurm_cgroup_conf->constrain_ram_space = false;
		if (!s_p_get_uint32(&slurm_cgroup_conf->allowed_ram_space,
				    "AllowedRAMSpace", tbl))
			slurm_cgroup_conf->allowed_ram_space = 100;
		if (!s_p_get_boolean(&slurm_cgroup_conf->constrain_swap_space,
				     "ConstrainSwapSpace", tbl))
			slurm_cgroup_conf->constrain_swap_space = false;
		if (!s_p_get_uint32(&slurm_cgroup_conf->allowed_swap_space,
				    "AllowedSwapSpace", tbl))
			slurm_cgroup_conf->allowed_swap_space = 0;

		/* Cores constraints */
		if (!s_p_get_boolean(&slurm_cgroup_conf->constrain_cores,
				     "ConstrainCores", tbl))
			slurm_cgroup_conf->constrain_cores = false;

		/* Memory limits */
		if (!s_p_get_boolean(&slurm_cgroup_conf->memlimit_enforcement,
				     "MemoryLimitEnforcement", tbl))
			slurm_cgroup_conf->memlimit_enforcement = false;
		if (!s_p_get_uint32(&slurm_cgroup_conf->memlimit_threshold,
				    "MemoryLimitThreshold", tbl))
			slurm_cgroup_conf->memlimit_threshold = 0;

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);

	return SLURM_SUCCESS;
}

/* Return the pathname of the cgroup.conf file.
 * xfree() the value returned */
static char * _get_conf_path(void)
{
	char *val = getenv("SLURM_CONF");
	char *path = NULL;
	int i;

	if (!val)
		val = default_slurm_config_file;

	/* Replace file name on end of path */
	i = strlen(val) + 15;
	path = xmalloc(i);
	strcpy(path, val);
	val = strrchr(path, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = path;
	strcpy(val, "cgroup.conf");

	return path;
}
