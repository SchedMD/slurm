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
	{"CreateEnvFile", S_P_STRING},
	{"DisableHooks", S_P_STRING},
	{"EnvExclude", S_P_STRING},
	{"MountSpoolDir", S_P_STRING},
	{"RunTimeCreate", S_P_STRING},
	{"RunTimeDelete", S_P_STRING},
	{"RunTimeKill", S_P_STRING},
	{"RunTimeEnvExclude", S_P_STRING},
	{"RunTimeQuery", S_P_STRING},
	{"RunTimeRun", S_P_STRING},
	{"RunTimeStart", S_P_STRING},
	{"SrunPath", S_P_STRING},
	{"SrunArgs", S_P_ARRAY},
	{"DisableCleanup", S_P_BOOLEAN},
	{"StdIODebug", S_P_STRING},
	{"SyslogDebug", S_P_STRING},
	{"FileDebug", S_P_STRING},
	{"DebugFlags", S_P_STRING},
	{"IgnoreFileConfigJson", S_P_BOOLEAN},
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
	char *debug_stdio = NULL, *debug_syslog = NULL;
	char *debug_flags = NULL, *debug_file = NULL;
	char *create_env_file = NULL;
	char *runtime_env_exclude = NULL, *env_exclude = NULL;

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
	(void) s_p_get_string(&create_env_file, "CreateEnvFile", tbl);
	(void) s_p_get_string(&disable_hooks, "DisableHooks", tbl);
	(void) s_p_get_boolean(&oci->ignore_config_json, "IgnoreFileConfigJson", tbl);
	(void) s_p_get_string(&env_exclude, "EnvExclude", tbl);
	(void) s_p_get_string(&oci->mount_spool_dir, "MountSpoolDir", tbl);
	(void) s_p_get_string(&oci->runtime_create, "RunTimeCreate", tbl);
	(void) s_p_get_string(&oci->runtime_delete, "RunTimeDelete", tbl);
	(void) s_p_get_string(&oci->runtime_kill, "RunTimeKill", tbl);
	(void) s_p_get_string(&runtime_env_exclude, "RunTimeEnvExclude", tbl);
	(void) s_p_get_string(&oci->runtime_query, "RunTimeQuery", tbl);
	(void) s_p_get_string(&oci->runtime_run, "RunTimeRun", tbl);
	(void) s_p_get_string(&oci->runtime_start, "RunTimeStart", tbl);
	(void) s_p_get_string(&oci->srun_path, "SrunPath", tbl);
	(void) s_p_get_array((void ***) &srun_args, &srun_args_count,
			     "SrunArgs", tbl);
	(void) s_p_get_boolean(&oci->disable_cleanup, "DisableCleanup", tbl);
	(void) s_p_get_string(&debug_stdio, "StdIODebug", tbl);
	(void) s_p_get_string(&debug_syslog, "SyslogDebug", tbl);
	(void) s_p_get_string(&debug_file, "FileDebug", tbl);
	(void) s_p_get_string(&debug_flags, "DebugFlags", tbl);

	if (debug_stdio) {
		oci->stdio_log_level = log_string2num(debug_stdio);
		xfree(debug_stdio);
	}
	if (debug_syslog) {
		oci->syslog_log_level = log_string2num(debug_syslog);
		xfree(debug_syslog);
	}
	if (debug_file) {
		oci->file_log_level = log_string2num(debug_file);
		xfree(debug_file);
	}
	if (debug_flags) {
		int rc;

		if ((rc = debug_str2flags(debug_flags, &oci->debug_flags)))
			fatal("%s: unable to parse oci.conf debugflags=%s: %m",
			      __func__, debug_flags);

		xfree(debug_flags);
	}

	if (srun_args_count) {
		oci->srun_args = xcalloc((srun_args_count + 1),
					 sizeof(*oci->srun_args));

		for (int i = 0; i < srun_args_count; i++)
			oci->srun_args[i] = xstrdup(srun_args[i]);
	}

	if (disable_hooks) {
		char *ptr1 = NULL, *ptr2 = NULL;
		int i = 0;

		/* NULL terminated array of char* */
		oci->disable_hooks = xmalloc(sizeof(*oci->disable_hooks));

		ptr1 = strtok_r(disable_hooks, ",", &ptr2);
		while (ptr1) {
			i++;

			xrecalloc(oci->disable_hooks, (i + 1),
				  sizeof(*oci->disable_hooks));

			oci->disable_hooks[i] = xstrdup(ptr1);
			debug("%s: disable hook type %s",
			      __func__, oci->disable_hooks[i]);
			ptr1 = strtok_r(NULL, ",", &ptr2);
		}

		xfree(disable_hooks);
	}

	if (oci->ignore_config_json) {
		/*
		 * If site has enabled ignore config.json then sanity checking
		 * the runtime commands is not required as the site is faking an
		 * OCI runtime any way.
		 */
		debug("OCI container activated with IgnoreFileConfigJson=True");
	} else if (!oci->runtime_create && !oci->runtime_delete &&
		   !oci->runtime_kill && !oci->runtime_query &&
		   !oci->runtime_run && !oci->runtime_start) {
		error("oci.conf present but missing required options. Rejecting invalid configuration.");
		rc = EINVAL;
	} else if (oci->runtime_create && oci->runtime_delete &&
		   oci->runtime_kill && oci->runtime_query &&
		   !oci->runtime_run && oci->runtime_start) {
		debug("OCI container activated with create/start");
	} else if (!oci->runtime_create && oci->runtime_delete &&
		   oci->runtime_kill && oci->runtime_run &&
		   !oci->runtime_start) {
		debug("OCI container activated with run");
	} else {
		error("RunTimeRun and RunCreate/RunTimeStart are mutually exclusive. All other RunTime* configurations items must be populated.");
		rc = SLURM_ERROR;
	}

	if (!xstrcasecmp(create_env_file, "null") ||
	    !xstrcasecmp(create_env_file, "true") ||
	    !xstrcasecmp(create_env_file, "Y") ||
	    !xstrcasecmp(create_env_file, "Yes") ||
	    !xstrcasecmp(create_env_file, "1")) {
		oci->create_env_file = NULL_TERMINATED_ENV_FILE;
	} else if (!xstrcasecmp(create_env_file, "newline")) {
		oci->create_env_file = NEWLINE_TERMINATED_ENV_FILE;
	} else if (!create_env_file || !xstrcasecmp(create_env_file, "false") ||
		 !xstrcasecmp(create_env_file, "disabled") ||
		 !xstrcasecmp(create_env_file, "N") ||
		 !xstrcasecmp(create_env_file, "No") ||
		 !xstrcasecmp(create_env_file, "0")) {
		oci->create_env_file = DISABLED_ENV_FILE;
	} else {
		error("Invalid value of CreateEnvFile=%s", create_env_file);
		rc = SLURM_ERROR;
	}

	xfree(create_env_file);
	s_p_hashtbl_destroy(tbl);
	xfree(conf_path);

	if (!rc && env_exclude) {
		if ((rc = regcomp(&oci->env_exclude, env_exclude,
				  REG_EXTENDED)))
			dump_regex_error(rc, &oci->env_exclude, "compile %s",
					 env_exclude);
		else
			oci->env_exclude_set = true;
	}
	xfree(env_exclude);

	if (!rc && runtime_env_exclude) {
		if ((rc = regcomp(&oci->runtime_env_exclude, runtime_env_exclude,
				  REG_EXTENDED)))
			dump_regex_error(rc, &oci->runtime_env_exclude,
					 "compile %s", runtime_env_exclude);
		else
			oci->runtime_env_exclude_set = true;
	}
	xfree(runtime_env_exclude);

	if (!rc) {
		const char *envfile = "disabled";
		free_oci_conf(*oci_ptr);
		*oci_ptr = oci;

		if (oci->create_env_file == NULL_TERMINATED_ENV_FILE)
			envfile = "null";
		else if (oci->create_env_file == NEWLINE_TERMINATED_ENV_FILE)
			envfile = "newline";

		debug("%s: oci.conf loaded: ContainerPath=%s CreateEnvFile=%s RunTimeCreate=%s RunTimeDelete=%s RunTimeKill=%s RunTimeQuery=%s RunTimeRun=%s RunTimeStart=%s IgnoreFileConfigJson=%c",
		      __func__, oci->container_path, envfile,
		      oci->runtime_create, oci->runtime_delete,
		      oci->runtime_kill, oci->runtime_query, oci->runtime_run,
		      oci->runtime_start,
		      (oci->ignore_config_json ? 'T' : 'F'));
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
	regfree(&oci->runtime_env_exclude);
	xfree(oci->mount_spool_dir);
	xfree(oci->runtime_create);
	xfree(oci->runtime_delete);
	xfree(oci->runtime_kill);
	regfree(&oci->runtime_env_exclude);
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
