/*****************************************************************************\
 *  oci_config.c - parse oci.conf configuration file.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
 *  All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include <sys/stat.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/oci_config.h"
#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define OCI_CONF "oci.conf"

static s_p_options_t options[] = {
	{"ContainerPath", S_P_STRING},
	{"CreateEnvFile", S_P_BOOLEAN},
	{"DisableHooks", S_P_STRING},
	{"RunTimeCreate", S_P_STRING},
	{"RunTimeDelete", S_P_STRING},
	{"RunTimeKill", S_P_STRING},
	{"RunTimeQuery", S_P_STRING},
	{"RunTimeRun", S_P_STRING},
	{"RunTimeStart", S_P_STRING},
	{"SrunPath", S_P_STRING},
	{"SrunArgs", S_P_ARRAY},
	{NULL}
};

extern int get_oci_conf(oci_conf_t **oci_ptr)
{
	s_p_hashtbl_t *tbl = NULL;
	struct stat buf;
	int rc = SLURM_SUCCESS;
	oci_conf_t *oci = NULL;
	char *conf_path = get_extra_conf_path(OCI_CONF);
	char *disable_hooks = NULL;
	char **srun_args = NULL;
	int srun_args_count = 0;

	if ((stat(conf_path, &buf) == -1)) {
		error("No %s file", OCI_CONF);
		xfree(conf_path);
		return ENOENT;
	}

	oci = xmalloc(sizeof(*oci));

	debug("Reading %s file %s", OCI_CONF, conf_path);
	tbl = s_p_hashtbl_create(options);
	if (s_p_parse_file(tbl, NULL, conf_path, false, NULL) == SLURM_ERROR)
		fatal("Could not parse %s file: %s", OCI_CONF, conf_path);

	(void) s_p_get_string(&oci->container_path, "ContainerPath", tbl);
	(void) s_p_get_boolean(&oci->create_env_file, "CreateEnvFile", tbl);
	(void) s_p_get_string(&disable_hooks, "DisableHooks", tbl);
	(void) s_p_get_string(&oci->runtime_create, "RunTimeCreate", tbl);
	(void) s_p_get_string(&oci->runtime_delete, "RunTimeDelete", tbl);
	(void) s_p_get_string(&oci->runtime_kill, "RunTimeKill", tbl);
	(void) s_p_get_string(&oci->runtime_query, "RunTimeQuery", tbl);
	(void) s_p_get_string(&oci->runtime_run, "RunTimeRun", tbl);
	(void) s_p_get_string(&oci->runtime_start, "RunTimeStart", tbl);
	(void) s_p_get_string(&oci->srun_path, "SrunPath", tbl);
	(void) s_p_get_array((void ***) &srun_args, &srun_args_count,
			     "SrunArgs", tbl);

	if (srun_args_count) {
		oci->srun_args = xcalloc((srun_args_count + 1),
					 sizeof(*oci->srun_args));

		for (int i = 0; i < srun_args_count; i++)
			oci->srun_args[i] = xstrdup(srun_args[i]);
	}

	if (disable_hooks) {
		char *ptr1 = NULL, *ptr2 = NULL;
		int i = 0, size = 1;

		oci->disable_hooks = xcalloc(size + 1,
					     sizeof(*oci->disable_hooks));

		ptr1 = strtok_r(disable_hooks, ",", &ptr2);
		while (ptr1) {
			if (i > size) {
				size++;
				xrecalloc(oci->disable_hooks, size + 1,
					  sizeof(*oci->disable_hooks));
			}

			oci->disable_hooks[i] = xstrdup(ptr1);
			debug("%s: disable hook type %s",
			      __func__, oci->disable_hooks[i]);
			ptr1 = strtok_r(NULL, ",", &ptr2);
		}

		xfree(disable_hooks);
	}

	if (!oci->runtime_create && !oci->runtime_delete &&
	    !oci->runtime_kill && !oci->runtime_query &&
	    !oci->runtime_run && !oci->runtime_start) {
		error("oci.conf present but missing required options. Rejecting invalid configuration.");
		rc = EINVAL;
	} else if (oci->runtime_create && oci->runtime_delete &&
		   oci->runtime_kill && oci->runtime_query &&
		   !oci->runtime_run && oci->runtime_start) {
		debug("OCI container activated with create/start");
	} else if (!oci->runtime_create && oci->runtime_delete &&
		   oci->runtime_kill && oci->runtime_query &&
		   oci->runtime_run && !oci->runtime_start) {
		debug("OCI container activated with run");
	} else {
		error("RunTimeRun and RunCreate/RunTimeStart are mutually exclusive. All other RunTime* configurations items must be populated.");
		rc = SLURM_ERROR;
	}

	s_p_hashtbl_destroy(tbl);
	xfree(conf_path);

	if (!rc) {
		free_oci_conf(*oci_ptr);
		*oci_ptr = oci;
	} else {
		free_oci_conf(oci);
	}

	return rc;
}

extern void free_oci_conf(oci_conf_t *oci)
{
	if (!oci)
		return;

	xfree(oci->container_path);
	xfree(oci->runtime_create);
	xfree(oci->runtime_delete);
	xfree(oci->runtime_kill);
	xfree(oci->runtime_query);
	xfree(oci->runtime_run);
	xfree(oci->runtime_start);
	xfree(oci->srun_path);
	for (int i = 0; oci->srun_args && oci->srun_args[i]; i++)
		xfree(oci->srun_args[i]);
	xfree(oci->srun_args);

	if (oci->disable_hooks) {
		for (int i = 0; oci->disable_hooks[i]; i++)
			xfree(oci->disable_hooks[i]);
		xfree(oci->disable_hooks);
	}

	xfree(oci);
}
