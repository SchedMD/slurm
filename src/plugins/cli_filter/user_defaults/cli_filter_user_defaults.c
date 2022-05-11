/*****************************************************************************\
 *  cli_filter_user_defaults.c - cli_filter plugin to read user-specific defaults
 *****************************************************************************
 *  Copyright (C) 2017-2019 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory (cf, DISCLAIMER).
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
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

#define _GNU_SOURCE

#include <inttypes.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/cli_filter.h"
#include "src/common/read_config.h"
#include "src/common/slurm_opt.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/cli_filter/common/cli_filter_common.h"

#define USER_DEFAULTS_FILE ".slurm/defaults"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for SLURM authentication) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "cli filter user defaults plugin";
const char plugin_type[]       	= "cli_filter/user_defaults";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*****************************************************************************\
 * We've provided a simple example of the type of things you can do with this
 * plugin. If you develop another plugin that may be of interest to others
 * please post it to slurm-dev@schedmd.com  Thanks!
\*****************************************************************************/

static char *_trim(char *str) {
	char *ptr = str;
	ssize_t len = 0;
	if (!str ) return NULL;
	for ( ; isspace(*ptr) && *ptr != 0; ptr++) {
		/* that's it */
	}
	if (*ptr == '\0') return ptr;
	len = strlen(ptr) - 1;
	for ( ; isspace(*(ptr + len)) && len > 0; len--)
		*(ptr + len) = '\0';
	return ptr;
}

static int _set_default(slurm_opt_t *opt, bool early,
			char *key, char *value, int line)
{
	int rc = SLURM_SUCCESS;
	char *tokens[3] = { NULL, NULL, NULL };
	int n_tokens = 0, used_tokens = 0;
	char *ptr, *search, *sv = NULL;
	char *command = NULL, *cluster = NULL, *component = NULL;

	search = key;
	/* sbatch:edison:constraint = ivybridge
         * edison:constraint = ivybridge
	 */
	while ((ptr = strtok_r(search, ":", &sv)) != NULL && n_tokens < 3) {
		search = NULL;
		tokens[n_tokens++] = ptr;
	}
	if (n_tokens > 2)
		command = _trim(tokens[used_tokens++]);
	if (n_tokens > 1)
		cluster = _trim(tokens[used_tokens++]);
	component = _trim(tokens[used_tokens++]);

	if (command != NULL) {
		if (strcasecmp(command, "salloc") == 0) {
			if (!opt->salloc_opt)
				goto cleanup;
		} else if (strcasecmp(command, "sbatch") == 0) {
			if (!opt->sbatch_opt)
				goto cleanup;
		} else if (strcasecmp(command, "srun") == 0) {
			if (!opt->srun_opt)
				goto cleanup;
		} else if (strcmp(command, "*") == 0) {
			/* any ok */
		} else {
			error("Unknown command \"%s\" in ~/%s, line %d",
			      command, USER_DEFAULTS_FILE, line);
			rc = SLURM_ERROR;
			goto cleanup;
		}
	}

	if (cluster != NULL && cluster[0] != '*' &&
	    xstrcmp(cluster, slurm_conf.cluster_name)) {
		/* if not for this cluster, exit */
		goto cleanup;
	}

	slurm_option_set(opt, component, value, early);
cleanup:
	return rc;
}

extern int cli_filter_p_setup_defaults(slurm_opt_t *opt, bool early)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE];
	char defaults_path[PATH_MAX];
	char *line = NULL;
	size_t line_sz = 0;
	ssize_t nbytes = 0;
	FILE *fp = NULL;
	int rc = 0, line_cnt = 0;

	rc = slurm_getpwuid_r(getuid(), &pwd, buffer, PW_BUF_SIZE, &result);
	if (!result || (rc != 0)) {
		error("Failed to lookup user homedir to load slurm defaults.");
		return SLURM_SUCCESS;
	}
	snprintf(defaults_path, sizeof(defaults_path), "%s/%s",
		 result->pw_dir, USER_DEFAULTS_FILE);
	fp = fopen(defaults_path, "r");

	if (!fp) {
		/* $HOME/<USER_DEFAULTS_FILE> does not exist or is not readable
		 * will assume user wants stock defaults */
		return SLURM_SUCCESS;
	}

	/* parse user $HOME/<USER_DEFAULTS_FILE> file here and populate opt data
	 * structure with user-selected defaults */

	while (!feof(fp) && !ferror(fp)) {
		char *key = NULL;
		char *value = NULL;
		char *trimmed = NULL;
		nbytes = getline(&line, &line_sz, fp);
		if (nbytes <= 0)
			break;

		line_cnt++;
		trimmed = _trim(line);
		if (trimmed[0] == '#')
			continue;

		value = xstrchr(trimmed, '=');
		if (!value)
			continue;
		*value++ = '\0';
		key = _trim(trimmed);
		value = _trim(value);

		_set_default(opt, early, key, value, line_cnt);
	}

	if (line)
		free(line);
	if (fp)
		fclose(fp);

	return SLURM_SUCCESS;
}

extern int cli_filter_p_pre_submit(slurm_opt_t *opt, int offset)
{
	return SLURM_SUCCESS;
}

extern int cli_filter_p_post_submit(int offset, uint32_t jobid, uint32_t stepid)
{
	return SLURM_SUCCESS;
}
