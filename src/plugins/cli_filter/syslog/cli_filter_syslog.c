/*****************************************************************************\
 *  cli_filter_syslog.c - Basic logging example plugin
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
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <syslog.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/interfaces/cli_filter.h"
#include "src/common/data.h"
#include "src/common/slurm_opt.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"
#include "src/plugins/cli_filter/common/cli_filter_common.h"

static char **stored_data = NULL;
static size_t stored_n = 0;
static size_t stored_sz = 0;

/* Required Slurm plugin symbols: */
const char plugin_name[] = "cli filter syslog plugin";
const char plugin_type[] = "cli_filter/syslog";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static int _store_data(int key, const char *data)
{
	if (key >= stored_sz) {
		stored_data = xrealloc(stored_data,
				       (key + 24) * sizeof(char *));
		stored_sz = key + 24;
	}
	if (key > stored_n)
		stored_n = key;
	stored_data[key] = xstrdup(data);
	return 0;
}

static char *_retrieve_data(int key)
{
	if (key <= stored_n && stored_data[key])
		return xstrdup(stored_data[key]);
	return NULL;
}

/*
 *  NOTE: The init callback should never be called multiple times,
 *   let alone called from multiple threads. Therefore, locking
 *   is unnecessary here.
 */
extern int init(void)
{
	stored_data = xcalloc(24, sizeof(char *));
	stored_sz = 24;

	serializer_required(MIME_TYPE_JSON);

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	for (int i = 0; i < stored_n; i++)
		xfree(stored_data[i]);
	xfree(stored_data);
}

/*****************************************************************************\
 * We've provided a simple example of the type of things you can do with this
 * plugin. If you develop another plugin that may be of interest to others
 * please post it to slurm-dev@schedmd.com  Thanks!
\*****************************************************************************/

extern int cli_filter_p_setup_defaults(slurm_opt_t *opt, bool early)
{
	return SLURM_SUCCESS;
}

extern int cli_filter_p_pre_submit(slurm_opt_t *opt, int offset)
{
	char *json = cli_filter_json_set_options(opt);
	_store_data(offset, json);
	xfree(json);
	return SLURM_SUCCESS;
}

extern void cli_filter_p_post_submit(
	int offset, uint32_t jobid, uint32_t stepid)
{
	char *json_env = cli_filter_json_env();
	char *json_opt = _retrieve_data(offset);
	char *json = NULL;
	const char *tag = "slurm/cli_filter/syslog";

	json = xstrdup_printf(
		"{ \"jobid\":%u,\"stepid\":%u,\"options\":%s,\"env\":%s}",
		jobid, stepid, json_opt, json_env);
	openlog(tag, LOG_PID, LOG_USER);
	syslog(LOG_NOTICE, "post_submit: %s", json);
	closelog();
	xfree(json_env);
	xfree(json_opt);
	xfree(json);
}
