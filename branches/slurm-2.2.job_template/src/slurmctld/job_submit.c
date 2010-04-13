/*****************************************************************************\
 *  job_submit.c - driver for job_submit plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#if HAVE_CONFIG_H
#  include "config.h"
#  if STDC_HEADERS
#    include <string.h>
#  endif
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif /* HAVE_SYS_TYPES_H */
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else /* ! HAVE_INTTYPES_H */
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#else /* ! HAVE_CONFIG_H */
#  include <sys/types.h>
#  include <unistd.h>
#  include <stdint.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct slurm_submit_ops {
	int		(*submit)	( struct job_descriptor *job_desc );
	int		(*modify)	( struct job_descriptor *job_desc,
					  struct job_record *job_ptr);
} slurm_submit_ops_t;

typedef struct slurm_submit_context {
	char	       	*sched_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		sched_errno;
	slurm_submit_ops_t ops;
} slurm_submit_context_t;

static int submit_context_cnt = -1;
static slurm_submit_context_t *submit_context = NULL;
static char *submit_plugin_list = NULL;
static pthread_mutex_t submit_context_lock = PTHREAD_MUTEX_INITIALIZER;

static int _load_submit_plugin(char *plugin_name, 
			       slurm_submit_context_t *plugin_context)
{
	/*
	 * Must be synchronized with slurm_submit_ops_t above.
	 */
	static const char *syms[] = {
		"job_submit",
		"job_modify"
	};
	int n_syms = sizeof(syms) / sizeof(char *);

	/* Find the correct plugin */
	plugin_context->sched_type	= xstrdup("job_submit/");
	xstrcat(plugin_context->sched_type, plugin_name);
	plugin_context->plugin_list	= NULL;
	plugin_context->cur_plugin	= PLUGIN_INVALID_HANDLE;
	plugin_context->sched_errno 	= SLURM_SUCCESS;

        plugin_context->cur_plugin = plugin_load_and_link(
					plugin_context->sched_type, 
					n_syms, syms,
					(void **) &plugin_context->ops);
        if (plugin_context->cur_plugin != PLUGIN_INVALID_HANDLE)
        	return SLURM_SUCCESS;

	error("job_submit: Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      plugin_context->sched_type);

	/* Get plugin list */
	if (plugin_context->plugin_list == NULL) {
		char *plugin_dir;
		plugin_context->plugin_list = plugrack_create();
		if (plugin_context->plugin_list == NULL) {
			error("job_submit: cannot create plugin manager");
			return SLURM_ERROR;
		}
		plugrack_set_major_type(plugin_context->plugin_list,
					"job_submit");
		plugrack_set_paranoia(plugin_context->plugin_list,
				      PLUGRACK_PARANOIA_NONE, 0);
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir(plugin_context->plugin_list, plugin_dir);
		xfree(plugin_dir);
	}

	plugin_context->cur_plugin = plugrack_use_by_type(
					plugin_context->plugin_list,
					plugin_context->sched_type );
	if (plugin_context->cur_plugin == PLUGIN_INVALID_HANDLE) {
		error("job_submit: cannot find scheduler plugin for %s", 
		       plugin_context->sched_type);
		return SLURM_ERROR;
	}

	/* Dereference the API. */
	if (plugin_get_syms(plugin_context->cur_plugin,
			    n_syms, syms,
			    (void **) &plugin_context->ops ) < n_syms ) {
		error("job_submit: incomplete plugin detected");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

static int _unload_submit_plugin(slurm_submit_context_t *plugin_context)
{
	int rc;

	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if (plugin_context->plugin_list)
		rc = plugrack_destroy(plugin_context->plugin_list);
	else {
		rc = SLURM_SUCCESS;
		plugin_unload(plugin_context->cur_plugin);
	}
	xfree(plugin_context->sched_type);

	return rc;
}

/*
 * Initialize the job submit plugin.
 *
 * Returns a SLURM errno.
 */
extern int job_submit_plugin_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last, *names, *one_name;

	slurm_mutex_lock(&submit_context_lock);
	if (submit_context_cnt >= 0)
		goto fini;

	submit_plugin_list = slurm_get_job_submit_plugins();
	submit_context_cnt = 0;
	if ((submit_plugin_list == NULL) || (submit_plugin_list[0] == '\0'))
		goto fini;

	submit_context_cnt = 0;
	names = xstrdup(submit_plugin_list);
	one_name = strtok_r(names, ",", &last);
	while (one_name) {
		xrealloc(submit_context, (sizeof(slurm_submit_context_t) * 
			 (submit_context_cnt + 1)));
		rc = _load_submit_plugin(one_name, 
					 submit_context + submit_context_cnt);
		if (rc != SLURM_SUCCESS)
			break;
		submit_context_cnt++;
		one_name = strtok_r(NULL, ",", &last);
	}
	xfree(names);

fini:	slurm_mutex_unlock(&submit_context_lock);
	return rc;
}

/*
 * Terminate the job submit plugin. Free memory.
 *
 * Returns a SLURM errno.
 */
extern int job_submit_plugin_fini(void)
{
	int i, j, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&submit_context_lock);
	if (submit_context_cnt < 0)
		goto fini;

	for (i=0; i<submit_context_cnt; i++) {
		j = _unload_submit_plugin(submit_context + i);
		if (j != SLURM_SUCCESS)
			rc = j;
	}
	xfree(submit_context);
	xfree(submit_plugin_list);
	submit_context_cnt = -1;

fini:	slurm_mutex_unlock(&submit_context_lock);
	return rc;
}

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Perform reconfig, re-read any configuration files
 */
extern int job_submit_plugin_reconfig(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_names = slurm_get_job_submit_plugins();
	bool plugin_change;

	if (!plugin_names && !submit_plugin_list)
		return rc;

	slurm_mutex_lock(&submit_context_lock);
	if (plugin_names && submit_plugin_list &&
	    strcmp(plugin_names, submit_plugin_list))
		plugin_change = true;
	else
		plugin_change = false;
	slurm_mutex_unlock(&submit_context_lock);

	if (plugin_change) {
		info("JobSubmitPlugins changed to %s", plugin_names);
		rc = job_submit_plugin_fini();
		if (rc == SLURM_SUCCESS)
			rc = job_submit_plugin_init();
	}
	xfree(plugin_names);

	return rc;
}

/*
 * Execute the job_submit() function in each job submit plugin.
 * If any plugin function returns anything other than SLURM_SUCCESS
 * then stop and forward it's return value.
 */
extern int job_submit_plugin_submit(struct job_descriptor *job_desc)
{
	int i, rc;

	rc = job_submit_plugin_init();
	slurm_mutex_lock(&submit_context_lock);
	for (i=0; ((i < submit_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(submit_context[i].ops.submit))(job_desc);
	slurm_mutex_unlock(&submit_context_lock);
	return rc;
}

/*
 * Execute the job_modify() function in each job submit plugin.
 * If any plugin function returns anything other than SLURM_SUCCESS
 * then stop and forward it's return value.
 */
extern int job_submit_plugin_modify(struct job_descriptor *job_desc,
				    struct job_record *job_ptr)
{
	int i, rc;

	rc = job_submit_plugin_init();
	slurm_mutex_lock(&submit_context_lock);
	for (i=0; ((i < submit_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(submit_context[i].ops.modify))(job_desc, job_ptr);
	slurm_mutex_unlock(&submit_context_lock);
	return rc;
}
